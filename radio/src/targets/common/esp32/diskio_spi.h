#ifndef _SDIO_SPI_H_
#define _SDIO_SPI_H_

#include <hal/fatfs_diskio.h>
#include "sdmmc_cmd.h"

extern const diskio_driver_t sdcard_spi_driver;
bool sdcardSpiEnsureInitialized();
sdmmc_card_t * sdcardSpiGetCard();

#endif // _SDIO_SPI_H_
