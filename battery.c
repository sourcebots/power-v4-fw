#include <stdbool.h>

#include "battery.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/nvic.h>

#include "led.h"
#include "i2c.h"
#include "clock.h"

#define INA219_ADDR_BATT 0x40
#define INA219_ADDR_SMPS 0x41
#define INA219_REG_VSHUNT 1
#define INA219_REG_VBUS 2

void battery_init(void) {
	// We poll the battery current sense / voltage sense to see whether
	// something is wrong. Reads that are too close together cause the
	// INA219 to croak. Therefore set up a timer to periodically trigger
	// reads.
	// The IC settles readings at 2KHz; enable a timer @ 4Khz and read
	// voltage / current every other tick.
	rcc_periph_clock_enable(RCC_TIM2);
	timer_reset(TIM2);
	timer_set_prescaler(TIM2, 1799); // 72Mhz -> 40Khz
	timer_set_period(TIM2, 10); // 10 ticks -> 4Khz
	nvic_enable_irq(NVIC_TIM2_IRQ);
	nvic_set_priority(NVIC_TIM2_IRQ, 2); // Less important
	timer_enable_update_event(TIM2);
	timer_enable_irq(TIM2, TIM_DIER_UIE);
	timer_enable_counter(TIM2);
}

uint16_t battery_voltage(uint16_t sample)
{
	uint16_t vbus = sample;
	// Lower 3 bits are status bits. Rest is the voltage, measured in units
	// of 4mV. So, mask the lower 3 bits, then shift down by one.
	vbus &= 0xFFF8;
	vbus >>= 1;
	return vbus;
}

static uint32_t battery_current(uint16_t sample)
{
	uint16_t vshunt = sample;

	// The measurement just taken is measured in 10uV units over the 500uO
	// resistor pair on the battery rail. I = V/R, and R being small,
	// multiply by 20 to give a figure measured in units of 1mA.
	uint32_t current = vshunt * 20;

	// Additionally, the current drawn is consistently reported as being
	// 800mA over reality; adjust this amount.
	if (current < 800) {
		// It's also super jittery, and might dip below this offset
		current = 1;
	} else {
		current -= 800;
	}

	return current;
}

volatile bool batt_do_read = false;
uint32_t batt_read_current = 0;
uint32_t batt_read_voltage = 0;

// 2 states per source: pre (waiting for timer signal), wait (blocked on i2c)
enum { BATT_PRE_CURR, BATT_WAIT_CURR,
	BATT_PRE_VOLT, BATT_WAIT_VOLT }
batt_read_state = BATT_PRE_CURR;

volatile uint16_t read_sample;
volatile enum i2c_stat read_flag;

void tim2_isr(void)
{
	batt_do_read = true;

	clock_isr(); // Piggy-back the wall-clock time on this ticking

	TIM_SR(TIM2) = 0; // Inexplicably does not reset
	return;
}

static bool timer_triggered()
{
	bool tmp;
	nvic_disable_irq(NVIC_TIM2_IRQ);
	tmp = batt_do_read;
	nvic_enable_irq(NVIC_TIM2_IRQ);
	return tmp;
}

static void reset_battery_timer()
{
	nvic_disable_irq(NVIC_TIM2_IRQ);
	timer_set_counter(TIM2, 0);
	batt_do_read = false;
	nvic_enable_irq(NVIC_TIM2_IRQ);
}

void battery_poll()
{

	switch (batt_read_state) {
	case BATT_PRE_CURR:
		if (!timer_triggered())
			break;

		reset_battery_timer();
		// Initiate i2c mangling
		i2c_init_read(INA219_ADDR_BATT, INA219_REG_VSHUNT,
				&read_sample, &read_flag);
		// Spin in this state until we're handed an error or sample
		batt_read_state = BATT_WAIT_CURR;
	case BATT_WAIT_CURR:
		if (read_flag == I2C_STAT_NOTYET)
			break;

		if (!i2c_error_flag(read_flag)) {
			// Convert the read sample into a current
			batt_read_current = battery_current(read_sample);
		} else {
			batt_read_current = 0;
		}

		batt_read_state = BATT_PRE_VOLT;
		break;
	case BATT_PRE_VOLT:
		if (!timer_triggered())
			break;

		reset_battery_timer();
		// Initiate i2c mangling
		i2c_init_read(INA219_ADDR_BATT, INA219_REG_VBUS,
				&read_sample, &read_flag);
		// Spin in this state until we're handed an error or sample
		batt_read_state = BATT_WAIT_VOLT;
	case BATT_WAIT_VOLT:
		if (read_flag == I2C_STAT_NOTYET)
			break;

		// Convert the read sample into a voltage
		if (!i2c_error_flag(read_flag)) {
			batt_read_voltage = battery_voltage(read_sample);
		} else {
			batt_read_voltage = 0;
		}

		batt_read_state = BATT_PRE_CURR;
		break;
	}

	return;
}

// Upon read, return the most recently sampled result.
uint16_t read_battery_voltage()
{
	return batt_read_voltage;
}

uint32_t read_battery_current()
{
	return batt_read_current;
}
