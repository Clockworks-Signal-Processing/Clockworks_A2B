/*******************************************************************************
 * pal_gpio.h — Linux GPIO PAL for A2B Stack (public interface)
 * ClockWorks Signal Processing LLC
 ******************************************************************************/

#ifndef PAL_GPIO_H_
#define PAL_GPIO_H_

#include "a2b/ctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pin value meaning "not connected / not used". */
#define A2B_GPIO_PIN_UNASSIGNED   (-1)

/*
 * GPIO configuration.  Pins are line offsets on chip_path (BCM GPIO
 * numbers on a Raspberry Pi); A2B_GPIO_PIN_UNASSIGNED skips that line.
 */
typedef struct {
    const char *chip_path;     /* e.g. "/dev/gpiochip0" (not copied) */
    int         reset_n_pin;   /* AD2437 RESETn output               */
    int         irq_pin;       /* AD2437 IRQ input                   */
} a2b_pal_gpio_cfg_t;

/*
 * Fill cfg with the compile-time defaults from pal_gpio.c.
 */
void a2b_pal_gpio_get_defaults(a2b_pal_gpio_cfg_t *cfg);

/*
 * Initialise the GPIO subsystem.
 * Opens cfg->chip_path and requests whichever lines are assigned: RESETn
 * as output (deasserted high), IRQ as input.  With no pins assigned the
 * chip is not opened (I2C-only mode).  cfg may be NULL for the defaults.
 * Must be called once before any other pal_gpio function.
 * Returns 0 on success, -1 on error.
 */
int a2b_pal_gpio_init(const a2b_pal_gpio_cfg_t *cfg);

/*
 * Assert AD2437 RESETn low, hold for 100 ms, then release high.
 * Blocks the caller for the hold period using nanosleep().
 * Call once at startup before a2b_palInit() to ensure the AD2437 starts
 * from a known state.
 */
void a2b_pal_gpio_reset_ad2437(void);

/*
 * Wait up to timeout_ms milliseconds for a falling edge on the IRQ line.
 * Returns  1 if an event was detected,
 *          0 if the timeout expired with no event,
 *         -1 on error.
 * Used by the main loop when ENABLE_INTERRUPT_PROCESS is not defined
 * (polled mode).  When interrupt-driven mode is enabled in future, the
 * main loop will call adi_a2b_EnablePinInterrupt() instead.
 */
int a2b_pal_gpio_wait_irq(int timeout_ms);

/*
 * Release all GPIO lines and close the chip handle.
 * Call on orderly shutdown.
 */
void a2b_pal_gpio_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PAL_GPIO_H_ */
