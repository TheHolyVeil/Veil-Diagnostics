#ifndef RAM_TEST_H
#define RAM_TEST_H

#include "efi_types.h"

typedef struct {
    UINT32 total_mb;
    UINT32 tested_mb;
    UINT32 total_steps;
    UINT32 current_step;
    UINT32 current_pattern_idx;
    char pattern_name[32];
    UINT32 errors_found;
    float progress_pct;
    UINT64 elapsed_ms;
    UINT32 eta_seconds;
    float speed_mbps;
    BOOLEAN is_complete;
    BOOLEAN is_running;
    BOOLEAN is_passed;
    void *buffer_ptr;
    UINTN buffer_words;
    UINTN chunk_words;
    UINTN current_chunk_idx;
    UINT64 start_tsc;
    UINT64 rng_seed;
} RamTestState;

/* Exported C functions from src/ram_test.zig */
void ram_test_init(RamTestState *state, void *mem_ptr, UINTN size_bytes);
void ram_test_step(RamTestState *state);
void ram_test_reset(RamTestState *state);

#endif /* RAM_TEST_H */
