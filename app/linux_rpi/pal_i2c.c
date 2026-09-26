/*******************************************************************************
 * pal_i2c.c — Linux I2C PAL for A2B Stack
 * ClockWorks Signal Processing LLC
 *
 * Linux/Raspberry Pi Zero 2W peer implementation of the PAL I2C functions.
 * Replaces the Pico SDK calls in app/RP2040/a2bstack-pal/adi_a2b_pal.c.
 *
 * PAL function signatures read from:
 *   docs/AE_09_A2B_Stack_UserGuide.pdf, Table 3 (p.18–21, Rev 11.0)
 *   app/RP2040/a2bstack-pal/pal_i2c.h
 *   app/RP2040/a2bstack-pal/adi_a2b_pal.c
 *
 * Key implementation constraints (see CLAUDE.md for rationale):
 *
 *   a2b_pal_I2cWriteReadFunc — single ioctl(I2C_RDWR) with TWO i2c_msg
 *     structs.  The first is the write phase (register address); the second
 *     has I2C_M_RD set (read phase).  This is a single atomic repeated-start
 *     transaction.  Two separate ioctl calls are NOT equivalent and will fail
 *     register reads on the AD2437.
 *
 *   a2b_pal_I2cReadFunc — single ioctl(I2C_RDWR) with ONE i2c_msg, I2C_M_RD
 *     set.  Not a write followed by a read.
 *
 *   a2b_pal_I2cWriteFunc — single ioctl(I2C_RDWR) with ONE write i2c_msg.
 *
 * I2C bus: /dev/i2c-1 by default (GPIO2=SDA, GPIO3=SCL, hardware I2C on
 * Pi Zero 2W); override at runtime with a2b_pal_i2c_set_device() (main.c -d).
 * Requires dtparam=i2c_arm=on and dtparam=i2c_arm_baudrate=400000 in
 * config.txt (/boot/firmware/config.txt on Bookworm and later,
 * /boot/config.txt on older releases).  Verify: ls /dev/i2c-* and i2cdetect -l.
 *
 * Do NOT use libi2c, wiringPi, or any other I2C wrapper.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include "a2b/ctypes.h"
#include "a2b/error.h"
#include "a2b/i2c.h"
#include "a2b/ecb.h"
#include "a2b/pal.h"
#include "adi_a2b_datatypes.h"
#include "adi_a2b_pal.h"
#include "pal_i2c.h"

/*---------------------------------------------------------------------------
 * I2C device path (default)
 * /dev/i2c-1 maps to GPIO2 (SDA) / GPIO3 (SCL) on the Pi Zero 2W.
 * This is the hardware I2C bus; it requires:
 *   dtparam=i2c_arm=on
 *   dtparam=i2c_arm_baudrate=400000
 * in config.txt (/boot/firmware/config.txt on Bookworm and later).  The
 * default bus speed is 100 kHz if the baudrate line is absent; the actual
 * speed is reported at open time when the kernel exposes it.
 * Other boards may use a different bus number: override with
 * a2b_pal_i2c_set_device().
 *---------------------------------------------------------------------------*/
#define I2C_DEVICE  "/dev/i2c-1"

#define I2C_REQUIRED_HZ  400000u

static const char *s_i2c_device = I2C_DEVICE;

void a2b_pal_i2c_set_device(const char *path)
{
    if (path && path[0] != '\0') {
        s_i2c_device = path;
    }
}

/*---------------------------------------------------------------------------
 * i2c_report_bus_speed
 *
 * Best-effort: reads the controller's device-tree clock-frequency property
 * (a big-endian u32) for this bus from sysfs and warns if it is below
 * 400 kHz.  Silent if the kernel doesn't expose it (non-DT platforms).
 *---------------------------------------------------------------------------*/
static void i2c_report_bus_speed(const char *dev_path)
{
    const char *name = strrchr(dev_path, '/');
    char sysfs_path[128];
    unsigned char be[4];
    unsigned int hz;
    FILE *f;

    name = name ? name + 1 : dev_path;            /* "i2c-1" */
    snprintf(sysfs_path, sizeof(sysfs_path),
             "/sys/bus/i2c/devices/%s/of_node/clock-frequency", name);

    f = fopen(sysfs_path, "rb");
    if (!f) {
        return;
    }
    if (fread(be, 1, sizeof(be), f) == sizeof(be)) {
        hz = ((unsigned int)be[0] << 24) | ((unsigned int)be[1] << 16) |
             ((unsigned int)be[2] << 8)  |  (unsigned int)be[3];
        if (hz < I2C_REQUIRED_HZ) {
            fprintf(stderr,
                "pal_i2c: WARNING: %s runs at %u Hz; the AD2437 needs %u Hz.\n"
                "  Add dtparam=i2c_arm_baudrate=400000 to config.txt "
                "(/boot/firmware/config.txt on Bookworm and later) and reboot.\n",
                dev_path, hz, I2C_REQUIRED_HZ);
        } else {
            fprintf(stderr, "pal_i2c: %s bus speed %u Hz\n", dev_path, hz);
        }
    }
    fclose(f);
}

/*---------------------------------------------------------------------------
 * Context
 * One static context for the single I2C bus.  a2b_Handle is void*; we
 * return a pointer to this struct from I2cOpenFunc and recover it in all
 * subsequent PAL calls.  No dynamic allocation required.
 *---------------------------------------------------------------------------*/
typedef struct {
    int fd;
} linux_i2c_ctx_t;

static linux_i2c_ctx_t s_i2c_ctx = { .fd = -1 };

/*===========================================================================
 * a2b_pal_I2cInit
 *
 * Called once during stack allocation to initialise the I2C subsystem.
 * On Linux nothing needs to be done here; the bus is opened in I2cOpenFunc.
 *
 * Signature: Table 3, Stack UG Rev 11.0 (not mandatory).
 *===========================================================================*/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_UInt32 a2b_pal_I2cInit(A2B_ECB *ecb)
{
    A2B_UNUSED(ecb);
    return A2B_RESULT_SUCCESS;
}

/*===========================================================================
 * a2b_pal_I2cOpenFunc
 *
 * Post-initialisation: opens the I2C device (default /dev/i2c-1), checks
 * that the adapter supports combined I2C_RDWR transfers, and returns the
 * handle.  Called immediately after a successful a2b_pal_I2cInit.
 *
 * @param fmt   7-bit or 10-bit addressing (A2B_I2C_ADDR_FMT_7BIT /
 *              A2B_I2C_ADDR_FMT_10BIT).  AD2437 uses 7-bit; parameter
 *              validated but not used beyond the open() call.
 * @param speed Bus speed requested by the stack.  The actual speed is set
 *              in config.txt (dtparam=i2c_arm_baudrate=400000); we cannot
 *              change it at runtime via the kernel I2C interface.
 *              A warning is printed if 100 kHz is requested, or if the
 *              kernel reports the bus running below 400 kHz.
 * @param ecb   PAL ECB — unused at this layer.
 *
 * @return  Handle (pointer to static context) on success; A2B_NULL on error.
 *
 * Signature: Table 3, Stack UG Rev 11.0 (mandatory).
 *===========================================================================*/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_Handle a2b_pal_I2cOpenFunc(a2b_I2cAddrFmt fmt,
                               a2b_I2cBusSpeed speed,
                               A2B_ECB *ecb)
{
    unsigned long funcs = 0;
    int err;

    A2B_UNUSED(fmt);
    A2B_UNUSED(ecb);

    if (speed == A2B_I2C_BUS_SPEED_100KHZ) {
        fprintf(stderr,
            "pal_i2c: WARNING: stack requested 100 kHz I2C bus speed. "
            "Verify dtparam=i2c_arm_baudrate=400000 is set in config.txt "
            "for AD2437 operation.\n");
    }

    s_i2c_ctx.fd = open(s_i2c_device, O_RDWR);
    if (s_i2c_ctx.fd < 0) {
        err = errno;
        fprintf(stderr, "pal_i2c: open(%s) failed: %s\n",
                s_i2c_device, strerror(err));
        if (err == EACCES || err == EPERM) {
            fprintf(stderr,
                "  Add your user to the i2c group and log in again:\n"
                "    sudo usermod -aG i2c $USER\n"
                "  or run with sudo.\n");
        } else if (err == ENOENT) {
            fprintf(stderr,
                "  I2C is not enabled, or this board uses another bus number.\n"
                "  Enable it with dtparam=i2c_arm=on in config.txt "
                "(/boot/firmware/config.txt on Bookworm and later) and reboot.\n"
                "  List buses with: ls /dev/i2c-*   then pass -d <device>.\n");
        }
        return A2B_NULL;
    }

    /* The stack relies on combined write+read transfers with a repeated
     * start (I2C_RDWR).  SMBus-only adapters can't do that, so fail here
     * with a clear message rather than on the first register read. */
    if (ioctl(s_i2c_ctx.fd, I2C_FUNCS, &funcs) < 0) {
        fprintf(stderr, "pal_i2c: ioctl(%s, I2C_FUNCS) failed: %s\n",
                s_i2c_device, strerror(errno));
    } else if (!(funcs & I2C_FUNC_I2C)) {
        fprintf(stderr,
            "pal_i2c: %s does not support plain I2C transfers (I2C_RDWR); "
            "it is SMBus-only and cannot drive the AD2437.\n",
            s_i2c_device);
        close(s_i2c_ctx.fd);
        s_i2c_ctx.fd = -1;
        return A2B_NULL;
    }

    i2c_report_bus_speed(s_i2c_device);

    return (a2b_Handle)&s_i2c_ctx;
}

/*===========================================================================
 * a2b_pal_I2cCloseFunc
 *
 * Called during stack destroy to close the I2C bus.
 *
 * Signature: Table 3, Stack UG Rev 11.0 (not mandatory).
 *===========================================================================*/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_I2cCloseFunc(a2b_Handle hnd)
{
    linux_i2c_ctx_t *ctx = (linux_i2c_ctx_t *)hnd;

    if (ctx && ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }

    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

/*===========================================================================
 * a2b_pal_I2cReadFunc
 *
 * Read-only I2C transaction: single ioctl(I2C_RDWR) with one i2c_msg
 * struct, I2C_M_RD flag set.
 *
 * IMPORTANT: this is NOT a write followed by a read.  The stack uses
 * a2b_pal_I2cWriteReadFunc for register read (write address, read data).
 * This function is for a bare read with no preceding write phase.
 *
 * @param hnd   Handle returned by I2cOpenFunc.
 * @param addr  7-bit I2C device address.
 * @param nRead Number of bytes to read.
 * @param rBuf  Receive buffer.
 *
 * @return  A2B_RESULT_SUCCESS (0) on success; 1 on error.
 *
 * Signature: Table 3, Stack UG Rev 11.0 (not mandatory).
 *===========================================================================*/
ADI_MEM_A2B_CODE_CRIT
a2b_HResult a2b_pal_I2cReadFunc(a2b_Handle hnd,
                                a2b_UInt16  addr,
                                a2b_UInt16  nRead,
                                a2b_Byte   *rBuf)
{
    linux_i2c_ctx_t *ctx = (linux_i2c_ctx_t *)hnd;
    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data msgset;
    int rc;

    msg.addr  = (__u16)addr;
    msg.flags = I2C_M_RD;
    msg.len   = (__u16)nRead;
    msg.buf   = (__u8 *)rBuf;

    msgset.msgs  = &msg;
    msgset.nmsgs = 1;

    rc = ioctl(ctx->fd, I2C_RDWR, &msgset);
    if (rc < 0) {
        fprintf(stderr,
            "pal_i2c: I2cRead addr=0x%02x nRead=%u failed: %s\n",
            addr, nRead, strerror(errno));
        return (a2b_HResult)1;
    }

    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

/*===========================================================================
 * a2b_pal_I2cWriteFunc
 *
 * Write-only I2C transaction: single ioctl(I2C_RDWR) with one write
 * i2c_msg struct.
 *
 * @param hnd    Handle returned by I2cOpenFunc.
 * @param addr   7-bit I2C device address.
 * @param nWrite Number of bytes to write.
 * @param wBuf   Transmit buffer (register address + data).
 *
 * @return  A2B_RESULT_SUCCESS (0) on success; 1 on error.
 *
 * Signature: Table 3, Stack UG Rev 11.0 (mandatory).
 *===========================================================================*/
ADI_MEM_A2B_CODE_CRIT
a2b_HResult a2b_pal_I2cWriteFunc(a2b_Handle      hnd,
                                 a2b_UInt16       addr,
                                 a2b_UInt16       nWrite,
                                 const a2b_Byte  *wBuf)
{
    linux_i2c_ctx_t *ctx = (linux_i2c_ctx_t *)hnd;
    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data msgset;
    int rc;

    msg.addr  = (__u16)addr;
    msg.flags = 0;                    /* write */
    msg.len   = (__u16)nWrite;
    msg.buf   = (__u8 *)(uintptr_t)wBuf; /* cast away const — kernel only reads */

    msgset.msgs  = &msg;
    msgset.nmsgs = 1;

    rc = ioctl(ctx->fd, I2C_RDWR, &msgset);
    if (rc < 0) {
        fprintf(stderr,
            "pal_i2c: I2cWrite addr=0x%02x reg=0x%02x nWrite=%u failed: %s\n",
            addr, wBuf[0], nWrite, strerror(errno));
        return (a2b_HResult)1;
    }

    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

/*===========================================================================
 * a2b_pal_I2cWriteReadFunc
 *
 * Atomic repeated-start I2C transaction.  Used by the stack for all AD2437
 * register reads: write the register address, then read the register value
 * in a single bus transaction with a repeated start between the two phases.
 *
 * IMPLEMENTATION RULE (CLAUDE.md §1): this MUST be a single ioctl(I2C_RDWR)
 * call with TWO i2c_msg structs — msgs[0] for the write phase, msgs[1] with
 * I2C_M_RD for the read phase.  Two separate ioctl calls are NOT equivalent;
 * the AD2437 requires the repeated-start to be atomic and will NACK if the
 * bus is released between write and read.
 *
 * @param hnd    Handle returned by I2cOpenFunc.
 * @param addr   7-bit I2C device address.
 * @param nWrite Number of bytes to write (typically 1: the register address).
 * @param wBuf   Transmit buffer (register address).
 * @param nRead  Number of bytes to read.
 * @param rBuf   Receive buffer.
 *
 * @return  A2B_RESULT_SUCCESS (0) on success; 1 on error.
 *
 * Signature: Table 3, Stack UG Rev 11.0 (mandatory).
 *===========================================================================*/
ADI_MEM_A2B_CODE_CRIT
a2b_HResult a2b_pal_I2cWriteReadFunc(a2b_Handle      hnd,
                                     a2b_UInt16       addr,
                                     a2b_UInt16       nWrite,
                                     const a2b_Byte  *wBuf,
                                     a2b_UInt16       nRead,
                                     a2b_Byte        *rBuf)
{
    linux_i2c_ctx_t *ctx = (linux_i2c_ctx_t *)hnd;
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data msgset;
    int rc;

    /* Write phase: sends the register address (no STOP issued after this) */
    msgs[0].addr  = (__u16)addr;
    msgs[0].flags = 0;                        /* write */
    msgs[0].len   = (__u16)nWrite;
    msgs[0].buf   = (__u8 *)(uintptr_t)wBuf; /* cast away const — kernel only reads */

    /* Read phase: repeated-start, then clock in the register data */
    msgs[1].addr  = (__u16)addr;
    msgs[1].flags = I2C_M_RD;                /* read */
    msgs[1].len   = (__u16)nRead;
    msgs[1].buf   = (__u8 *)rBuf;

    msgset.msgs  = msgs;
    msgset.nmsgs = 2;

    rc = ioctl(ctx->fd, I2C_RDWR, &msgset);
    if (rc < 0) {
        fprintf(stderr,
            "pal_i2c: I2cWriteRead addr=0x%02x reg=0x%02x "
            "nWrite=%u nRead=%u failed: %s\n",
            addr, wBuf[0], nWrite, nRead, strerror(errno));
        return (a2b_HResult)1;
    }

    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

/*===========================================================================
 * a2b_pal_I2cShutdownFunc
 *
 * Called during stack destroy after I2cCloseFunc.  No action required on
 * Linux — the fd is already closed by I2cCloseFunc.
 *
 * Signature: Table 3, Stack UG Rev 11.0 (not mandatory).
 *===========================================================================*/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_UInt32 a2b_pal_I2cShutdownFunc(A2B_ECB *ecb)
{
    A2B_UNUSED(ecb);
    return A2B_RESULT_SUCCESS;
}
