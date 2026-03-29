/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x 
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "opentx.h"
#include "diskio_spi.h"

#include "driver/gpio.h"
#include "soc/soc_caps.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"


static bool card_present = false; // default to consider it as present until mount failed
static sdmmc_host_t config = SDSPI_HOST_DEFAULT();
static sdspi_dev_handle_t handle;
static sdspi_device_config_t dev_config = SDSPI_DEVICE_CONFIG_DEFAULT();
static sdmmc_card_t sdcard;
static sdmmc_card_t* card = &sdcard;

static DSTATUS sdcard_spi_initialize(BYTE lun)
{
    if (!card_present) {
#ifdef SD_DEDICATED_SPI
        spi_bus_config_t bus_config = {
            .mosi_io_num = SDSPI_MOSI,
            .miso_io_num = SDSPI_MISO,
            .sclk_io_num = SDSPI_CLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
        };
        config.slot = SD_SPI_HOST;
        ESP_ERROR_CHECK(spi_bus_initialize((spi_host_device_t)config.slot, &bus_config, SPI_DMA_CH_AUTO));
#endif
        dev_config.host_id = (spi_host_device_t)config.slot;

        dev_config.gpio_cs = SDCARD_CS_GPIO;
        sdspi_host_init();
        sdspi_host_init_device(&dev_config, &handle);

        config.slot = handle;
        if (0 == sdmmc_card_init(&config, card)) {
            card_present = true;
        } else {
            sdspi_host_remove_device(handle);
            sdspi_host_deinit();
        }
    }
    return 0;
}

static DSTATUS sdcard_spi_status(BYTE lun)
{
    DSTATUS stat = 0;
    return stat;
}

static DRESULT sdcard_spi_read(BYTE lun, BYTE * buff, DWORD sector, UINT count)
{
    // TRACE("disk_read %d %p %10d %d", lun, buff, sector, count);

    DRESULT state = RES_OK;
    if (0 != sdmmc_read_sectors(card, buff, sector, count)) {
        state = RES_ERROR;
    }

    return state;
}

static DRESULT sdcard_spi_write(BYTE lun, const BYTE* buff, DWORD sector, UINT count)
{
    // TRACE("disk_write %d %p %10d %d", lun, buff, sector, count);

    DRESULT res = RES_OK;
    if (0 != sdmmc_write_sectors(card, buff, sector, count)) {
        res = RES_ERROR;
    }
    return res;
}

static DRESULT sdcard_spi_ioctl(BYTE lun, BYTE ctrl, void *buff)
{
    assert(card);
    switch(ctrl) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *((DWORD*) buff) = card->csd.capacity;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *((WORD*) buff) = card->csd.sector_size;
        return RES_OK;
    case GET_BLOCK_SIZE:
        return RES_ERROR;
    }
    return RES_ERROR;
}

const diskio_driver_t sdcard_spi_driver = {
    .initialize = sdcard_spi_initialize,
    .status = sdcard_spi_status,
    .read = sdcard_spi_read,
    .write = sdcard_spi_write,
    .ioctl = sdcard_spi_ioctl,
};

