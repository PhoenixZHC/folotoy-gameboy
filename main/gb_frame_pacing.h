#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t draw_interval;
    uint32_t frames;
    uint32_t window_frames;
    uint64_t window_work_us;
} gb_frame_pacing_t;

void gb_frame_pacing_init(gb_frame_pacing_t *pacing);
bool gb_frame_pacing_draw(const gb_frame_pacing_t *pacing);
void gb_frame_pacing_record(gb_frame_pacing_t *pacing, uint32_t work_us);
