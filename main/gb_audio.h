#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gb_audio_pacing.h"
#define GB_AUDIO_SAMPLES GB_AUDIO_SOURCE_SAMPLES

bool gb_audio_start(uint8_t volume);
void gb_audio_submit(const int16_t *samples, size_t count);
bool gb_audio_healthy(void);
uint32_t gb_audio_dropped(void);
uint32_t gb_audio_output_samples(void);
uint32_t gb_audio_requested_samples(void);
uint32_t gb_audio_waits(void);
uint32_t gb_audio_max_wait_us(void);
bool gb_audio_stop(void);
