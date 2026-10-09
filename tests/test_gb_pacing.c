#include <assert.h>
#include "gb_audio_pacing.h"
#include "gb_frame_pacing.h"

int main(void) {
    assert(gb_audio_blocks_due(16687, 0) == 1);
    assert(gb_audio_blocks_due(33333, 0) == 2);
    assert(gb_audio_blocks_due(50000, 0) == 3);
    assert(gb_audio_blocks_due(100000, 0) == 4);
    assert(gb_audio_blocks_due(50000, 3) == 0);
    assert(gb_audio_resampled_count(1) == 267);
    assert(gb_audio_resampled_count(2) == 267);
    for (unsigned fps = 20; fps <= 60; fps += 10) {
        uint64_t period_us = 1000000u / fps;
        uint64_t blocks = 0;
        for (unsigned frame = 1; frame <= 4; frame++) {
            uint64_t elapsed = frame * period_us;
            blocks += gb_audio_blocks_due(elapsed, blocks);
        }
        assert(blocks >= (uint64_t)(4u * 60u / fps) - 1u);
    }

    gb_frame_pacing_t pacing;
    gb_frame_pacing_init(&pacing);
    assert(gb_frame_pacing_draw(&pacing));
    gb_frame_pacing_mark_drawn(&pacing);
    gb_frame_pacing_record(&pacing, 0);
    assert(!gb_frame_pacing_draw(&pacing));
    gb_frame_pacing_record(&pacing, 0);
    assert(gb_frame_pacing_draw(&pacing));
    // A busy LCD misses the due frame; the next frame must still be due.
    gb_frame_pacing_record(&pacing, 0);
    assert(gb_frame_pacing_draw(&pacing));
    gb_frame_pacing_mark_drawn(&pacing);
    assert(!gb_frame_pacing_draw(&pacing));
    pacing.frames = UINT32_MAX;
    gb_frame_pacing_mark_drawn(&pacing);
    gb_frame_pacing_record(&pacing, 0);
    assert(!gb_frame_pacing_draw(&pacing));
    gb_frame_pacing_record(&pacing, 0);
    assert(gb_frame_pacing_draw(&pacing));
    gb_frame_pacing_init(&pacing);
    // A workload below the old 20 ms threshold still misses the GB deadline.
    for (int i = 0; i < 60; i++) gb_frame_pacing_record(&pacing, 18000);
    assert(pacing.draw_interval == 3);
    for (int i = 0; i < 60; i++) gb_frame_pacing_record(&pacing, 18000);
    assert(pacing.draw_interval == 4);
    for (int i = 0; i < 60; i++) gb_frame_pacing_record(&pacing, 24000);
    assert(pacing.draw_interval == 4);
    for (int i = 0; i < 60; i++) gb_frame_pacing_record(&pacing, 15000);
    assert(pacing.draw_interval == 4);
    for (int i = 0; i < 60; i++) gb_frame_pacing_record(&pacing, 13000);
    assert(pacing.draw_interval == 3);
    for (int i = 0; i < 60; i++) gb_frame_pacing_record(&pacing, 13000);
    assert(pacing.draw_interval == 2);
    return 0;
}
