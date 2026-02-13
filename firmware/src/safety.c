#include "safety.h"
#include "pins.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"

static volatile bool estop_triggered;
static volatile bool alarm_state;

// Button debounce state
#define DEBOUNCE_MS 20
typedef struct {
    uint gpio;
    bool state;         // debounced state (true = pressed, active low)
    bool raw_last;
    uint32_t last_change_ms;
} button_t;

static button_t btn_engage;
static button_t btn_feed_hold;
static button_t btn_cycle_start;

// LED blink
static uint32_t led_last_ms;
static bool led_on;

static void estop_isr(uint gpio, uint32_t events) {
    (void)gpio;
    (void)events;
    estop_triggered = true;
    alarm_state = true;
}

static void button_init(button_t *btn, uint gpio) {
    btn->gpio = gpio;
    btn->state = false;
    btn->raw_last = false;
    btn->last_change_ms = 0;
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_IN);
    gpio_pull_up(gpio);
}

void safety_init(void) {
    estop_triggered = false;
    alarm_state = false;

    // E-stop: GP14, active low with hardware pull-up
    gpio_init(PIN_ESTOP);
    gpio_set_dir(PIN_ESTOP, GPIO_IN);
    gpio_pull_up(PIN_ESTOP);
    gpio_set_irq_enabled_with_callback(PIN_ESTOP,
        GPIO_IRQ_EDGE_FALL, true, estop_isr);

    // Check if E-stop is already active at boot
    if (!gpio_get(PIN_ESTOP)) {
        estop_triggered = true;
        alarm_state = true;
    }

    // Physical buttons
    button_init(&btn_engage, PIN_ENGAGE);
    button_init(&btn_feed_hold, PIN_FEED_HOLD);
    button_init(&btn_cycle_start, PIN_CYCLE_START);

    // Watchdog: 500ms timeout
    watchdog_enable(500, true);

    led_last_ms = to_ms_since_boot(get_absolute_time());
    led_on = false;
}

void safety_watchdog_feed(void) {
    watchdog_update();
}

bool safety_estop_active(void) {
    // Also check current pin state (hardware may have changed)
    if (!gpio_get(PIN_ESTOP)) {
        estop_triggered = true;
        alarm_state = true;
    }
    return estop_triggered;
}

bool safety_alarm_active(void) {
    return alarm_state;
}

bool safety_alarm_clear(void) {
    // Only clear if E-stop is released (pin high = not active)
    if (gpio_get(PIN_ESTOP)) {
        estop_triggered = false;
        alarm_state = false;
        return true;
    }
    return false;
}

static void debounce_update(button_t *btn) {
    bool raw = !gpio_get(btn->gpio); // active low
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (raw != btn->raw_last) {
        btn->last_change_ms = now;
        btn->raw_last = raw;
    }
    if ((now - btn->last_change_ms) >= DEBOUNCE_MS) {
        btn->state = raw;
    }
}

void safety_debounce_update(void) {
    debounce_update(&btn_engage);
    debounce_update(&btn_feed_hold);
    debounce_update(&btn_cycle_start);
}

bool button_engage_pressed(void) {
    return btn_engage.state;
}

bool button_feed_hold_pressed(void) {
    return btn_feed_hold.state;
}

bool button_cycle_start_pressed(void) {
    return btn_cycle_start.state;
}

void safety_led_update(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    uint32_t interval = alarm_state ? 100 : 500; // fast blink in alarm, slow heartbeat

    if ((now - led_last_ms) >= interval) {
        led_on = !led_on;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
        led_last_ms = now;
    }
}
