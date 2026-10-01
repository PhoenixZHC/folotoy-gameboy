#pragma once
#include <stddef.h>
#include <stdint.h>

// ES8311 驱动支持 16 kHz / 4.096 MHz MCLK；14 kHz 不在其分频表内。
#define GB_AUDIO_OUTPUT_RATE 16000u
#define GB_AUDIO_SOURCE_RATE 16000u
#define GB_AUDIO_SOURCE_SAMPLES 267u
#define GB_AUDIO_MAX_OUTPUT_SAMPLES 267u

// 按墙钟时间推进 APU，而不是把一帧声音拉长到两三帧。
size_t gb_audio_blocks_due(uint64_t elapsed_us, uint64_t generated_blocks);
size_t gb_audio_resampled_count(uint64_t generated_blocks);
