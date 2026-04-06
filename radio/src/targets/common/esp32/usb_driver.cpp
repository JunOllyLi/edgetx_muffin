/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
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

#include "hal/usb_driver.h"

#include "hal/adc_driver.h"
#include "hal/key_driver.h"

#include "usb_joystick.h"

#include "debug.h"
#include "diskio_spi.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

#include <cstring>

namespace {

static usbMode selectedUsbMode = USB_UNSELECTED_MODE;
static bool usbSessionEnabled = false;
static bool usbDriverStarted = false;
static bool usbStorageMounted = false;

static constexpr uint8_t kMscLun = 0;
static constexpr uint8_t kMscMaxLun = 1;
static constexpr uint16_t kFallbackSectorSize = 512;
static constexpr uint16_t kUsbConnectedVbatThreshold = 430;
static constexpr uint16_t kUsbVendorId = 0x303A;
static constexpr uint16_t kUsbProductIdHid = 0x4004;
static constexpr uint16_t kUsbProductIdMsc = 0x4002;
static constexpr uint16_t kUsbBcdDevice = 0x0100;
static constexpr uint8_t kUsbStringCount = 5;
static constexpr uint8_t kUsbEndpointHidIn = 0x81;
static constexpr uint8_t kUsbEndpointMscOut = 0x02;
static constexpr uint8_t kUsbEndpointMscIn = 0x82;
static constexpr uint8_t kUsbHidPacketSize = 64;
static constexpr uint8_t kUsbMscPacketSize = 64;
static constexpr uint8_t kUsbHidPollingInterval = 1;
static constexpr size_t kUsbHidReportDescLenOffset =
    TUD_CONFIG_DESC_LEN + 9 + 7;
static uint8_t mscSectorBuffer[kFallbackSectorSize];

enum UsbInterfaceIndex : uint8_t {
    kUsbInterfaceHid = 0,
    kUsbInterfaceMsc = 0,
};

static uint8_t hidConfigurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(kUsbInterfaceHid, 4, false, 0, kUsbEndpointHidIn,
                       kUsbHidPacketSize, kUsbHidPollingInterval),
};

static const uint8_t mscConfigurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(kUsbInterfaceMsc, 4, kUsbEndpointMscOut,
                       kUsbEndpointMscIn, kUsbMscPacketSize),
};

static const char * usbStringDescriptorHid[kUsbStringCount] = {
    "0x0409",
    "EdgeTX",
    "Muffin Joystick",
    "0001",
    "HID Interface",
};

static const char * usbStringDescriptorMsc[kUsbStringCount] = {
    "0x0409",
    "EdgeTX",
    "Muffin Storage",
    "0001",
    "MSC Interface",
};

static const tusb_desc_device_t usbDeviceDescriptorHid = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = kUsbVendorId,
    .idProduct = kUsbProductIdHid,
    .bcdDevice = kUsbBcdDevice,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const tusb_desc_device_t usbDeviceDescriptorMsc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = kUsbVendorId,
    .idProduct = kUsbProductIdMsc,
    .bcdDevice = kUsbBcdDevice,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

template <typename T>
constexpr T usbMin(T a, T b)
{
    return a < b ? a : b;
}

sdmmc_card_t * getUsbStorageCard()
{
    return sdcardSpiGetCard();
}

bool usbSessionRequestedAtBoot()
{
    pollKeys();
    return (readKeys() & (1u << KEY_MODEL)) != 0;
}

bool isUsbStorageReady(uint8_t lun)
{
    return lun == kMscLun && usbDriverStarted && usbStorageMounted &&
           getUsbStorageCard() != nullptr;
}

bool mscReadBuffer(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    auto *card = getUsbStorageCard();
    if (card == nullptr) {
        return false;
    }

    const uint32_t sectorSize = card->csd.sector_size;
    uint8_t *out = static_cast<uint8_t *>(buffer);

    while (bufsize > 0) {
        const uint32_t chunk = usbMin<uint32_t>(bufsize, sectorSize - offset);
        if (offset == 0 && chunk == sectorSize) {
            if (sdmmc_read_sectors(card, out, lba, 1) != 0) {
                return false;
            }
        }
        else {
            if (sectorSize > sizeof(mscSectorBuffer) ||
                sdmmc_read_sectors(card, mscSectorBuffer, lba, 1) != 0) {
                return false;
            }
            memcpy(out, mscSectorBuffer + offset, chunk);
        }

        out += chunk;
        bufsize -= chunk;
        offset = 0;
        ++lba;
    }

    return true;
}

bool mscWriteBuffer(uint32_t lba, uint32_t offset, const uint8_t *buffer,
                    uint32_t bufsize)
{
    auto *card = getUsbStorageCard();
    if (card == nullptr) {
        return false;
    }

    const uint32_t sectorSize = card->csd.sector_size;
    const uint8_t *in = buffer;

    while (bufsize > 0) {
        const uint32_t chunk = usbMin<uint32_t>(bufsize, sectorSize - offset);
        if (offset == 0 && chunk == sectorSize) {
            if (sdmmc_write_sectors(card, in, lba, 1) != 0) {
                return false;
            }
        }
        else {
            if (sectorSize > sizeof(mscSectorBuffer) ||
                sdmmc_read_sectors(card, mscSectorBuffer, lba, 1) != 0) {
                return false;
            }
            memcpy(mscSectorBuffer + offset, in, chunk);
            if (sdmmc_write_sectors(card, mscSectorBuffer, lba, 1) != 0) {
                return false;
            }
        }

        in += chunk;
        bufsize -= chunk;
        offset = 0;
        ++lba;
    }

    return true;
}

void usbTinyUsbEvent(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    switch (event->id) {
      case TINYUSB_EVENT_ATTACHED:
        ESP_LOGI("USB", "attached");
        break;

      case TINYUSB_EVENT_DETACHED:
        ESP_LOGI("USB", "detached");
        break;

      default:
        break;
    }
}

}  // namespace

int getSelectedUsbMode()
{
    return selectedUsbMode;
}

void setSelectedUsbMode(int mode)
{
    selectedUsbMode = usbMode(mode);
}

void usbInit()
{
    usbDriverStarted = false;
    usbStorageMounted = false;
    usbSessionEnabled = false;
    selectedUsbMode = USB_UNSELECTED_MODE;
    ESP_LOGI("USB", "usbInit");
    usbSessionEnabled = usbSessionRequestedAtBoot();
}

void usbStart()
{
    ESP_LOGI("USB", "usbStart");
    if (!usbSessionEnabled || usbDriverStarted) {
        return;
    }

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(usbTinyUsbEvent);

    switch (getSelectedUsbMode()) {
      case USB_MASS_STORAGE_MODE:
        if (!sdcardSpiEnsureInitialized()) {
            TRACE("USB MSC start skipped: SD card is not available");
            return;
        }
        usbStorageMounted = true;
        tusb_cfg.descriptor.device = &usbDeviceDescriptorMsc;
        tusb_cfg.descriptor.string = usbStringDescriptorMsc;
        tusb_cfg.descriptor.string_count = kUsbStringCount;
        tusb_cfg.descriptor.full_speed_config = mscConfigurationDescriptor;
        tusb_cfg.descriptor.high_speed_config = mscConfigurationDescriptor;
        break;

      case USB_JOYSTICK_MODE: {
        if (!setupUSBJoystick()) {
            TRACE("USB HID start skipped: joystick descriptor setup failed");
            return;
        }
        usbStorageMounted = false;
        auto reportDesc = usbReportDesc();
        tusb_cfg.descriptor.device = &usbDeviceDescriptorHid;
        tusb_cfg.descriptor.string = usbStringDescriptorHid;
        tusb_cfg.descriptor.string_count = kUsbStringCount;
        hidConfigurationDescriptor[kUsbHidReportDescLenOffset] =
            reportDesc.size & 0xFF;
        hidConfigurationDescriptor[kUsbHidReportDescLenOffset + 1] =
            reportDesc.size >> 8;
        tusb_cfg.descriptor.full_speed_config = hidConfigurationDescriptor;
        tusb_cfg.descriptor.high_speed_config = hidConfigurationDescriptor;
        break;
      }

      default:
        return;
    }

    if (tinyusb_driver_install(&tusb_cfg) != ESP_OK) {
        TRACE("tinyusb_driver_install failed");
        usbStorageMounted = false;
        return;
    }
    usbDriverStarted = true;
}

void usbStop()
{
    ESP_LOGI("USB", "usbStop");
    if (!usbDriverStarted) {
        return;
    }

    tinyusb_driver_uninstall();

    usbStorageMounted = false;
    usbDriverStarted = false;
}

bool usbStarted()
{
    return usbDriverStarted;
}

int usbPlugged()
{
    if (!usbSessionEnabled) {
        return false;
    }

    return getBatteryVoltage() > kUsbConnectedVbatThreshold;
}

void usbJoystickRestart()
{
    if (!usbDriverStarted || getSelectedUsbMode() != USB_JOYSTICK_MODE) {
        return;
    }

    usbStop();
    usbStart();
}

void usbJoystickUpdate()
{
    if (!usbDriverStarted || getSelectedUsbMode() != USB_JOYSTICK_MODE ||
        !tud_hid_ready()) {
        return;
    }

    usbReport_t report = usbReport();
    if (report.ptr != nullptr && report.size > 0) {
        tud_hid_report(0, report.ptr, report.size);
    }
}

extern "C" void msc_storage_mount_to_usb(void)
{
}

extern "C" void msc_storage_mount_to_app(void)
{
}

extern "C" uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return usbReportDesc().ptr;
}

extern "C" uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                          hid_report_type_t report_type,
                                          uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

extern "C" void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                                      hid_report_type_t report_type,
                                      uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

extern "C" uint8_t tud_msc_get_maxlun_cb(void)
{
    return kMscMaxLun;
}

extern "C" void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                                   uint8_t product_id[16],
                                   uint8_t product_rev[4])
{
    (void)lun;

    const char vid[] = "EdgeTX";
    const char pid[] = "SD Card";
    const char rev[] = "1.0";

    memset(vendor_id, ' ', 8);
    memset(product_id, ' ', 16);
    memset(product_rev, ' ', 4);

    memcpy(vendor_id, vid, usbMin<size_t>(8, sizeof(vid) - 1));
    memcpy(product_id, pid, usbMin<size_t>(16, sizeof(pid) - 1));
    memcpy(product_rev, rev, usbMin<size_t>(4, sizeof(rev) - 1));
}

extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (isUsbStorageReady(lun)) {
        return true;
    }

    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
    return false;
}

extern "C" void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                                     uint16_t *block_size)
{
    if (!isUsbStorageReady(lun)) {
        *block_count = 0;
        *block_size = kFallbackSectorSize;
        return;
    }

    auto *card = getUsbStorageCard();
    *block_count = card->csd.capacity;
    *block_size = card->csd.sector_size;
}

extern "C" bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition,
                                       bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;
}

extern "C" bool tud_msc_is_writable_cb(uint8_t lun)
{
    return isUsbStorageReady(lun);
}

extern "C" int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba,
                                      uint32_t offset, void *buffer,
                                      uint32_t bufsize)
{
    if (!isUsbStorageReady(lun) || !mscReadBuffer(lba, offset, buffer, bufsize)) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }

    return bufsize;
}

extern "C" int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba,
                                       uint32_t offset, uint8_t *buffer,
                                       uint32_t bufsize)
{
    if (!isUsbStorageReady(lun) ||
        !mscWriteBuffer(lba, offset, buffer, bufsize)) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }

    return bufsize;
}

extern "C" int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                                    void *buffer, uint16_t bufsize)
{
    (void)buffer;
    (void)bufsize;

    switch (scsi_cmd[0]) {
      case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        return 0;

      default:
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }
}
