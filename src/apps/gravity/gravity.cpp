#include "gravity.h"

#include "board_config.h"
#include "ui/control_center.h"

#include <Arduino.h>

namespace {

constexpr uint32_t kPaperDay = 0xF7F7F7;
constexpr uint32_t kInkDay = 0x535353;
constexpr int kGroundY = 196;
constexpr int kDinoX = 26;
constexpr int kDinoH = 36;
constexpr float kGravity = 820.f;
constexpr float kJumpV = -360.f;
constexpr float kAirTime = 2.f * 360.f / 820.f;
constexpr int kInkMax = 48;
constexpr int kTextMax = 8;

enum class Phase : uint8_t { ready, running, dead };

enum class Kind : uint8_t { short_cactus, tall_cactus, twin_cactus, low_bird, high_bird };

struct Box {
    int x;
    int y;
    int w;
    int h;
};

struct Drifter {
    lv_obj_t *obj;
    float x;
    int y;
    int w;
};

struct Cloud {
    lv_obj_t *root;
    float x;
    int y;
    int w;
};

struct Obs {
    lv_obj_t *root;
    lv_obj_t *stem;
    lv_obj_t *arm_l;
    lv_obj_t *arm_r;
    lv_obj_t *stem2;
    lv_obj_t *wing;
    lv_obj_t *beak;
    float x;
    int y;
    int w;
    int h;
    int hit_dy;
    int hit_h;
    Kind kind;
    bool live;
    bool near;
    bool passed;
};

lv_obj_t *s_root = nullptr;
lv_obj_t *s_world = nullptr;
lv_obj_t *s_back_hint = nullptr;
lv_obj_t *s_hi_label = nullptr;
lv_obj_t *s_score_label = nullptr;
lv_obj_t *s_hint = nullptr;
lv_obj_t *s_over = nullptr;
lv_obj_t *s_quip = nullptr;
lv_obj_t *s_bonus = nullptr;
lv_obj_t *s_dino = nullptr;
lv_obj_t *s_tail = nullptr;
lv_obj_t *s_body = nullptr;
lv_obj_t *s_head = nullptr;
lv_obj_t *s_eye = nullptr;
lv_obj_t *s_dead_x = nullptr;
lv_obj_t *s_leg0 = nullptr;
lv_obj_t *s_leg1 = nullptr;
lv_obj_t *s_ground = nullptr;
lv_obj_t *s_dash[6] = {};
lv_obj_t *s_dust[2] = {};
lv_obj_t *s_ink[kInkMax] = {};
lv_obj_t *s_text[kTextMax] = {};
Cloud s_cloud[2] = {};
lv_obj_t *s_puff[8] = {};
int s_puff_n = 0;
Drifter s_moon = {};
Drifter s_star[2] = {};
Obs s_obs[3] = {};
float s_dash_x[6] = {};
float s_dust_x[2] = {};
float s_dino_y = 0;
float s_vy = 0;
float s_score_acc = 0;
int s_ink_n = 0;
int s_text_n = 0;
int s_score = 0;
int s_hi = 0;
uint32_t s_last_ms = 0;
uint32_t s_dead_at = 0;
uint32_t s_shake_ms = 0;
uint32_t s_squash_ms = 0;
uint32_t s_bonus_ms = 0;
uint32_t s_pop_ms = 0;
uint32_t s_blink_ms = 0;
bool s_grounded = true;
Phase s_phase = Phase::ready;

bool night()
{
    return (s_score / 100) % 2 == 1;
}

uint32_t paper()
{
    return night() ? kInkDay : kPaperDay;
}

uint32_t ink()
{
    return night() ? kPaperDay : kInkDay;
}

uint32_t cloud_color()
{
    return night() ? 0x8A8A8A : 0xC8C8C8;
}

void hide_obj(lv_obj_t *obj)
{
    if (obj) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void show_obj(lv_obj_t *obj)
{
    if (obj) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *make_node(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t *make_rect(lv_obj_t *parent, int w, int h, int radius)
{
    lv_obj_t *obj = make_node(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(kInkDay), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    if (s_ink_n < kInkMax) s_ink[s_ink_n++] = obj;
    return obj;
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(kInkDay), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    if (s_text_n < kTextMax) s_text[s_text_n++] = obj;
    return obj;
}

void show_piece(lv_obj_t *obj, int x, int y, int w, int h)
{
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    show_obj(obj);
}

void hide_parts(Obs &obs)
{
    hide_obj(obs.stem);
    hide_obj(obs.arm_l);
    hide_obj(obs.arm_r);
    hide_obj(obs.stem2);
    hide_obj(obs.wing);
    hide_obj(obs.beak);
}

bool is_bird(Kind kind)
{
    return kind == Kind::low_bird || kind == Kind::high_bird;
}

void apply_theme()
{
    uint32_t paper_color = paper();
    uint32_t ink_color = ink();
    lv_obj_set_style_bg_color(s_root, lv_color_hex(paper_color), 0);
    for (int i = 0; i < s_ink_n; ++i) {
        lv_obj_set_style_bg_color(s_ink[i], lv_color_hex(ink_color), 0);
    }
    for (int i = 0; i < s_text_n; ++i) {
        lv_obj_set_style_text_color(s_text[i], lv_color_hex(ink_color), 0);
    }
    lv_obj_set_style_bg_color(s_eye, lv_color_hex(paper_color), 0);
    lv_obj_set_style_text_color(s_dead_x, lv_color_hex(paper_color), 0);
    if (s_back_hint) lv_obj_set_style_bg_color(s_back_hint, lv_color_hex(ink_color), 0);
    uint32_t puff_color = cloud_color();
    for (int i = 0; i < s_puff_n; ++i) {
        lv_obj_set_style_bg_color(s_puff[i], lv_color_hex(puff_color), 0);
    }

    bool dark = night();
    for (Cloud &cloud : s_cloud) {
        if (dark) hide_obj(cloud.root);
        else show_obj(cloud.root);
    }
    if (dark) show_obj(s_moon.obj);
    else hide_obj(s_moon.obj);
    for (Drifter &star : s_star) {
        if (dark) show_obj(star.obj);
        else hide_obj(star.obj);
    }
}

void refresh_hud()
{
    lv_label_set_text_fmt(s_hi_label, "HI %05d", s_hi);
    lv_label_set_text_fmt(s_score_label, "%05d", s_score);
}

void sync_score_style()
{
    const lv_font_t *font = s_pop_ms > 0 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    lv_obj_set_style_text_font(s_score_label, font, 0);
    lv_obj_align(s_score_label, LV_ALIGN_TOP_RIGHT, -6, 16);
}

void add_score(int points)
{
    if (points <= 0 || s_score >= 99999) return;
    int band = s_score / 100;
    s_score += points;
    if (s_score > 99999) s_score = 99999;
    if (s_score > s_hi) s_hi = s_score;
    refresh_hud();
    if (s_score / 100 == band) return;
    s_pop_ms = 280;
    sync_score_style();
    apply_theme();
}

void layout_dino()
{
    int squash = s_squash_ms > 0 ? 4 : 0;
    bool run_legs = s_phase == Phase::running && s_grounded && squash == 0;
    bool step = ((millis() / 120) & 1) == 0;
    lv_obj_set_pos(s_dino, kDinoX, (int)s_dino_y + squash / 2);
    lv_obj_set_pos(s_tail, 0, 16 + squash / 2);
    lv_obj_set_pos(s_body, 8, 12 + squash);
    lv_obj_set_size(s_body, 16, 14 - squash);
    lv_obj_set_pos(s_head, 16, 2);
    lv_obj_set_pos(s_eye, 24, 5);
    lv_obj_set_pos(s_dead_x, 21, 0);
    lv_obj_set_y(s_leg0, run_legs && step ? 22 : 26);
    lv_obj_set_height(s_leg0, run_legs && step ? 8 : 10);
    lv_obj_set_y(s_leg1, run_legs && !step ? 22 : 26);
    lv_obj_set_height(s_leg1, run_legs && !step ? 8 : 10);
}

Box dino_box()
{
    return {kDinoX + 10, (int)s_dino_y + 8, 12, 22};
}

bool overlap(Box a, Box b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

int vertical_gap(Box a, Box b)
{
    if (a.y + a.h <= b.y) return b.y - (a.y + a.h);
    if (b.y + b.h <= a.y) return a.y - (b.y + b.h);
    return 0;
}

const char *quip_for(Kind kind)
{
    if (kind == Kind::low_bird) return lv_rand(0, 1) ? "ERR_BIRD" : "ERR_TOO_LOW";
    if (kind == Kind::high_bird) return lv_rand(0, 1) ? "ERR_BIRD" : "ERR_TOO_HIGH";
    const char *cactus[] = {"ERR_CACTUS", "ERR_TOO_SLOW", "STILL NO INTERNET", "ERR_JUMP"};
    return cactus[lv_rand(0, 3)];
}

void reset_round();

void die(Kind kind)
{
    if (s_phase != Phase::running) return;
    s_phase = Phase::dead;
    s_dead_at = millis();
    s_shake_ms = 220;
    s_vy = 0;
    hide_obj(s_eye);
    hide_obj(s_hint);
    hide_obj(s_bonus);
    show_obj(s_dead_x);
    show_obj(s_over);
    lv_label_set_text(s_quip, quip_for(kind));
    show_obj(s_quip);
}

void flash_bonus()
{
    s_bonus_ms = 420;
    lv_label_set_text(s_bonus, "+5");
    show_obj(s_bonus);
}

Kind pick_kind()
{
    uint32_t roll = lv_rand(0, 99);
    if (s_score >= 100 && roll < 16) return roll < 8 ? Kind::low_bird : Kind::high_bird;
    if (roll % 5 == 0) return Kind::twin_cactus;
    if (roll % 5 == 1) return Kind::tall_cactus;
    return Kind::short_cactus;
}

void layout_kind(Obs &obs)
{
    hide_parts(obs);
    switch (obs.kind) {
    case Kind::short_cactus:
        obs.w = 16;
        obs.h = 24;
        obs.y = kGroundY - obs.h;
        obs.hit_dy = 2;
        obs.hit_h = 20;
        show_piece(obs.stem, 5, 2, 6, 22);
        show_piece(obs.arm_l, 0, 8, 6, 4);
        show_piece(obs.arm_r, 10, 12, 6, 4);
        break;
    case Kind::tall_cactus:
        obs.w = 16;
        obs.h = 38;
        obs.y = kGroundY - obs.h;
        obs.hit_dy = 2;
        obs.hit_h = 34;
        show_piece(obs.stem, 5, 2, 6, 36);
        show_piece(obs.arm_l, 0, 12, 6, 4);
        show_piece(obs.arm_r, 10, 20, 6, 4);
        break;
    case Kind::twin_cactus:
        obs.w = 22;
        obs.h = 26;
        obs.y = kGroundY - obs.h;
        obs.hit_dy = 4;
        obs.hit_h = 18;
        show_piece(obs.stem, 1, 6, 5, 20);
        show_piece(obs.arm_l, 0, 12, 4, 3);
        show_piece(obs.stem2, 13, 2, 5, 24);
        show_piece(obs.arm_r, 17, 10, 5, 3);
        break;
    case Kind::low_bird:
        obs.w = 20;
        obs.h = 16;
        obs.y = kGroundY - 26;
        obs.hit_dy = 6;
        obs.hit_h = 7;
        show_piece(obs.stem, 2, 6, 12, 5);
        show_piece(obs.beak, 14, 7, 5, 2);
        show_piece(obs.wing, 4, 2, 8, 3);
        break;
    case Kind::high_bird:
        obs.w = 20;
        obs.h = 16;
        obs.y = kGroundY - 108;
        obs.hit_dy = 6;
        obs.hit_h = 7;
        show_piece(obs.stem, 2, 6, 12, 5);
        show_piece(obs.beak, 14, 7, 5, 2);
        show_piece(obs.wing, 4, 2, 8, 3);
        break;
    }
    lv_obj_set_size(obs.root, obs.w, obs.h);
}

void spawn(Obs &obs)
{
    obs.kind = pick_kind();
    obs.live = true;
    obs.near = false;
    obs.passed = false;
    obs.x = 248.f + (float)lv_rand(0, 28);
    layout_kind(obs);
    lv_obj_set_pos(obs.root, (int)obs.x, obs.y);
    show_obj(obs.root);
}

float run_speed()
{
    float speed = 90.f + (float)s_score * 0.25f;
    if (speed > 170.f) speed = 170.f;
    return speed;
}

void drift(Drifter &item, float distance)
{
    item.x -= distance;
    if (item.x < -item.w) item.x = (float)BOARD_LCD_H_RES + (float)lv_rand(8, 40);
    lv_obj_set_pos(item.obj, (int)item.x, item.y);
}

void tick_scenery(float dt, float speed)
{
    float cloud_speed = speed > 0 ? speed * 0.12f + 6.f : 0;
    for (Cloud &cloud : s_cloud) {
        cloud.x -= cloud_speed * dt;
        if (cloud.x < -cloud.w) cloud.x = (float)BOARD_LCD_H_RES + (float)lv_rand(12, 60);
        lv_obj_set_pos(cloud.root, (int)cloud.x, cloud.y);
    }
    drift(s_moon, cloud_speed * 0.7f * dt);
    for (Drifter &star : s_star) drift(star, cloud_speed * 0.45f * dt);
    for (int i = 0; i < 6; ++i) {
        s_dash_x[i] -= speed * dt;
        if (s_dash_x[i] < -16.f) s_dash_x[i] += (float)BOARD_LCD_H_RES;
        lv_obj_set_x(s_dash[i], (int)s_dash_x[i]);
    }
}

void tick_dust(float dt, float speed)
{
    bool on = s_phase == Phase::running && s_grounded;
    for (int i = 0; i < 2; ++i) {
        if (!on) {
            hide_obj(s_dust[i]);
            continue;
        }
        show_obj(s_dust[i]);
        s_dust_x[i] -= speed * 1.15f * dt;
        if (s_dust_x[i] < kDinoX - 8) s_dust_x[i] = (float)kDinoX + 8.f - i * 6.f;
        lv_obj_set_pos(s_dust[i], (int)s_dust_x[i], kGroundY - 5);
    }
}

void physics(float dt)
{
    s_vy += kGravity * dt;
    s_dino_y += s_vy * dt;
    float floor_y = (float)(kGroundY - kDinoH);
    if (s_dino_y < floor_y) {
        s_grounded = false;
        return;
    }
    if (!s_grounded && s_vy > 80.f) s_squash_ms = 110;
    s_dino_y = floor_y;
    s_vy = 0;
    s_grounded = true;
}

void tick_play(float dt)
{
    float speed = run_speed();
    Box dino = dino_box();
    for (Obs &obs : s_obs) {
        if (!obs.live) continue;
        obs.x -= speed * dt;
        if (is_bird(obs.kind)) lv_obj_set_y(obs.wing, ((millis() / 140) & 1) ? 2 : 11);
        Box box{(int)obs.x + 2, obs.y + obs.hit_dy, obs.w - 4, obs.hit_h};
        bool same_x = box.x < dino.x + dino.w && dino.x < box.x + box.w;
        if (overlap(dino, box)) {
            die(obs.kind);
            lv_obj_set_x(obs.root, (int)obs.x);
            return;
        }
        if (same_x) {
            int gap = vertical_gap(dino, box);
            if (gap > 0 && gap <= 8) obs.near = true;
        }
        if (!obs.passed && box.x + box.w < dino.x) {
            obs.passed = true;
            if (obs.near) {
                add_score(5);
                flash_bonus();
            }
        }
        if (obs.x + obs.w < 0) {
            obs.live = false;
            hide_obj(obs.root);
            continue;
        }
        lv_obj_set_pos(obs.root, (int)obs.x, obs.y);
    }
    if (s_phase != Phase::running) return;

    s_score_acc += speed * dt * 0.1f;
    int whole = (int)s_score_acc;
    if (whole > 0) {
        s_score_acc -= (float)whole;
        add_score(whole);
    }

    float tail = -1000.f;
    int alive = 0;
    Obs *slot = nullptr;
    for (Obs &obs : s_obs) {
        if (!obs.live) {
            if (!slot) slot = &obs;
            continue;
        }
        ++alive;
        float edge = obs.x + (float)obs.w;
        if (edge > tail) tail = edge;
    }
    float min_empty = run_speed() * kAirTime + 36.f;
    if (slot && (alive == 0 || tail + min_empty < 250.f)) spawn(*slot);
    tick_dust(dt, speed);
}

void tick_blink(uint32_t dt)
{
    if (s_phase == Phase::dead) return;
    s_blink_ms += dt;
    bool closed = s_blink_ms > 2100 && s_blink_ms < 2220;
    if (s_blink_ms >= 2220) s_blink_ms = 0;
    if (closed) hide_obj(s_eye);
    else show_obj(s_eye);
}

void tick_shake(uint32_t dt)
{
    if (s_shake_ms == 0) return;
    if (dt >= s_shake_ms) s_shake_ms = 0;
    else s_shake_ms -= dt;
    if (s_shake_ms == 0) {
        lv_obj_set_pos(s_world, 0, 0);
        return;
    }
    int mag = s_shake_ms > 100 ? 2 : 1;
    int dx = ((s_shake_ms / 40) & 1) ? mag : -mag;
    lv_obj_set_pos(s_world, dx, 0);
}

void tick_bonus(uint32_t dt)
{
    if (s_bonus_ms == 0) return;
    if (dt >= s_bonus_ms) s_bonus_ms = 0;
    else s_bonus_ms -= dt;
    if (s_bonus_ms == 0) {
        hide_obj(s_bonus);
        return;
    }
    int rise = (int)(420 - s_bonus_ms) / 10;
    lv_obj_set_pos(s_bonus, kDinoX + 30, (int)s_dino_y - 8 - rise);
}

void tick_pop(uint32_t dt)
{
    if (s_pop_ms == 0) return;
    if (dt >= s_pop_ms) s_pop_ms = 0;
    else s_pop_ms -= dt;
    if (s_pop_ms == 0) sync_score_style();
}

void tick_squash(uint32_t dt)
{
    if (s_squash_ms == 0) return;
    if (dt >= s_squash_ms) s_squash_ms = 0;
    else s_squash_ms -= dt;
}

void try_jump()
{
    if (s_phase != Phase::running || !s_grounded) return;
    s_vy = kJumpV;
    s_grounded = false;
}

void on_tap(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_PRESSED) return;
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        if (point.y <= 12 || point.y >= BOARD_LCD_V_RES - 10) return;
    }
    if (s_phase == Phase::dead) {
        if (millis() - s_dead_at < 400) return;
        reset_round();
        return;
    }
    if (s_phase == Phase::ready) {
        s_phase = Phase::running;
        hide_obj(s_hint);
    }
    try_jump();
}

void reset_round()
{
    s_phase = Phase::ready;
    s_score = 0;
    s_score_acc = 0;
    s_vy = 0;
    s_dino_y = (float)(kGroundY - kDinoH);
    s_grounded = true;
    s_squash_ms = 0;
    s_shake_ms = 0;
    s_bonus_ms = 0;
    s_pop_ms = 0;
    s_blink_ms = 0;
    lv_obj_set_pos(s_world, 0, 0);
    for (Obs &obs : s_obs) {
        obs.live = false;
        obs.near = false;
        obs.passed = false;
        hide_obj(obs.root);
    }
    hide_obj(s_dust[0]);
    hide_obj(s_dust[1]);
    s_dust_x[0] = (float)kDinoX;
    s_dust_x[1] = (float)kDinoX - 10.f;
    show_obj(s_hint);
    hide_obj(s_over);
    hide_obj(s_quip);
    hide_obj(s_bonus);
    hide_obj(s_dead_x);
    show_obj(s_eye);
    refresh_hud();
    sync_score_style();
    apply_theme();
    layout_dino();
}

void build_obs(Obs &obs)
{
    obs.root = make_node(s_world);
    obs.stem = make_rect(obs.root, 6, 6, 0);
    obs.arm_l = make_rect(obs.root, 6, 4, 0);
    obs.arm_r = make_rect(obs.root, 6, 4, 0);
    obs.stem2 = make_rect(obs.root, 6, 6, 0);
    obs.wing = make_rect(obs.root, 8, 3, 0);
    obs.beak = make_rect(obs.root, 5, 2, 0);
    hide_obj(obs.root);
    obs.live = false;
}

lv_obj_t *make_puff(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *puff = make_node(parent);
    lv_obj_set_pos(puff, x, y);
    lv_obj_set_size(puff, w, h);
    lv_obj_set_style_radius(puff, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(puff, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(puff, lv_color_hex(0xC8C8C8), 0);
    if (s_puff_n < 8) s_puff[s_puff_n++] = puff;
    return puff;
}

void build_cloud(Cloud &cloud, float x, int y, int w, int h, const int *puffs, int count)
{
    cloud.root = make_node(s_world);
    cloud.x = x;
    cloud.y = y;
    cloud.w = w;
    lv_obj_set_size(cloud.root, w, h);
    lv_obj_set_pos(cloud.root, (int)x, y);
    lv_obj_set_style_bg_opa(cloud.root, LV_OPA_TRANSP, 0);
    for (int i = 0; i < count; i += 4) make_puff(cloud.root, puffs[i], puffs[i + 1], puffs[i + 2], puffs[i + 3]);
}

} // namespace

void gravity_app::create(const AppContext &context)
{
    s_ink_n = 0;
    s_text_n = 0;
    s_puff_n = 0;
    s_last_ms = 0;
    s_root = context.root;
    s_back_hint = lv_obj_get_child(s_root, 0);
    if (s_back_hint) lv_obj_clear_flag(s_back_hint, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_bg_color(s_root, lv_color_hex(kPaperDay), 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, on_tap, LV_EVENT_PRESSED, nullptr);

    s_world = make_node(s_root);
    lv_obj_set_size(s_world, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_pos(s_world, 0, 0);
    lv_obj_set_style_bg_opa(s_world, LV_OPA_TRANSP, 0);

    const int puff_a[] = {4, 14, 46, 12, 0, 8, 20, 18, 16, 0, 26, 22, 36, 8, 18, 16};
    const int puff_b[] = {4, 12, 32, 10, 0, 7, 16, 14, 12, 0, 20, 18, 26, 6, 14, 14};
    build_cloud(s_cloud[0], 16.f, 48, 56, 28, puff_a, 16);
    build_cloud(s_cloud[1], 150.f, 78, 42, 24, puff_b, 16);
    s_moon = {make_rect(s_world, 14, 14, LV_RADIUS_CIRCLE), 180.f, 40, 14};
    s_star[0] = {make_rect(s_world, 3, 3, LV_RADIUS_CIRCLE), 48.f, 46, 3};
    s_star[1] = {make_rect(s_world, 3, 3, LV_RADIUS_CIRCLE), 120.f, 64, 3};
    lv_obj_set_pos(s_moon.obj, (int)s_moon.x, s_moon.y);
    lv_obj_set_pos(s_star[0].obj, (int)s_star[0].x, s_star[0].y);
    lv_obj_set_pos(s_star[1].obj, (int)s_star[1].x, s_star[1].y);

    s_ground = make_rect(s_world, BOARD_LCD_H_RES, 2, 0);
    lv_obj_set_pos(s_ground, 0, kGroundY);
    for (int i = 0; i < 6; ++i) {
        s_dash_x[i] = (float)(i * 40);
        s_dash[i] = make_rect(s_world, 12, 2, 0);
        lv_obj_set_pos(s_dash[i], (int)s_dash_x[i], kGroundY + 6);
    }

    for (Obs &obs : s_obs) build_obs(obs);
    s_dust[0] = make_rect(s_world, 3, 3, 0);
    s_dust[1] = make_rect(s_world, 3, 3, 0);

    s_dino = make_node(s_world);
    lv_obj_set_size(s_dino, 36, kDinoH);
    s_tail = make_rect(s_dino, 10, 5, 0);
    s_body = make_rect(s_dino, 16, 14, 0);
    s_head = make_rect(s_dino, 16, 12, 0);
    s_leg0 = make_rect(s_dino, 5, 10, 0);
    s_leg1 = make_rect(s_dino, 5, 10, 0);
    lv_obj_set_pos(s_leg0, 10, 26);
    lv_obj_set_pos(s_leg1, 18, 26);
    s_eye = make_node(s_dino);
    lv_obj_set_size(s_eye, 4, 4);
    lv_obj_set_style_bg_opa(s_eye, LV_OPA_COVER, 0);
    s_dead_x = lv_label_create(s_dino);
    lv_label_set_text(s_dead_x, "x");
    lv_obj_set_style_text_font(s_dead_x, &lv_font_montserrat_14, 0);
    lv_obj_clear_flag(s_dead_x, LV_OBJ_FLAG_CLICKABLE);

    s_bonus = make_label(s_world, "+5", &lv_font_montserrat_14);

    s_hi_label = make_label(s_root, "HI 00000", &lv_font_montserrat_12);
    s_score_label = make_label(s_root, "00000", &lv_font_montserrat_14);
    s_hint = make_label(s_root, "TAP TO START", &lv_font_montserrat_14);
    s_over = make_label(s_root, "GAME OVER", &lv_font_montserrat_20);
    s_quip = make_label(s_root, "ERR_CACTUS", &lv_font_montserrat_12);
    lv_obj_align(s_hi_label, LV_ALIGN_TOP_RIGHT, -6, 2);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, -28);
    lv_obj_align(s_over, LV_ALIGN_CENTER, 0, -36);
    lv_obj_align(s_quip, LV_ALIGN_CENTER, 0, -8);

    reset_round();
}

void gravity_app::update()
{
    if (!s_root || lv_screen_active() != s_root || control_center_is_visible()) {
        s_last_ms = 0;
        return;
    }
    uint32_t now = millis();
    if (s_last_ms == 0) {
        s_last_ms = now;
        return;
    }
    uint32_t dt_ms = now - s_last_ms;
    s_last_ms = now;
    if (dt_ms > 32) dt_ms = 32;
    if (dt_ms == 0) return;
    float dt = (float)dt_ms / 1000.f;

    float speed = 0;
    if (s_phase == Phase::running) {
        physics(dt);
        tick_play(dt);
        speed = run_speed();
    } else if (s_phase == Phase::ready) {
        speed = 16.f;
    }
    tick_scenery(dt, speed);
    tick_blink(dt_ms);
    tick_shake(dt_ms);
    tick_squash(dt_ms);
    tick_bonus(dt_ms);
    tick_pop(dt_ms);
    layout_dino();
}

void gravity_app::destroy()
{
    s_root = nullptr;
    s_world = nullptr;
    s_back_hint = nullptr;
    s_hi_label = nullptr;
    s_score_label = nullptr;
    s_hint = nullptr;
    s_over = nullptr;
    s_quip = nullptr;
    s_bonus = nullptr;
    s_dino = nullptr;
    s_tail = nullptr;
    s_body = nullptr;
    s_head = nullptr;
    s_eye = nullptr;
    s_dead_x = nullptr;
    s_leg0 = nullptr;
    s_leg1 = nullptr;
    s_ground = nullptr;
    s_moon = {};
    for (lv_obj_t *&dash : s_dash) dash = nullptr;
    for (lv_obj_t *&dust : s_dust) dust = nullptr;
    for (Cloud &cloud : s_cloud) cloud = {};
    for (lv_obj_t *&puff : s_puff) puff = nullptr;
    s_puff_n = 0;
    for (Drifter &star : s_star) star = {};
    for (Obs &obs : s_obs) obs = {};
    s_ink_n = 0;
    s_text_n = 0;
    s_last_ms = 0;
}
