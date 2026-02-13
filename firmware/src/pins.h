#ifndef PINS_H
#define PINS_H

// Spindle encoder (PIO 0, SM 0)
#define PIN_SPINDLE_A       2
#define PIN_SPINDLE_B       3
#define PIN_SPINDLE_INDEX   4

// X-axis scale (PIO 0, SM 1)
#define PIN_X_SCALE_A       5
#define PIN_X_SCALE_B       6
#define PIN_X_INDEX         7   // reserved

// Z stepper (PIO 1, SM 0)
#define PIN_Z_STEP          8
#define PIN_Z_DIR           9
#define PIN_Z_ENABLE        10

// Future X stepper (PIO 1, SM 1)
#define PIN_X_STEP          11  // reserved
#define PIN_X_DIR           12  // reserved
#define PIN_X_ENABLE        13  // reserved

// Controls
#define PIN_ESTOP           14
#define PIN_ENGAGE          15
#define PIN_FEED_HOLD       16
#define PIN_CYCLE_START     17

// Z-axis scale (PIO 0, SM 2)
#define PIN_Z_SCALE_A       20
#define PIN_Z_SCALE_B       21

// Reserved
#define PIN_JOG_FWD         18
#define PIN_JOG_REV         19
#define PIN_FEED_OVERRIDE   22

// ADC (reserved)
#define PIN_ADC0            26
#define PIN_ADC1_FEED_OVR   27
#define PIN_ADC2            28

// Onboard LED is via CYW43 WL_GPIO0, not a direct GPIO pin

#endif // PINS_H
