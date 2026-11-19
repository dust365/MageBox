#include "keyboard_hid.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include <esp_random.h>

#include <cstring>
#include <vector>

namespace {

constexpr char kName[] = "MageBox Keyboard";
constexpr char kPrefsNs[] = "kbd_ble";
constexpr char kPrefsAddrKey[] = "rnd_addr";
constexpr char kPrefsSetupKey[] = "setup_ok";
constexpr uint16_t kAppearanceKeyboard = 0x03C1;
constexpr uint32_t kTapMs = 40;
constexpr uint32_t kSecDelayMs = 200;
constexpr uint32_t kSecTimeoutMs = 8000;
constexpr uint32_t kAdvRetryMs = 500;
constexpr uint32_t kPendingWaitMs = 600;

// 8-byte input report: modifiers, Apple Fn, six keycodes.
// Fn is Usage Page 0x00FF (Apple Vendor Top Case), Usage 0x0003.
// The report id lives in the HID reference descriptor, not in this payload.
constexpr uint8_t kReportMap[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)

    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0xE0,       //   Usage Minimum (Left Control)
    0x29, 0xE7,       //   Usage Maximum (Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data, Var, Abs)

    0x05, 0xFF,       //   Usage Page (Apple Vendor Top Case)
    0x09, 0x03,       //   Usage (Keyboard Fn)
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x02,       //   Input (Data, Var, Abs)

    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0x00,
    0x29, 0x73,
    0x15, 0x00,
    0x25, 0x73,
    0x75, 0x08,
    0x95, 0x06,       //   Report Count (6)
    0x81, 0x00,       //   Input (Data, Array)

    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,
    0x29, 0x05,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x05,
    0x91, 0x02,       //   Output (Data, Var, Abs)
    0x95, 0x01,
    0x75, 0x03,
    0x91, 0x01,       //   Output (Const)

    0xC0,
};

struct Report {
    uint8_t mods;
    uint8_t fn;
    uint8_t keys[6];
};

bool s_inited = false;
bool s_session = false;
volatile bool s_active = false;
volatile bool s_secure = false;
volatile bool s_need_sync = false;
volatile uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
volatile uint32_t s_connect_at = 0;
volatile bool s_sec_requested = false;
volatile uint32_t s_adv_retry_at = 0;

bool s_pending_forget = false;
uint32_t s_pending_at = 0;

bool s_needs_setup = false;
bool s_fn_down = false;
bool s_backspace_down = false;
uint8_t s_tap = 0;
uint32_t s_tap_at = 0;
uint8_t s_rnd_addr[6] = {};

NimBLEServer *s_server = nullptr;
NimBLEHIDDevice *s_hid = nullptr;
NimBLECharacteristic *s_input = nullptr;
Report s_report = {};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override
    {
        if (server->getConnectedCount() > 1) {
            server->disconnect(connInfo.getConnHandle());
            return;
        }
        s_conn_handle = connInfo.getConnHandle();
        s_connect_at = millis();
        s_sec_requested = false;
        s_secure = false;
        Serial.printf("Keyboard link up %s (handle=%u)\n", connInfo.getAddress().toString().c_str(),
                      connInfo.getConnHandle());
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override
    {
        (void)server;
        (void)connInfo;
        s_secure = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_sec_requested = false;
        s_adv_retry_at = 0;
        Serial.printf("Keyboard link down reason=%d\n", reason);
    }

    void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
    {
        if (!connInfo.isEncrypted()) {
            Serial.println("Keyboard encryption failed, disconnecting");
            s_secure = false;
            if (s_server) s_server->disconnect(connInfo.getConnHandle());
            return;
        }
        s_secure = true;
        s_need_sync = true;
        Serial.println("Keyboard encrypted");
    }
};

ServerCallbacks s_callbacks;

void save_setup_done(bool done)
{
    s_needs_setup = !done;
    Preferences prefs;
    if (prefs.begin(kPrefsNs, false)) {
        prefs.putBool(kPrefsSetupKey, done);
        prefs.end();
    }
}

void load_setup_state()
{
    if (NimBLEDevice::getNumBonds() == 0) {
        save_setup_done(false);
        return;
    }
    bool done = false;
    Preferences prefs;
    if (prefs.begin(kPrefsNs, true)) {
        done = prefs.getBool(kPrefsSetupKey, false);
        prefs.end();
    }
    s_needs_setup = !done;
}

bool is_valid_static_addr(const uint8_t addr[6])
{
    if ((addr[5] & 0xC0) != 0xC0) return false;
    int ones = __builtin_popcount(addr[0]) + __builtin_popcount(addr[1]) +
               __builtin_popcount(addr[2]) + __builtin_popcount(addr[3]) +
               __builtin_popcount(addr[4]) + __builtin_popcount(addr[5] & 0x3F);
    return ones > 0 && ones < 46;
}

void generate_static_addr(uint8_t out[6])
{
    do {
        esp_fill_random(out, 6);
        out[5] |= 0xC0; // Static random address requires top two bits = 11.
    } while (!is_valid_static_addr(out));
}

void load_or_create_addr(bool force_new)
{
    Preferences prefs;
    bool loaded = false;
    if (!force_new && prefs.begin(kPrefsNs, true)) {
        if (prefs.getBytesLength(kPrefsAddrKey) == sizeof(s_rnd_addr)) {
            uint8_t tmp[6] = {};
            if (prefs.getBytes(kPrefsAddrKey, tmp, sizeof(tmp)) == sizeof(tmp) &&
                is_valid_static_addr(tmp)) {
                memcpy(s_rnd_addr, tmp, sizeof(s_rnd_addr));
                loaded = true;
            }
        }
        prefs.end();
    }

    if (!loaded) {
        generate_static_addr(s_rnd_addr);
        if (prefs.begin(kPrefsNs, false)) {
            prefs.putBytes(kPrefsAddrKey, s_rnd_addr, sizeof(s_rnd_addr));
            prefs.end();
        }
    }
}

void apply_own_addr()
{
    if (NimBLEDevice::setOwnAddr(s_rnd_addr) &&
        NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM)) {
        Serial.printf("Keyboard BLE addr %02X:%02X:%02X:%02X:%02X:%02X\n", s_rnd_addr[5],
                      s_rnd_addr[4], s_rnd_addr[3], s_rnd_addr[2], s_rnd_addr[1], s_rnd_addr[0]);
    } else {
        Serial.println("Keyboard BLE setOwnAddr failed, falling back to public");
        NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
    }
}

void configure_advertising()
{
    if (!s_hid) return;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (!adv) return;

    NimBLEAdvertisementData advData;
    advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    advData.setAppearance(kAppearanceKeyboard);
    advData.addServiceUUID(s_hid->getHidService()->getUUID());
    advData.setName(kName);

    NimBLEAdvertisementData scanData;
    scanData.addTxPower();

    adv->setConnectableMode(BLE_GAP_CONN_MODE_UND);
    adv->setDiscoverableMode(BLE_GAP_DISC_MODE_GEN);
    adv->setScanFilter(false, false);
    adv->enableScanResponse(true);
    if (!adv->setAdvertisementData(advData)) {
        Serial.println("Keyboard setAdvertisementData failed");
    }
    if (!adv->setScanResponseData(scanData)) {
        Serial.println("Keyboard setScanResponseData failed");
    }
    adv->setMinInterval(32); // 20 ms for fast discovery on macOS
    adv->setMaxInterval(64); // 40 ms
}

void send_report()
{
    if (!s_secure || !s_input) return;
    s_input->setValue(reinterpret_cast<const uint8_t *>(&s_report), sizeof(s_report));
    s_input->notify();
}

void remove_key(uint8_t usage)
{
    for (uint8_t &key : s_report.keys) {
        if (key == usage) key = 0;
    }
}

void add_key(uint8_t usage)
{
    for (uint8_t key : s_report.keys) {
        if (key == usage) return;
    }
    for (uint8_t &key : s_report.keys) {
        if (key == 0) {
            key = usage;
            return;
        }
    }
}

void apply_latched_keys()
{
    s_report.fn = s_fn_down ? 1 : 0;
    if (s_backspace_down) add_key(keyboard_hid::key_backspace);
    else remove_key(keyboard_hid::key_backspace);
}

void stop_advertising()
{
    NimBLEDevice::stopAdvertising();
}

void disconnect_all()
{
    if (!s_server) return;
    std::vector<uint16_t> peers = s_server->getPeerDevices();
    for (uint16_t handle : peers) s_server->disconnect(handle);
}

bool start_advertising()
{
    if (!s_active || !s_server || s_server->getConnectedCount() > 0) return false;
    NimBLEAdvertising *adv = s_server->getAdvertising();
    if (!adv) return false;
    if (adv->isAdvertising()) return true;

    configure_advertising();
    if (adv->start()) {
        Serial.println("Keyboard advertising started");
        return true;
    }
    Serial.println("Keyboard advertising start failed, will retry");
    return false;
}

void ensure_init()
{
    if (s_inited) return;

    NimBLEDevice::init(kName);
    NimBLEDevice::setPower(9);
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    load_or_create_addr(false);
    apply_own_addr();
    load_setup_state();

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_callbacks, false);
    s_server->advertiseOnDisconnect(false);

    s_hid = new NimBLEHIDDevice(s_server);
    s_hid->setReportMap(const_cast<uint8_t *>(kReportMap), sizeof(kReportMap));
    s_hid->setManufacturer("MageBox");
    // Magic Keyboard A1644 over Bluetooth. macOS only accepts the
    // AppleVendorTopCase Fn usage from a keyboard identity with Fn support.
    s_hid->setPnp(0x01, 0x004C, 0x0267, 0x0100);
    s_hid->setHidInfo(0x00, 0x02);
    s_hid->setBatteryLevel(100);
    s_input = s_hid->getInputReport(1);
    s_hid->getOutputReport(1);
    s_server->start();

    configure_advertising();
    s_inited = true;
    Serial.println("Keyboard BLE ready");
}

} // namespace

namespace keyboard_hid {

void start()
{
    ensure_init();
    if (!s_session) {
        s_session = true;
        load_setup_state();
        s_tap = 0;
        s_fn_down = false;
        s_backspace_down = false;
        s_report = {};
    }
    if (!s_active) {
        s_active = true;
        s_adv_retry_at = 0;
        start_advertising();
    }
}

void stop()
{
    if (!s_inited) return;
    s_active = false;
    s_pending_forget = false;
    s_fn_down = false;
    s_backspace_down = false;
    s_tap = 0;
    s_report = {};
    send_report();
    s_secure = false;
    stop_advertising();
    disconnect_all();
}

void end_session()
{
    stop();
    s_session = false;
}

void forget()
{
    if (!s_inited) return;
    save_setup_done(false);
    s_fn_down = false;
    s_backspace_down = false;
    s_tap = 0;
    s_report = {};
    send_report();
    s_secure = false;
    stop_advertising();
    disconnect_all();
    s_pending_forget = true;
    s_pending_at = millis();
    Serial.println("Keyboard forget requested");
}

void poll()
{
    if (!s_inited || !s_server) return;
    uint32_t now = millis();

    if (s_pending_forget) {
        if (s_server->getConnectedCount() == 0) {
            s_pending_forget = false;
            stop_advertising();
            bool cleared = NimBLEDevice::deleteAllBonds();
            Serial.printf("Keyboard bonds cleared=%d (remaining=%d)\n", cleared ? 1 : 0,
                          NimBLEDevice::getNumBonds());
            // Rotate static random address so macOS treats this as a fresh
            // peripheral and lists it under Nearby Devices even if the old
            // pairing record was not removed from macOS Bluetooth settings.
            load_or_create_addr(true);
            apply_own_addr();
            s_adv_retry_at = 0;
            if (s_active) start_advertising();
        } else if (now - s_pending_at >= kPendingWaitMs) {
            s_pending_at = now;
            disconnect_all();
        }
        return;
    }

    if (s_server->getConnectedCount() > 0) {
        if (!s_secure) {
            uint16_t handle = s_conn_handle;
            if (!s_sec_requested && handle != BLE_HS_CONN_HANDLE_NONE &&
                now - s_connect_at >= kSecDelayMs) {
                s_sec_requested = true;
                NimBLEDevice::startSecurity(handle);
            } else if (now - s_connect_at >= kSecTimeoutMs) {
                Serial.println("Keyboard security timeout, dropping stale link");
                disconnect_all();
            }
        }
    } else if (s_active) {
        NimBLEAdvertising *adv = s_server->getAdvertising();
        if (adv && !adv->isAdvertising() &&
            (s_adv_retry_at == 0 || static_cast<int32_t>(now - s_adv_retry_at) >= 0)) {
            if (!start_advertising()) {
                s_adv_retry_at = now + kAdvRetryMs;
            }
        }
    }

    if (s_need_sync && s_secure) {
        s_need_sync = false;
        apply_latched_keys();
        send_report();
    }
    if (s_tap != 0 && now - s_tap_at >= kTapMs) {
        uint8_t usage = s_tap;
        s_tap = 0;
        remove_key(usage);
        send_report();
    }
}

bool secure()
{
    return s_secure;
}

bool needs_setup()
{
    return s_needs_setup;
}

void finish_setup()
{
    save_setup_done(true);
}

void set_fn(bool down)
{
    s_fn_down = down;
    s_report.fn = down ? 1 : 0;
    send_report();
}

void set_backspace(bool down)
{
    s_backspace_down = down;
    if (down) add_key(key_backspace);
    else remove_key(key_backspace);
    send_report();
}

void pulse_backspace()
{
    if (!s_backspace_down) return;
    remove_key(key_backspace);
    send_report();
    add_key(key_backspace);
    send_report();
}

void tap(uint8_t usage)
{
    if (!s_secure || usage == 0) return;
    if (s_tap != 0) remove_key(s_tap);
    add_key(usage);
    send_report();
    s_tap = usage;
    s_tap_at = millis();
}

} // namespace keyboard_hid
