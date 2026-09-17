#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "gb_port.h"

typedef enum { GB_SAVE_EMPTY, GB_SAVE_LOADED, GB_SAVE_ERROR } gb_save_result_t;
bool gb_saves_init(void);
bool gb_saves_ready(void);
gb_save_result_t gb_saves_load(gb_session_t *session, const uint8_t rom_hash[32]);
bool gb_saves_write(gb_session_t *session, const uint8_t rom_hash[32]);
bool gb_saves_delete(const uint8_t rom_hash[32]);
bool gb_saves_prune_orphans(void);
