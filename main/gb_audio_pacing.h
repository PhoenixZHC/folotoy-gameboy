#pragma once
#include <stddef.h>
#include <stdint.h>

#define GB_AUDIO_OUTPUT_RATE 14000u
#define GB_AUDIO_SOURCE_RATE 14000u
#define GB_AUDIO_SOURCE_SAMPLES 234u
#define GB_AUDIO_MAX_OUTPUT_SAMPLES 234u

// 按墙钟时间推进 APU，而不是把一帧声音拉长到两三帧。
size_t gb_audio_blocks_due(uint64_t elapsed_us, uint64_t generated_blocks);
size_t gb_audio_resampled_count(uint64_t generated_blocks);
