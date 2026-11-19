#pragma once

#include <cstdint>

namespace keyboard_hid {

constexpr uint8_t key_enter = 0x28;
constexpr uint8_t key_backspace = 0x2A;
constexpr uint8_t key_z = 0x1D;
constexpr uint8_t key_slash = 0x38;

void start();
void stop();
void end_session();
void forget();
void poll();

bool secure();
bool needs_setup();
void finish_setup();

void set_fn(bool down);
void set_backspace(bool down);
void pulse_backspace();
void tap(uint8_t usage);

} // namespace keyboard_hid
