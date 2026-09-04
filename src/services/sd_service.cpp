#include "sd_service.h"

#include "board_config.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool s_ready = false;
bool s_pins_set = false;

int compare_names(const void *left, const void *right)
{
    return strcasecmp((const char *)left, (const char *)right);
}

} // namespace

bool sd_svc_begin()
{
    if (s_ready) {
        if (SD_MMC.cardType() != CARD_NONE) return true;
        Serial.println("SD card lost, remount");
        SD_MMC.end();
        s_ready = false;
    }

    if (!s_pins_set) {
        if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3)) {
            Serial.println("SD pin change failed");
            return false;
        }
        s_pins_set = true;
    }

    if (!SD_MMC.begin("/sdcard", false)) {
        Serial.println("SD 4-bit mount failed, try 1-bit");
        SD_MMC.end();
        if (!SD_MMC.begin("/sdcard", true)) {
            Serial.println("SD mount failed");
            return false;
        }
        Serial.println("SD mounted (1-bit)");
    } else {
        Serial.println("SD mounted (4-bit)");
    }

    s_ready = true;
    uint64_t mb = SD_MMC.cardSize() / (1024ULL * 1024ULL);
    Serial.printf("SD type=%u size=%llu MB\n", (unsigned)SD_MMC.cardType(), (unsigned long long)mb);
    return true;
}

bool sd_svc_ready()
{
    return s_ready;
}

int sd_svc_list(const char *directory, char names[][sd_svc_name_max], int max_count,
                bool (*accept)(const char *name))
{
    if (!s_ready || !directory || !names || max_count <= 0) return 0;

    File root = SD_MMC.open(directory);
    if (!root || !root.isDirectory()) {
        Serial.printf("SD dir missing: %s\n", directory);
        if (root) root.close();
        return 0;
    }

    int count = 0;
    File file = root.openNextFile();
    while (file && count < max_count) {
        if (!file.isDirectory()) {
            const char *name = file.name();
            char path[sd_svc_name_max];
            if (name && name[0] == '/') {
                snprintf(path, sizeof(path), "%s", name);
            } else {
                snprintf(path, sizeof(path), "%s/%s", directory, name ? name : "");
            }
            const char *base = strrchr(path, '/');
            base = base ? base + 1 : path;
            if (base[0] != '.' && (!accept || accept(path))) {
                strncpy(names[count], path, sd_svc_name_max - 1);
                names[count][sd_svc_name_max - 1] = 0;
                Serial.printf("SD file: %s (%u bytes)\n", names[count], (unsigned)file.size());
                count++;
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    if (count > 1) qsort(names, (size_t)count, sd_svc_name_max, compare_names);
    Serial.printf("SD listed %d file(s) in %s\n", count, directory);
    return count;
}
