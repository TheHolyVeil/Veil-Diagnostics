#ifndef SMART_TEST_H
#define SMART_TEST_H

#include "efi_types.h"

#define MAX_SMART_DRIVES 8
#define MAX_SMART_ATTRIBUTES 12

/* Command IDs for driver API command dispatches */
#define SMART_CMD_NONE           0
#define SMART_CMD_REFRESH        1
#define SMART_CMD_SHORT_TEST     2
#define SMART_CMD_EXTENDED_TEST  3
#define SMART_CMD_NEXT_DRIVE     4
#define SMART_CMD_ABORT          5

typedef struct {
    UINT8 id;
    char name[32];
    UINT8 current_val;
    UINT8 worst_val;
    UINT8 threshold;
    UINT64 raw_val;
    UINT8 is_ok; /* 1 = OK/Passed, 0 = Warning/Failure */
} SmartAttribute;

typedef struct {
    char model[40];
    char serial[24];
    UINT64 total_capacity_mb;
    UINT32 block_size;
    UINT64 total_blocks;
    UINT8 is_present;
    UINT8 is_removable;
    UINT8 is_read_only;
    UINT8 drive_type; /* 0 = SATA/AHCI, 1 = NVMe Express, 2 = Block I/O */
    UINT8 overall_health; /* 0 = UNKNOWN, 1 = PASSED, 2 = WARNING, 3 = FAILED */
    UINT8 attr_count;
    SmartAttribute attributes[MAX_SMART_ATTRIBUTES];
    EFI_HANDLE handle;
    EFI_BLOCK_IO_PROTOCOL *bio;
} SmartDriveInfo;

typedef struct {
    SmartDriveInfo drives[MAX_SMART_DRIVES];
    UINT32 drive_count;
    UINT32 active_drive_idx;
    
    UINT32 current_command;
    BOOLEAN is_running;
    BOOLEAN is_complete;
    BOOLEAN is_passed;
    
    UINT32 total_steps;
    UINT32 current_step;
    float progress_pct;
    UINT64 elapsed_ms;
    UINT32 eta_seconds;
    float read_speed_mbps;
    UINT32 latency_us;
    UINT32 read_errors;
    UINT64 current_lba;
    UINT64 sectors_scanned;
    
    char status_msg[64];
    UINT64 start_tsc;
    UINT8 test_buffer[65536];
} SmartTestState;

/* Exported C functions from src/smart_test.zig */
void smart_test_init(SmartTestState *state, EFI_BOOT_SERVICES *bs);
void smart_test_step(SmartTestState *state, EFI_BOOT_SERVICES *bs);
void smart_test_send_command(SmartTestState *state, UINT32 cmd_id);
void smart_test_reset(SmartTestState *state);

#endif /* SMART_TEST_H */
