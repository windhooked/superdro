// Stub axis functions for tests that link protocol.c but not encoder.c.
#include <stdint.h>
#include <stdbool.h>

void    x_axis_zero(void)         {}
void    z_axis_zero(void)         {}
void    x_axis_preset(float v)    { (void)v; }
void    z_axis_preset(float v)    { (void)v; }
int32_t x_axis_read(void)         { return 0; }
int32_t z_axis_read(void)         { return 0; }
