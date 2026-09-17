#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool running;
    char ssid[33];
} gb_web_status_t;

bool gb_web_start(void);
void gb_web_stop(void);
gb_web_status_t gb_web_status(void);
