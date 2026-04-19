// Shared stubs for tests that link els_fsm.c but don't test hardware.
#include <stdint.h>
#include <stdbool.h>

// Safety stub
bool safety_estop_active(void) { return false; }

// Multicore FIFO stubs (extern-declared in pico/multicore.h)
bool     multicore_fifo_rvalid(void)          { return false; }
uint32_t multicore_fifo_pop_blocking(void)    { return 0; }

int32_t  spindle_update(void)                 { return 0; }
int32_t  spindle_read_count(void)             { return 0; }
float    spindle_read_rpm(void)               { return 0.0f; }
uint32_t spindle_read_rate_eps(void)          { return 1000; }
int8_t   spindle_direction(void)              { return 1; }
bool     spindle_index_latched(void)          { return false; }
void     spindle_index_latch_clear(void)      {}
bool     spindle_index_fault(void)            { return false; }
void     spindle_arm_start_offset(uint32_t p) { (void)p; }
void     spindle_init(void)                   {}
