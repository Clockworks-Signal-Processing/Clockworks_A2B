/*******************************************************************************
 * pal_i2c.h — Linux I2C PAL for A2B Stack (runtime configuration)
 * ClockWorks Signal Processing LLC
 ******************************************************************************/

#ifndef PAL_I2C_H_
#define PAL_I2C_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Override the I2C device the PAL opens (default "/dev/i2c-1").
 * Must be called before the stack opens the bus.  The string is not
 * copied, so it must stay valid for the life of the program (argv is fine).
 */
void a2b_pal_i2c_set_device(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* PAL_I2C_H_ */
