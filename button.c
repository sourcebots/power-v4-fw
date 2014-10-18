#include <stdbool.h>

#include "button.h"
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#define INT_PORT GPIOC
#define INT_PIN GPIO14
#define EXT_PORT GPIOC
#define EXT_PIN GPIO15

#define delay(x) do { for (int i = 0; i < x * 1000; i++) \
                          __asm__("nop"); \
                    } while(0)


static uint32_t debounce_int, debounce_ext;

void button_init(void) {
	gpio_set(INT_PORT, INT_PIN); // Pull-up
	gpio_set_mode(INT_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, INT_PIN);
	gpio_set(EXT_PORT, EXT_PIN); // Pull-up
	gpio_set_mode(EXT_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, EXT_PIN);
}

bool button_int_read(void) { return gpio_get(INT_PORT, INT_PIN); }
bool button_ext_read(void) { return gpio_get(EXT_PORT, EXT_PIN); }


bool force_bootloader() __attribute__((section(".bootloader")));

bool force_bootloader()
{
	// Function to check whether we should force the bootloader.
	// Specifically, do that if the start button is pressed on startup
	rcc_periph_clock_enable(RCC_GPIOC);
	gpio_set(INT_PORT, INT_PIN); // Pull-up
	gpio_set_mode(INT_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, INT_PIN);
	gpio_set(EXT_PORT, EXT_PIN); // Pull-up
	gpio_set_mode(EXT_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, EXT_PIN);

	// Rise time
	delay(1);

	if (!button_int_read() || !button_ext_read())
		return true;
	return false;
}

void button_poll()
{
	uint32_t internal, external;

	// Button inputs are active low, so invert their value
	internal = (button_int_read()) ? 0 : 1;
	external = (button_ext_read()) ? 0 : 1;

	debounce_int <<= 1;
	debounce_ext <<= 1;

	debounce_int |= internal;
	debounce_ext |= external;

	return;
}

bool button_pressed(void)
{
	if (debounce_int == 0xFFFFFFFF || debounce_ext == 0xFFFFFFFF)
		return true;
	else
		return false;
}
