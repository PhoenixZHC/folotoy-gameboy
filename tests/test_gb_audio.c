#include <assert.h>
#include <stdbool.h>
#include "minigb_apu.h"
#include "../main/gb_audio_pacing.h"

_Static_assert(AUDIO_SAMPLE_RATE == GB_AUDIO_SOURCE_RATE, "APU/source rate mismatch");
_Static_assert(AUDIO_SAMPLES == GB_AUDIO_SOURCE_SAMPLES, "APU/block size mismatch");

int main(void) {
    struct minigb_apu_ctx apu;
    minigb_apu_audio_init(&apu);
    minigb_apu_audio_write(&apu, 0xff26, 0x80); // power
    minigb_apu_audio_write(&apu, 0xff24, 0x77); // master volume
    minigb_apu_audio_write(&apu, 0xff25, 0x22); // channel 2 to both outputs
    minigb_apu_audio_write(&apu, 0xff16, 0x80); // duty
    minigb_apu_audio_write(&apu, 0xff17, 0xf0); // envelope
    minigb_apu_audio_write(&apu, 0xff18, 0x40); // frequency low
    minigb_apu_audio_write(&apu, 0xff19, 0x87); // trigger
    audio_sample_t samples[AUDIO_SAMPLES_TOTAL];
    minigb_apu_audio_callback(&apu, samples);
    bool nonzero = false;
    for (unsigned i = 0; i < AUDIO_SAMPLES_TOTAL; i++)
        if (samples[i]) nonzero = true;
    assert(nonzero);
    // Verify the programmed tone stays near 131072 / (2048 - 0x740) Hz
    // over continuous callback blocks, including their boundaries.
    unsigned rising = 0;
    int previous = samples[AUDIO_SAMPLES_TOTAL-2];
    for (unsigned block=0; block<60; block++) {
        minigb_apu_audio_callback(&apu,samples);
        for(unsigned i=0;i<AUDIO_SAMPLES_TOTAL;i+=2) {
            if(previous <= 0 && samples[i] > 0) rising++;
            previous=samples[i];
        }
    }
    double expected = (131072.0 / 192.0) * (60.0 * AUDIO_SAMPLES / AUDIO_SAMPLE_RATE);
    assert(rising > expected-3 && rising < expected+3);
    return 0;
}
