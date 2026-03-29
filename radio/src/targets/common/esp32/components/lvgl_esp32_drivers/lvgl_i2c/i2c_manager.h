#ifndef _I2C_MANAGER_H
#define _I2C_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

// Migrate to use driver/i2c_master.h
#include <driver/i2c_master.h>

/**
 * Let the app code to manage I2C port in a central place.
 * Require the app code to provide this to the bus handle
 */
extern i2c_master_bus_handle_t lvgl_i2c_bus_handle;

#ifdef __cplusplus
}
#endif
#endif
