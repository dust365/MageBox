#pragma once

#include <cstddef>

constexpr int sd_svc_name_max = 64;
constexpr int sd_svc_list_max = 64;

bool sd_svc_begin();
bool sd_svc_ready();
int sd_svc_list(const char *directory, char names[][sd_svc_name_max], int max_count,
                bool (*accept)(const char *name));
