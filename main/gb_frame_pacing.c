#include "gb_frame_pacing.h"

void gb_frame_pacing_init(gb_frame_pacing_t *pacing) {
    *pacing = (gb_frame_pacing_t){.draw_interval = 2};
}

bool gb_frame_pacing_draw(const gb_frame_pacing_t *pacing) {
    return pacing->frames % pacing->draw_interval == 0;
}

void gb_frame_pacing_record(gb_frame_pacing_t *pacing, uint32_t work_us) {
    pacing->frames++;
    pacing->window_frames++;
    pacing->window_work_us += work_us;
    if (pacing->window_frames < 120) return;
    uint64_t average = pacing->window_work_us / pacing->window_frames;
    if (average > 20000 && pacing->draw_interval < 3) pacing->draw_interval++;
    else if (average < 15000 && pacing->draw_interval > 2) pacing->draw_interval--;
    pacing->window_frames = 0;
    pacing->window_work_us = 0;
}
