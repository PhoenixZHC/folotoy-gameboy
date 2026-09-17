#include "gb_audio_pacing.h"

size_t gb_audio_blocks_due(uint64_t elapsed_us, uint64_t generated_blocks) {
    uint64_t source_samples = elapsed_us * GB_AUDIO_SOURCE_RATE / 1000000u;
    uint64_t due = (source_samples + GB_AUDIO_SOURCE_SAMPLES - 1u) /
                   GB_AUDIO_SOURCE_SAMPLES;
    if (due <= generated_blocks) return 0;
    uint64_t needed = due - generated_blocks;
    return (size_t)(needed > 4u ? 4u : needed);
}

size_t gb_audio_resampled_count(uint64_t generated_blocks) {
    uint64_t total = generated_blocks * GB_AUDIO_SOURCE_SAMPLES *
                     GB_AUDIO_OUTPUT_RATE / GB_AUDIO_SOURCE_RATE;
    uint64_t previous = (generated_blocks - 1u) * GB_AUDIO_SOURCE_SAMPLES *
                        GB_AUDIO_OUTPUT_RATE / GB_AUDIO_SOURCE_RATE;
    return (size_t)(total - previous);
}
