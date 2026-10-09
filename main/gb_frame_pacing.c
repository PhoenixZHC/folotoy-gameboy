#include "gb_frame_pacing.h"

void gb_frame_pacing_init(gb_frame_pacing_t *pacing) {
    *pacing = (gb_frame_pacing_t){.draw_interval = 2};
}

bool gb_frame_pacing_draw(const gb_frame_pacing_t *pacing) {
    return !pacing->has_drawn ||
           (uint32_t)(pacing->frames - pacing->last_draw_frame) >= pacing->draw_interval;
}

void gb_frame_pacing_mark_drawn(gb_frame_pacing_t *pacing) {
    pacing->last_draw_frame = pacing->frames;
    pacing->has_drawn = true;
}

void gb_frame_pacing_record(gb_frame_pacing_t *pacing, uint32_t work_us) {
    pacing->frames++;
    pacing->window_frames++;
    pacing->window_work_us += work_us;
    // Reserve part of the 16.743 ms GB frame for BLE, audio and scheduling.
    // React within one second, before emulation has already slowed to 50 Hz.
    if (pacing->window_frames < 60) return;
    uint64_t average = pacing->window_work_us / pacing->window_frames;
    if (average > 16000 && pacing->draw_interval < 4) pacing->draw_interval++;
    else if (average < 14000 && pacing->draw_interval > 2) pacing->draw_interval--;
    pacing->window_frames = 0;
    pacing->window_work_us = 0;
}
