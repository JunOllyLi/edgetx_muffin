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

#include <stdio.h>

#include "edgetx.h"
#include "io/frsky_firmware_update.h"
#include "bluetooth_driver.h"
#include "os/sleep.h"
#include "trainer.h"

#if defined(LOG_BLUETOOTH)
extern FIL g_bluetoothFile;
#endif

#if defined(_MSC_VER)
  #define SWAP32(val)      (_byteswap_ulong(val))
#elif defined(__GNUC__ )
  #define SWAP32(val)      (__builtin_bswap32(val))
#endif

Bluetooth bluetooth;

namespace {

constexpr tmr10ms_t BT_WAKEUP_DEFAULT_DELAY = 5;
constexpr tmr10ms_t BT_WAKEUP_DISABLED_DELAY = 10;
constexpr tmr10ms_t BT_WAKEUP_CONNECTING_DELAY = 20;
constexpr tmr10ms_t BT_WAKEUP_RECONNECT_DELAY = 200;
constexpr tmr10ms_t BT_TRAINER_ACTIVE_DELAY = 1;
constexpr tmr10ms_t BT_TRAINER_SLAVE_CONNECTED_DELAY = 10;

BluetoothPlatformRole getDesiredRole()
{
  if (g_eeGeneral.bluetoothMode == BLUETOOTH_TRAINER &&
      g_model.trainerData.mode == TRAINER_MODE_MASTER_BLUETOOTH) {
    return BLUETOOTH_ROLE_CENTRAL;
  }

  return BLUETOOTH_ROLE_PERIPHERAL;
}

void getDesiredName(char *name, size_t len)
{
  uint8_t btNameLen = ZLEN(g_eeGeneral.bluetoothName);
  if (btNameLen > 0) {
    size_t out = 0;
    for (uint8_t i = 0; i < btNameLen && out + 1 < len; ++i) {
      name[out++] = char2lower(g_eeGeneral.bluetoothName[i]);
    }
    name[out] = '\0';
  }
  else {
    strAppend(name, FLAVOUR);
  }
}

bool bluetoothShouldBeEnabled()
{
  return !(g_eeGeneral.bluetoothMode == BLUETOOTH_OFF ||
           (g_eeGeneral.bluetoothMode == BLUETOOTH_TRAINER &&
            !IS_BLUETOOTH_TRAINER()));
}

void refreshLocalAddress()
{
  char addr[LEN_BLUETOOTH_ADDR + 1];
  if (bluetoothGetLocalAddress(addr, sizeof(addr))) {
    strcpy(bluetooth.localAddr, addr);
  }
}

void refreshDiscoveryResults()
{
  uint8_t count = min<uint8_t>(bluetoothGetDiscoveryResultCount(),
                               MAX_BLUETOOTH_DISTANT_ADDR);
  reusableBuffer.moduleSetup.bt.devicesCount = 0;

  for (uint8_t i = 0; i < count; ++i) {
    char addr[LEN_BLUETOOTH_ADDR + 1];
    if (bluetoothGetDiscoveryResult(i, addr, sizeof(addr))) {
      strncpy(reusableBuffer.moduleSetup.bt.devices[i], addr,
              LEN_BLUETOOTH_ADDR);
      reusableBuffer.moduleSetup.bt.devices[i][LEN_BLUETOOTH_ADDR] = '\0';
      ++reusableBuffer.moduleSetup.bt.devicesCount;
    }
  }
}

tmr10ms_t updateConnectionEvents()
{
  tmr10ms_t delay = 0;
  char addr[LEN_BLUETOOTH_ADDR + 1];
  if (bluetoothTakeConnected(addr, sizeof(addr))) {
    strncpy(bluetooth.distantAddr, addr, LEN_BLUETOOTH_ADDR);
    bluetooth.distantAddr[LEN_BLUETOOTH_ADDR] = '\0';
    bluetooth.state = BLUETOOTH_STATE_CONNECTED;
    if (g_model.trainerData.mode == TRAINER_MODE_SLAVE_BLUETOOTH) {
      delay = BT_TRAINER_SLAVE_CONNECTED_DELAY;
    }
  }

  if (bluetoothTakeDisconnected()) {
    bluetooth.state = BLUETOOTH_STATE_DISCONNECTED;
    delay = 200;
  }

  return delay;
}

}  // namespace

void Bluetooth::write(const uint8_t *data, uint8_t length)
{
  BLUETOOTH_TRACE_VERBOSE("BT>");
  for (int i = 0; i < length; i++) {
    BLUETOOTH_TRACE_VERBOSE(" %02X", data[i]);
  }
  BLUETOOTH_TRACE_VERBOSE(CRLF);
  bluetoothWrite(data, length);
}

static const char _bt_crlf[] = "\r\n";

void Bluetooth::writeString(const char *str)
{
  BLUETOOTH_TRACE("BT> %s" CRLF, str);
  bluetoothWrite(str, strlen(str));
  bluetoothWrite(_bt_crlf, sizeof(_bt_crlf) - 1);
}

char * Bluetooth::readline(bool)
{
  uint8_t byte;

  while (true) {
    if (!bluetoothRead(&byte)) {
      return nullptr;
    }

    BLUETOOTH_TRACE_VERBOSE("%02X ", byte);

    if (byte == '\n') {
      if (bufferIndex > 2 && buffer[bufferIndex - 1] == '\r') {
        buffer[bufferIndex - 1] = '\0';
        bufferIndex = 0;
        BLUETOOTH_TRACE("BT< %s" CRLF, buffer);
        return (char *)buffer;
      }

      bufferIndex = 0;
    }
    else {
      buffer[bufferIndex++] = byte;
      bufferIndex &= (BLUETOOTH_LINE_LENGTH - 1);
    }
  }
}

void Bluetooth::processTrainerFrame(const uint8_t *buffer)
{
  for (uint8_t channel = 0, i = 1; channel < BLUETOOTH_TRAINER_CHANNELS;
       channel += 2, i += 3) {
    trainerInput[channel] =
        buffer[i] + ((buffer[i + 1] & 0xf0) << 4) - 1500;
    trainerInput[channel + 1] =
        ((buffer[i + 1] & 0x0f) << 4) + ((buffer[i + 2] & 0xf0) >> 4) +
        ((buffer[i + 2] & 0x0f) << 8) - 1500;
  }

  trainerResetTimer();
}

void Bluetooth::appendTrainerByte(uint8_t data)
{
  if (bufferIndex < BLUETOOTH_LINE_LENGTH) {
    buffer[bufferIndex++] = data;
  }
}

void Bluetooth::processTrainerByte(uint8_t data)
{
  static uint8_t dataState = STATE_DATA_IDLE;

  switch (dataState) {
    case STATE_DATA_START:
      if (data == START_STOP) {
        dataState = STATE_DATA_IN_FRAME;
        bufferIndex = 0;
      }
      else {
        appendTrainerByte(data);
      }
      break;

    case STATE_DATA_IN_FRAME:
      if (data == BYTE_STUFF) {
        dataState = STATE_DATA_XOR;
      }
      else if (data == START_STOP) {
        dataState = STATE_DATA_IN_FRAME;
        bufferIndex = 0;
      }
      else {
        appendTrainerByte(data);
      }
      break;

    case STATE_DATA_XOR:
      switch (data) {
        case BYTE_STUFF ^ STUFF_MASK:
        case START_STOP ^ STUFF_MASK:
          appendTrainerByte(data ^ STUFF_MASK);
          dataState = STATE_DATA_IN_FRAME;
          break;
        case START_STOP:
          bufferIndex = 0;
          dataState = STATE_DATA_IN_FRAME;
          break;
        default:
          dataState = STATE_DATA_START;
          break;
      }
      break;

    case STATE_DATA_IDLE:
      if (data == START_STOP) {
        bufferIndex = 0;
        dataState = STATE_DATA_START;
      }
      else {
        appendTrainerByte(data);
      }
      break;
  }

  if (bufferIndex >= BLUETOOTH_PACKET_SIZE) {
    uint8_t check = 0x00;
    for (int i = 0; i < BLUETOOTH_PACKET_SIZE - 1; i++) {
      check ^= buffer[i];
    }
    if (check == buffer[BLUETOOTH_PACKET_SIZE - 1] &&
        buffer[0] == TRAINER_FRAME) {
      processTrainerFrame(buffer);
    }
    dataState = STATE_DATA_IDLE;
  }
}

void Bluetooth::pushByte(uint8_t byte)
{
  crc ^= byte;
  if (byte == START_STOP || byte == BYTE_STUFF) {
    buffer[bufferIndex++] = BYTE_STUFF;
    byte ^= STUFF_MASK;
  }
  buffer[bufferIndex++] = byte;
}

void Bluetooth::sendTrainer()
{
  int16_t PPM_range = g_model.extendedLimits ? 640 * 2 : 512 * 2;

  int firstCh = g_model.trainerData.channelsStart;
  int lastCh = firstCh + BLUETOOTH_TRAINER_CHANNELS;

  uint8_t *cur = buffer;
  bufferIndex = 0;
  crc = 0x00;

  buffer[bufferIndex++] = START_STOP;
  pushByte(TRAINER_FRAME);
  for (int channel = firstCh; channel < lastCh; channel += 2, cur += 3) {
    uint16_t channelValue1 =
        PPM_CH_CENTER(channel) +
        limit((int16_t)-PPM_range, channelOutputs[channel],
              (int16_t)PPM_range) /
            2;
    uint16_t channelValue2 =
        PPM_CH_CENTER(channel + 1) +
        limit((int16_t)-PPM_range, channelOutputs[channel + 1],
              (int16_t)PPM_range) /
            2;
    pushByte(channelValue1 & 0x00ff);
    pushByte(((channelValue1 & 0x0f00) >> 4) +
             ((channelValue2 & 0x00f0) >> 4));
    pushByte(((channelValue2 & 0x000f) << 4) +
             ((channelValue2 & 0x0f00) >> 8));
  }
  pushByte(crc);
  buffer[bufferIndex++] = START_STOP;

  write(buffer, bufferIndex);
  bufferIndex = 0;
}

void Bluetooth::forwardTelemetry(const uint8_t *packet)
{
  crc = 0x00;

  buffer[bufferIndex++] = START_STOP;
  for (uint8_t i = 0; i < sizeof(SportTelemetryPacket); i++) {
    pushByte(packet[i]);
  }
  pushByte(crc);
  buffer[bufferIndex++] = START_STOP;

  if (bufferIndex >= 2 * FRSKY_SPORT_PACKET_SIZE) {
    write(buffer, bufferIndex);
    bufferIndex = 0;
  }
}

void Bluetooth::receiveTrainer()
{
  uint8_t byte;

  while (true) {
    if (!bluetoothRead(&byte)) {
      return;
    }

    processTrainerByte(byte);
  }
}

void Bluetooth::wakeup()
{
  tmr10ms_t now = get_tmr10ms();
  if (now < wakeupTime) {
    return;
  }

  wakeupTime = now + BT_WAKEUP_DEFAULT_DELAY;

  if (state == BLUETOOTH_STATE_FLASH_FIRMWARE) {
    return;
  }

  if (!bluetoothShouldBeEnabled()) {
    bluetoothDisable();
    state = BLUETOOTH_STATE_OFF;
    wakeupTime = now + BT_WAKEUP_DISABLED_DELAY;
    return;
  }

  if (state == BLUETOOTH_STATE_OFF) {
    char name[LEN_BLUETOOTH_NAME + sizeof(FLAVOUR) + 1] = "";
    getDesiredName(name, sizeof(name));
    bluetoothInit(BLUETOOTH_DEFAULT_BAUDRATE, true);
    bluetoothSetName(name);
    bluetoothSetRole(getDesiredRole());
    state = BLUETOOTH_STATE_BAUDRATE_INIT;
    wakeupTime = now + BT_WAKEUP_DISABLED_DELAY;
    refreshLocalAddress();
    return;
  }

  bluetoothSetRole(getDesiredRole());
  refreshLocalAddress();
  wakeupTime += updateConnectionEvents();

  if (state == BLUETOOTH_STATE_BAUDRATE_INIT) {
    state = BLUETOOTH_STATE_IDLE;
  }

  if (state == BLUETOOTH_STATE_DISCOVER_REQUESTED) {
    bluetoothSetRole(BLUETOOTH_ROLE_CENTRAL);
    bluetoothClearDiscoveryResults();
    bluetoothStartDiscovery();
    state = BLUETOOTH_STATE_DISCOVER_SENT;
    return;
  }

  if (state == BLUETOOTH_STATE_DISCOVER_SENT ||
      state == BLUETOOTH_STATE_DISCOVER_START) {
    refreshDiscoveryResults();
    if (reusableBuffer.moduleSetup.bt.devicesCount > 0) {
      state = BLUETOOTH_STATE_DISCOVER_START;
    }
    if (bluetoothTakeDiscoveryFinished()) {
      refreshDiscoveryResults();
      state = BLUETOOTH_STATE_DISCOVER_END;
    }
    return;
  }

  if (state == BLUETOOTH_STATE_CLEAR_REQUESTED) {
    bluetoothDisconnectPeer();
    state = BLUETOOTH_STATE_IDLE;
    return;
  }

  if (state == BLUETOOTH_STATE_BIND_REQUESTED) {
    bluetoothSetRole(BLUETOOTH_ROLE_CENTRAL);
    if (bluetoothConnectToAddress(distantAddr)) {
      state = BLUETOOTH_STATE_CONNECT_SENT;
      wakeupTime = now + BT_WAKEUP_CONNECTING_DELAY;
    }
    else {
      state = BLUETOOTH_STATE_IDLE;
    }
    return;
  }

  if (state == BLUETOOTH_STATE_DISCONNECTED && distantAddr[0] &&
      g_eeGeneral.bluetoothMode == BLUETOOTH_TRAINER &&
      g_model.trainerData.mode == TRAINER_MODE_MASTER_BLUETOOTH) {
    if (bluetoothConnectToAddress(distantAddr)) {
      state = BLUETOOTH_STATE_CONNECT_SENT;
      wakeupTime = now + BT_WAKEUP_RECONNECT_DELAY;
      return;
    }
  }

  if (state == BLUETOOTH_STATE_CONNECTED) {
    if (g_eeGeneral.bluetoothMode == BLUETOOTH_TRAINER &&
        g_model.trainerData.mode == TRAINER_MODE_MASTER_BLUETOOTH) {
      receiveTrainer();
      wakeupTime = now + BT_TRAINER_ACTIVE_DELAY;
    }
    else if (g_eeGeneral.bluetoothMode == BLUETOOTH_TRAINER &&
             g_model.trainerData.mode == TRAINER_MODE_SLAVE_BLUETOOTH) {
      sendTrainer();
      wakeupTime = now + BT_TRAINER_ACTIVE_DELAY;
    }
  }
}

uint8_t Bluetooth::bootloaderChecksum(uint8_t command, const uint8_t *data,
                                      uint8_t size)
{
  uint8_t sum = command;
  for (uint8_t i = 0; i < size; i++) {
    sum += data[i];
  }
  return sum;
}

uint8_t Bluetooth::read(uint8_t *data, uint8_t size, uint32_t timeout)
{
  watchdogSuspend(timeout / 10);

  uint8_t len = 0;
  while (len < size) {
    uint32_t elapsed = 0;
    uint8_t byte;
    while (!bluetoothRead(&byte)) {
      if (elapsed++ >= timeout) {
        return len;
      }
      sleep_ms(1);
    }
    data[len++] = byte;
  }
  return len;
}

#define BLUETOOTH_ACK   0xCC
#define BLUETOOTH_NACK  0x33

const char * Bluetooth::bootloaderWaitCommandResponse(uint32_t timeout)
{
  uint8_t response[2];
  if (read(response, sizeof(response), timeout) != sizeof(response)) {
    return "Bluetooth timeout";
  }

  if (response[0] != 0x00) {
    return "Bluetooth error";
  }

  if (response[1] == BLUETOOTH_ACK || response[1] == BLUETOOTH_NACK) {
    return nullptr;
  }

  return "Bluetooth error";
}

const char * Bluetooth::bootloaderWaitResponseData(uint8_t *data, uint8_t size)
{
  uint8_t header[2];
  if (read(header, 2) != 2) {
    return "Bluetooth timeout";
  }

  uint8_t len = header[0] - 2;
  uint8_t checksum = header[1];

  if (len > size) {
    return "Bluetooth error";
  }

  if (read(data, len) != len) {
    return "Bluetooth timeout";
  }

  if (bootloaderChecksum(0, data, len) != checksum) {
    return "Bluetooth CRC error";
  }

  return nullptr;
}

const char * Bluetooth::bootloaderSetAutoBaud()
{
  uint8_t packet[2] = {0x55, 0x55};
  write(packet, sizeof(packet));
  return bootloaderWaitCommandResponse();
}

void Bluetooth::bootloaderSendCommand(uint8_t command, const void *data,
                                      uint8_t size)
{
  uint8_t packet[3] = {uint8_t(3 + size),
                       bootloaderChecksum(command, (uint8_t *)data, size),
                       command};
  write(packet, sizeof(packet));
  if (size > 0) {
    write((uint8_t *)data, size);
  }
}

void Bluetooth::bootloaderSendCommandResponse(uint8_t response)
{
  uint8_t packet[2] = {0x00, response};
  write(packet, sizeof(packet));
}

enum {
  CMD_DOWNLOAD = 0x21,
  CMD_GET_STATUS = 0x23,
  CMD_SEND_DATA = 0x24,
  CMD_SECTOR_ERASE = 0x26,
  CMD_GET_CHIP_ID = 0x28,
};

enum {
  CMD_RET_SUCCESS = 0x40,
};

constexpr uint32_t CC26XX_BOOTLOADER_SIZE = 0x00001000;
constexpr uint32_t CC26XX_FIRMWARE_BASE = CC26XX_BOOTLOADER_SIZE;
constexpr uint32_t CC26XX_PAGE_ERASE_SIZE = 0x1000;
constexpr uint32_t CC26XX_MAX_BYTES_PER_TRANSFER = 252;

const char * Bluetooth::bootloaderSendData(const uint8_t *data, uint8_t size)
{
  bootloaderSendCommand(CMD_SEND_DATA, data, size);
  return bootloaderWaitCommandResponse();
}

const char * Bluetooth::bootloaderReadStatus(uint8_t &status)
{
  bootloaderSendCommand(CMD_GET_STATUS);
  const char *result = bootloaderWaitCommandResponse();
  if (result)
    return result;
  result = bootloaderWaitResponseData(&status, 1);
  bootloaderSendCommandResponse(result == nullptr ? BLUETOOTH_ACK
                                                  : BLUETOOTH_NACK);
  return result;
}

const char * Bluetooth::bootloaderCheckStatus()
{
  uint8_t status;
  const char *result = bootloaderReadStatus(status);
  if (result)
    return result;
  if (status != CMD_RET_SUCCESS)
    return "Wrong status";
  return nullptr;
}

const char * Bluetooth::bootloaderEraseFlash(uint32_t start, uint32_t size)
{
  uint32_t address = start;
  uint32_t end = start + size;
  while (address < end) {
    uint32_t addressBigEndian = SWAP32(address);
    bootloaderSendCommand(CMD_SECTOR_ERASE, &addressBigEndian,
                          sizeof(addressBigEndian));
    const char *result = bootloaderWaitCommandResponse();
    if (result)
      return result;
    result = bootloaderCheckStatus();
    if (result)
      return result;
    address += CC26XX_PAGE_ERASE_SIZE;
  }
  return nullptr;
}

const char * Bluetooth::bootloaderStartWriteFlash(uint32_t start, uint32_t size)
{
  uint32_t cmdArgs[2] = {
      SWAP32(start),
      SWAP32(size),
  };
  bootloaderSendCommand(CMD_DOWNLOAD, cmdArgs, sizeof(cmdArgs));
  const char *result = bootloaderWaitCommandResponse();
  if (result)
    return result;
  result = bootloaderCheckStatus();
  if (result)
    return result;
  return result;
}

const char * Bluetooth::bootloaderWriteFlash(const uint8_t *data, uint32_t size)
{
  while (size > 0) {
    uint32_t len = min<uint32_t>(size, CC26XX_MAX_BYTES_PER_TRANSFER);
    const char *result = bootloaderSendData(data, len);
    if (result)
      return result;
    result = bootloaderCheckStatus();
    if (result)
      return result;
    data += len;
    size -= len;
  }
  return nullptr;
}

const char * Bluetooth::doFlashFirmware(const char *filename,
                                        ProgressHandler progressHandler)
{
  const char *result;
  FIL file;
  uint8_t buffer[CC26XX_MAX_BYTES_PER_TRANSFER * 4];
  UINT count;

  bootloaderSendCommand(0);
  result = bootloaderWaitCommandResponse(0);
  if (result)
    result = bootloaderSetAutoBaud();
  if (result)
    return result;

  bootloaderSendCommand(CMD_GET_CHIP_ID);
  result = bootloaderWaitCommandResponse();
  if (result)
    return result;
  uint8_t id[4];
  result = bootloaderWaitResponseData(id, 4);
  bootloaderSendCommandResponse(result == nullptr ? BLUETOOTH_ACK
                                                  : BLUETOOTH_NACK);

  if (f_open(&file, filename, FA_READ) != FR_OK) {
    return "Error opening file";
  }

  FrSkyFirmwareInformation *information = (FrSkyFirmwareInformation *)buffer;
  if (f_read(&file, buffer, sizeof(FrSkyFirmwareInformation), &count) != FR_OK ||
      count != sizeof(FrSkyFirmwareInformation)) {
    f_close(&file);
    return "Format error";
  }

  progressHandler(getBasename(filename), STR_FLASH_ERASE, 0, 0);

  result = bootloaderEraseFlash(CC26XX_FIRMWARE_BASE, information->size);
  if (result) {
    f_close(&file);
    return result;
  }

  uint32_t size = information->size;
  progressHandler(getBasename(filename), STR_FLASH_WRITE, 0, size);

  result = bootloaderStartWriteFlash(CC26XX_FIRMWARE_BASE, size);
  if (result)
    return result;

  uint32_t done = 0;
  while (1) {
    progressHandler(getBasename(filename), STR_FLASH_WRITE, done, size);
    if (f_read(&file, buffer, min<uint32_t>(sizeof(buffer), size - done),
               &count) != FR_OK) {
      f_close(&file);
      return "Error reading file";
    }
    result = bootloaderWriteFlash(buffer, count);
    if (result)
      return result;
    done += count;
    if (done >= size) {
      f_close(&file);
      return nullptr;
    }
  }
}

const char * Bluetooth::flashFirmware(const char *filename,
                                      ProgressHandler progressHandler)
{
  progressHandler(getBasename(filename), STR_MODULE_RESET, 0, 0);

  state = BLUETOOTH_STATE_FLASH_FIRMWARE;

  pulsesStop();

  bluetoothInit(BLUETOOTH_BOOTLOADER_BAUDRATE, true);
  watchdogSuspend(500);
  sleep_ms(1000);

  bluetoothInit(BLUETOOTH_BOOTLOADER_BAUDRATE, false);
  watchdogSuspend(500);
  sleep_ms(1000);

  const char *result = doFlashFirmware(filename, progressHandler);

  AUDIO_PLAY(AU_SPECIAL_SOUND_BEEP1);
  BACKLIGHT_ENABLE();

  if (result) {
    POPUP_WARNING(STR_FIRMWARE_UPDATE_ERROR, result);
  }
  else {
    POPUP_INFORMATION(STR_FIRMWARE_UPDATE_SUCCESS);
  }

  progressHandler(getBasename(filename), STR_MODULE_RESET, 0, 0);

  watchdogSuspend(500);
  sleep_ms(1000);

  state = BLUETOOTH_STATE_OFF;
  pulsesStart();

  return result;
}
