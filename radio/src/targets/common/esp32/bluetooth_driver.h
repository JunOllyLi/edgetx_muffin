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

#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "hal/serial_driver.h"

#define BLUETOOTH_BOOTLOADER_BAUDRATE   230400
#define BLUETOOTH_DEFAULT_BAUDRATE      115200
#define BLUETOOTH_FACTORY_BAUDRATE      57600
#define BT_TX_FIFO_SIZE    64
#define BT_RX_FIFO_SIZE    256

typedef enum BluetoothPlatformRole {
  BLUETOOTH_ROLE_OFF = 0,
  BLUETOOTH_ROLE_PERIPHERAL,
  BLUETOOTH_ROLE_CENTRAL,
} BluetoothPlatformRole;

#ifdef __cplusplus
extern "C" {
#endif

void bluetoothInit(uint32_t baudrate, bool enable);
void bluetoothWrite(const void* buffer, uint32_t length);
int bluetoothRead(uint8_t* data);
uint8_t bluetoothIsWriting();
void bluetoothDisable();

void bluetoothSetName(const char *name);
void bluetoothSetRole(BluetoothPlatformRole role);
bool bluetoothGetLocalAddress(char *addr, size_t len);
void bluetoothClearDiscoveryResults();
void bluetoothStartDiscovery();
uint8_t bluetoothGetDiscoveryResultCount();
bool bluetoothGetDiscoveryResult(uint8_t index, char *addr, size_t len);
bool bluetoothTakeDiscoveryFinished();
bool bluetoothConnectToAddress(const char *addr);
void bluetoothDisconnectPeer();
bool bluetoothTakeConnected(char *addr, size_t len);
bool bluetoothTakeDisconnected();
bool bluetoothIsConnected();
bool bluetoothEnsureHostStarted();
bool bluetoothIsHostStarted();
bool bluetoothIsHostSynced();

#ifdef __cplusplus
}
#endif

#define IS_BLUETOOTH_CHIP_PRESENT()     (true)
