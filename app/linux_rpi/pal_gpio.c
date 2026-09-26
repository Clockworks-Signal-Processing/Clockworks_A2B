/*******************************************************************************
 * pal_gpio.c — Linux GPIO PAL for A2B Stack
 * ClockWorks Signal Processing LLC
 *
 * Linux / Raspberry Pi (64-bit Raspberry Pi OS) implementation of the GPIO
 * PAL functions.  Replaces the Pico SDK gpio_init/gpio_put/gpio_set_dir calls
 * in app/rp2040/a2bapp_rp2040.c and app/rp2040/a2bstack-pal/adi_a2b_irq.c.
 *
 * API:
 *   a2b_pal_gpio_get_defaults() — compile-time defaults (chip, pins)
 *   a2b_pal_gpio_init()         — open chip, request lines (call at startup)
 *   a2b_pal_gpio_reset_ad2437() — pulse RESETn low for 100 ms
 *   a2b_pal_gpio_wait_irq()     — wait for a falling edge on IRQ
 *   a2b_pal_gpio_shutdown()     — release lines and close chip
 *   adi_a2b_EnablePinInterrupt()— interrupt registration (Stage 1 stub)
 *
 * Library: libgpiod v2 API (libgpiod 2.x, Raspberry Pi OS trixie and later).
 * Do NOT use sysfs (/sys/class/gpio), pigpio, wiringPi, or bcm2835.
 *
 * GPIO chip: /dev/gpiochip0 by default (the BCM GPIO controller on Pi Zero
 * 2W).  The chip and both pins can be overridden at runtime (main.c -c, -r,
 * -i), so other boards don't need a rebuild.
 *
 * Pin number note
 * ---------------
 * RESET_N_GPIO_PIN and IRQ_GPIO_PIN below match the reference board,
 * rev A.  Do NOT hardcode pin numbers anywhere else — change
 * only the two #defines below (or pass them at runtime with -r / -i).
 * A pin set to A2B_GPIO_PIN_UNASSIGNED is not used; each line is
 * optional on its own.
 ******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <gpiod.h>

#include "a2b/ctypes.h"
#include "a2b/error.h"
#include "adi_a2b_datatypes.h"
#include "pal_gpio.h"

/*---------------------------------------------------------------------------
 * GPIO pin assignments — BCM GPIO numbers on the Raspberry Pi
 *
 * From the reference board's rev A netlist.  Verify with
 * `gpioinfo -c gpiochip0`.
 *---------------------------------------------------------------------------*/

/* AD2437 RESETn (U7 pin 25) — NOT connected to the Pi on rev A: it goes only
 * to header J7 pin 6, with a 10k pull-up (R39).  Software can't reset the
 * AD2437; power-cycle it before each run.                                  */
#define RESET_N_GPIO_PIN    A2B_GPIO_PIN_UNASSIGNED

/* AD2437 IRQ (U7 pin 24) — Pi header pin 33 = BCM GPIO13.
 * Only used with ENABLE_INTERRUPT_PROCESS (off: the stack polls over I2C).
 * The line is requested for falling edges, i.e. assumes an active-low IRQ;
 * the AD2437's IRQ polarity is configurable (PINCFG.IRQINV), so check the
 * bus configuration before enabling interrupt mode.                        */
#define IRQ_GPIO_PIN        13

/*---------------------------------------------------------------------------
 * GPIO chip device
 * /dev/gpiochip0 is the BCM GPIO controller on the Pi Zero 2W, 3 and 4.
 * Other boards (e.g. Pi 5 on some kernels) differ.  Verify: gpiodetect
 *---------------------------------------------------------------------------*/
#define GPIO_CHIP_DEVICE    "/dev/gpiochip0"

/* Consumer string shown by gpioinfo for our lines */
#define GPIO_CONSUMER       "a2b-pnp"

/*---------------------------------------------------------------------------
 * Module state
 *---------------------------------------------------------------------------*/
static a2b_pal_gpio_cfg_t s_cfg = {
    GPIO_CHIP_DEVICE, RESET_N_GPIO_PIN, IRQ_GPIO_PIN
};

static struct gpiod_chip         *s_chip      = NULL;
static struct gpiod_line_request *s_reset_req = NULL;   /* RESETn output      */
static struct gpiod_line_request *s_irq_req   = NULL;   /* IRQ falling edges  */
static struct gpiod_edge_event_buffer *s_irq_events = NULL;

/*---------------------------------------------------------------------------
 * gpio_hint
 *
 * Prints a likely fix for a failed libgpiod call, based on errno.
 *---------------------------------------------------------------------------*/
static void gpio_hint(int err)
{
    switch (err) {
    case EACCES:
    case EPERM:
        fprintf(stderr,
            "  Add your user to the gpio group and log in again:\n"
            "    sudo usermod -aG gpio $USER\n"
            "  or run with sudo.\n");
        break;
    case ENOENT:
    case ENODEV:
        fprintf(stderr,
            "  No such GPIO chip.  List chips with: gpiodetect   "
            "then pass -c <chip>.\n");
        break;
    case EBUSY:
        fprintf(stderr,
            "  Line already in use by another program or driver.  "
            "Check with: gpioinfo\n");
        break;
    case EINVAL:
        fprintf(stderr,
            "  Pin number out of range for this chip.  Check with: gpioinfo\n");
        break;
    default:
        break;
    }
}

/*---------------------------------------------------------------------------
 * gpio_print_owner
 *
 * For a busy line, prints which consumer (program or driver) holds it.
 *---------------------------------------------------------------------------*/
static void gpio_print_owner(unsigned int offset)
{
    struct gpiod_line_info *info = gpiod_chip_get_line_info(s_chip, offset);
    const char *consumer;

    if (!info) {
        return;
    }
    if (gpiod_line_info_is_used(info)) {
        consumer = gpiod_line_info_get_consumer(info);
        fprintf(stderr, "  GPIO%u is held by \"%s\".\n",
                offset, (consumer && consumer[0]) ? consumer : "(unnamed)");
    }
    gpiod_line_info_free(info);
}

/*---------------------------------------------------------------------------
 * request_line
 *
 * Requests one line on s_chip with the given direction.  For an output,
 * out_high sets the initial level; for an input, falling_edge enables
 * falling-edge event detection.  Returns the request, or NULL with errno set.
 *---------------------------------------------------------------------------*/
static struct gpiod_line_request *request_line(unsigned int offset,
                                               int is_output,
                                               int out_high,
                                               int falling_edge)
{
    struct gpiod_line_settings  *settings = NULL;
    struct gpiod_line_config    *line_cfg = NULL;
    struct gpiod_request_config *req_cfg  = NULL;
    struct gpiod_line_request   *request  = NULL;
    int err = 0;

    settings = gpiod_line_settings_new();
    line_cfg = gpiod_line_config_new();
    req_cfg  = gpiod_request_config_new();
    if (!settings || !line_cfg || !req_cfg) {
        err = ENOMEM;
        goto out;
    }

    if (is_output) {
        gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
        gpiod_line_settings_set_output_value(settings,
            out_high ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
    } else {
        gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
        if (falling_edge) {
            gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_FALLING);
        }
    }

    if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings) < 0) {
        err = errno;
        goto out;
    }
    gpiod_request_config_set_consumer(req_cfg, GPIO_CONSUMER);

    request = gpiod_chip_request_lines(s_chip, req_cfg, line_cfg);
    if (!request) {
        err = errno;
    }

out:
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    errno = err;
    return request;
}

/*===========================================================================
 * a2b_pal_gpio_get_defaults
 *
 * Fills cfg with the compile-time defaults (GPIO_CHIP_DEVICE,
 * RESET_N_GPIO_PIN, IRQ_GPIO_PIN), so callers only override what they need.
 *===========================================================================*/
void a2b_pal_gpio_get_defaults(a2b_pal_gpio_cfg_t *cfg)
{
    cfg->chip_path   = GPIO_CHIP_DEVICE;
    cfg->reset_n_pin = RESET_N_GPIO_PIN;
    cfg->irq_pin     = IRQ_GPIO_PIN;
}

/*===========================================================================
 * a2b_pal_gpio_init
 *
 * Opens the GPIO chip and requests whichever lines are assigned:
 *   reset_n_pin as output, initially high (deasserted — AD2437 running)
 *   irq_pin     as input with falling-edge detection
 * A pin set to A2B_GPIO_PIN_UNASSIGNED is skipped.  If neither is
 * assigned the chip is not opened at all (I2C-only mode).
 *
 * cfg may be NULL to use the compile-time defaults.
 *
 * Call once at application startup before a2b_pal_gpio_reset_ad2437() or
 * any IRQ operation.
 *
 * Returns 0 on success, -1 on error.
 *===========================================================================*/
int a2b_pal_gpio_init(const a2b_pal_gpio_cfg_t *cfg)
{
    if (cfg) {
        s_cfg = *cfg;
    }
    if (!s_cfg.chip_path || s_cfg.chip_path[0] == '\0') {
        s_cfg.chip_path = GPIO_CHIP_DEVICE;
    }

    if (s_cfg.reset_n_pin < 0 && s_cfg.irq_pin < 0) {
        fprintf(stderr,
            "pal_gpio: no RESETn/IRQ pins assigned — running without them "
            "(I2C-only mode).\n"
            "  Assign with -r <pin> / -i <pin>, or RESET_N_GPIO_PIN / "
            "IRQ_GPIO_PIN in pal_gpio.c.\n");
        return 0;
    }

    s_chip = gpiod_chip_open(s_cfg.chip_path);
    if (!s_chip) {
        int err = errno;
        fprintf(stderr, "pal_gpio: gpiod_chip_open(%s) failed: %s\n",
                s_cfg.chip_path, strerror(err));
        gpio_hint(err);
        return -1;
    }

    /* RESETn: output, initially high (not in reset) */
    if (s_cfg.reset_n_pin >= 0) {
        s_reset_req = request_line((unsigned int)s_cfg.reset_n_pin, 1, 1, 0);
        if (!s_reset_req) {
            int err = errno;
            fprintf(stderr,
                "pal_gpio: cannot use RESETn pin %d on %s as output: %s\n",
                s_cfg.reset_n_pin, s_cfg.chip_path, strerror(err));
            gpio_hint(err);
            goto err_close;
        }
    }

    /* IRQ: input, falling-edge events (see the polarity note at IRQ_GPIO_PIN) */
    if (s_cfg.irq_pin >= 0) {
        int err = 0;

        s_irq_req = request_line((unsigned int)s_cfg.irq_pin, 0, 0, 1);
        if (!s_irq_req) {
            err = errno;
        } else {
            s_irq_events = gpiod_edge_event_buffer_new(1);
            if (!s_irq_events) {
                err = ENOMEM;
            }
        }
        if (err) {
            fprintf(stderr,
                "pal_gpio: cannot use IRQ pin %d on %s as input: %s\n",
                s_cfg.irq_pin, s_cfg.chip_path, strerror(err));
            if (err == EBUSY) {
                gpio_print_owner((unsigned int)s_cfg.irq_pin);
            }
            gpio_hint(err);
#ifdef ENABLE_INTERRUPT_PROCESS
            goto err_release;
#else
            /* Polled mode doesn't use the IRQ line, so carry on without it. */
            if (s_irq_req) {
                gpiod_line_request_release(s_irq_req);
                s_irq_req = NULL;
            }
            fprintf(stderr,
                "  Continuing without IRQ: not needed while the stack polls the "
                "AD2437 over I2C.\n");
#endif
        }
    }

    return 0;

#ifdef ENABLE_INTERRUPT_PROCESS
err_release:    /* only an IRQ failure in interrupt mode is fatal */
    if (s_irq_req) {
        gpiod_line_request_release(s_irq_req);
        s_irq_req = NULL;
    }
    gpiod_edge_event_buffer_free(s_irq_events);
    s_irq_events = NULL;
    if (s_reset_req) {
        gpiod_line_request_release(s_reset_req);
        s_reset_req = NULL;
    }
#endif
err_close:
    gpiod_chip_close(s_chip);
    s_chip = NULL;
    return -1;
}

/*===========================================================================
 * a2b_pal_gpio_reset_ad2437
 *
 * Asserts AD2437 RESETn low for 100 ms then releases it high.
 * Blocks the caller for 100 ms using nanosleep().
 *
 * Call once at startup (after a2b_pal_gpio_init(), before a2b_palInit())
 * to bring the AD2437 up from a known state.
 * Can also be called to trigger re-discovery on a bus fault.
 *
 * Pico equivalent (a2bapp_rp2040.c):
 *   gpio_put(AD2437_RESET_n, false);
 *   sleep_ms(100);
 *   gpio_put(AD2437_RESET_n, true);
 *===========================================================================*/
void a2b_pal_gpio_reset_ad2437(void)
{
    struct timespec hold = { 0, 100000000L };   /* 100 ms */
    unsigned int offset = (unsigned int)s_cfg.reset_n_pin;

    if (!s_reset_req) {
        /* No RESETn pin assigned — no hardware reset available, continuing */
        return;
    }

    /* Assert reset — drive low */
    if (gpiod_line_request_set_value(s_reset_req, offset,
                                     GPIOD_LINE_VALUE_INACTIVE) < 0) {
        fprintf(stderr, "pal_gpio: assert RESETn failed: %s\n",
                strerror(errno));
        return;
    }

    nanosleep(&hold, NULL);

    /* Release reset — drive high */
    if (gpiod_line_request_set_value(s_reset_req, offset,
                                     GPIOD_LINE_VALUE_ACTIVE) < 0) {
        fprintf(stderr, "pal_gpio: release RESETn failed: %s\n",
                strerror(errno));
    }
}

/*===========================================================================
 * a2b_pal_gpio_wait_irq
 *
 * Waits up to timeout_ms milliseconds for a falling edge on the IRQ line,
 * and consumes it.
 *
 * Returns:
 *   1  — falling edge detected (AD2437 has a pending interrupt)
 *   0  — timeout expired, no event
 *  -1  — error, or no IRQ pin assigned
 *===========================================================================*/
int a2b_pal_gpio_wait_irq(int timeout_ms)
{
    int ret;

    if (!s_irq_req) {
        fprintf(stderr, "pal_gpio: wait_irq called with no IRQ line\n");
        return -1;
    }

    ret = gpiod_line_request_wait_edge_events(s_irq_req,
                                              (int64_t)timeout_ms * 1000000LL);
    if (ret < 0) {
        fprintf(stderr, "pal_gpio: gpiod_line_request_wait_edge_events failed: %s\n",
                strerror(errno));
        return -1;
    }
    if (ret > 0) {
        /* Consume the event so the next wait blocks until a new edge */
        if (gpiod_line_request_read_edge_events(s_irq_req, s_irq_events, 1) < 0) {
            fprintf(stderr, "pal_gpio: gpiod_line_request_read_edge_events failed: %s\n",
                    strerror(errno));
            return -1;
        }
        return 1;
    }
    return 0;
}

/*===========================================================================
 * a2b_pal_gpio_shutdown
 *
 * Releases all GPIO lines and closes the chip handle.
 * Call on orderly application shutdown.
 *===========================================================================*/
void a2b_pal_gpio_shutdown(void)
{
    if (s_reset_req) {
        /* Drive RESETn high (deasserted) before releasing */
        (void)gpiod_line_request_set_value(s_reset_req,
                                           (unsigned int)s_cfg.reset_n_pin,
                                           GPIOD_LINE_VALUE_ACTIVE);
        gpiod_line_request_release(s_reset_req);
        s_reset_req = NULL;
    }
    if (s_irq_req) {
        gpiod_line_request_release(s_irq_req);
        s_irq_req = NULL;
    }
    gpiod_edge_event_buffer_free(s_irq_events);
    s_irq_events = NULL;
    if (s_chip) {
        gpiod_chip_close(s_chip);
        s_chip = NULL;
    }
}

/*===========================================================================
 * adi_a2b_EnablePinInterrupt
 *
 * Registers the stack's interrupt callback for AD2437 IRQ events.
 * Called by a2bpnp_local.c when ENABLE_INTERRUPT_PROCESS is defined.
 *
 * Stage 1 (ENABLE_INTERRUPT_PROCESS not defined, the current build): not
 * called by the stack, which polls the AD2437 over I2C instead.  Provided
 * for link compatibility.
 *
 * Stage 2 (ENABLE_INTERRUPT_PROCESS defined): the IRQ line is already
 * requested with falling-edge detection by a2b_pal_gpio_init(); this stores
 * the callback, and the main loop (or a dedicated thread) must call
 * a2b_pal_gpio_wait_irq() and invoke it on each event.
 *
 * @param nGPIONum        Logical IRQ channel (value 4 in a2bpnp_local.c;
 *                        mapped to the configured IRQ pin in this port).
 * @param pUserCallBack   Stack interrupt callback (a2b_IntrptCallbk).
 * @param CallBackParam   Opaque parameter passed to the callback
 *                        (a2b_App_t* instance) — pointer-sized.
 * @param bFallingEdgeTrig 0 = falling edge, non-zero = rising edge.
 *                         AD2437 IRQ is active-low so falling edge is used.
 *
 * Returns 0 on success, 1 on failure.
 *===========================================================================*/

#ifdef ENABLE_INTERRUPT_PROCESS
typedef void (*irq_callback_t)(a2b_UInt32, void *);
static irq_callback_t s_irq_callback   = NULL;
static void          *s_irq_cb_param   = NULL;
#endif /* ENABLE_INTERRUPT_PROCESS */

ADI_MEM_A2B_CODE_CRIT
a2b_UInt32 adi_a2b_EnablePinInterrupt(a2b_UInt8   nGPIONum,
                                      void       *pUserCallBack,
                                      a2b_UIntPtr CallBackParam,
                                      a2b_UInt8   bFallingEdgeTrig)
{
    A2B_UNUSED(nGPIONum);          /* mapped to s_cfg.irq_pin in this port */
    A2B_UNUSED(bFallingEdgeTrig);  /* AD2437 IRQ is always falling edge    */

#ifdef ENABLE_INTERRUPT_PROCESS
    if (!s_irq_req) {
        fprintf(stderr,
            "pal_gpio: EnablePinInterrupt needs an IRQ pin (-i <pin>)\n");
        return 1u;
    }
    s_irq_callback = (irq_callback_t)pUserCallBack;
    s_irq_cb_param = (void *)CallBackParam;
    return 0u;
#else
    A2B_UNUSED(pUserCallBack);
    A2B_UNUSED(CallBackParam);
    return 0u;
#endif /* ENABLE_INTERRUPT_PROCESS */
}
