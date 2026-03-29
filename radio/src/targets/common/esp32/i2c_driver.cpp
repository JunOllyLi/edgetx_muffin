
#include <opentx.h>
#include "i2c_driver.h"

esp_err_t i2c_register_read(i2c_master_dev_handle_t handle, uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(handle, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS);
}

esp_err_t i2c_register_read_uint16(i2c_master_dev_handle_t handle, uint8_t reg_addr, uint16_t *data)
{
    uint8_t buf[2] = {0};
    esp_err_t ret = i2c_master_transmit_receive(handle, &reg_addr, 1, buf, 2, I2C_MASTER_TIMEOUT_MS);
    *data = ((uint16_t)buf[0] << 8) | buf[1];
    return ret;
}

esp_err_t i2c_register_write_byte(i2c_master_dev_handle_t handle, uint8_t reg_addr, uint8_t data)
{
    int ret;
    uint8_t write_buf[2] = {reg_addr, data};

    ret = i2c_master_transmit(handle, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS);

    return ret;
}

esp_err_t i2c_register_write_uint16(i2c_master_dev_handle_t handle, uint8_t reg_addr, uint16_t data)
{
    int ret;
    uint8_t write_buf[3] = {reg_addr, (uint8_t)(data >> 8), (uint8_t)(data & 0xFF)};

    ret = i2c_master_transmit(handle, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS);

    return ret;
}

esp_err_t i2c_register_write_read_buf(i2c_master_dev_handle_t handle, uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen) {
    esp_err_t ret;
    ret = i2c_master_transmit_receive(handle, wbuf, wlen, rbuf, rlen, I2C_MASTER_TIMEOUT_MS);
    return ret;
}

esp_err_t i2c_register_write_buf(i2c_master_dev_handle_t handle, uint8_t *buf, size_t len)
{
    int ret;
    ret = i2c_master_transmit(handle, buf, len, I2C_MASTER_TIMEOUT_MS);

    return ret;
}

