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

#include "bluetooth_driver.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

extern "C" {
#include "sys/queue.h"
#include "esp_nimble_cfg.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "esp_central.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
void ble_store_config_init(void);
}

namespace {

constexpr const char *TAG = "BT-ESP32";
constexpr uint8_t MAX_DISCOVERED_DEVICES = 6;
constexpr uint16_t CCCD_UUID16 = 0x2902;
constexpr int DISCOVERY_TIME_MS = 4000;
constexpr int PEER_MAX_SERVICES = 64;
constexpr int PEER_MAX_CHARACTERISTICS = 64;
constexpr int PEER_MAX_DESCRIPTORS = 64;

struct DiscoveredDevice {
  ble_addr_t addr {};
  char text[13] {};
  bool used = false;
};

struct BtContext {
  bool initialized = false;
  bool hostStarted = false;
  bool synced = false;
  bool enabled = false;
  bool connected = false;
  bool notificationsEnabled = false;
  bool scanning = false;
  bool advertising = false;
  bool connecting = false;
  bool discoveryFinished = false;
  bool connectedEvent = false;
  bool disconnectedEvent = false;
  BluetoothPlatformRole role = BLUETOOTH_ROLE_OFF;
  uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
  uint16_t peerValueHandle = 0;
  uint16_t localValueHandle = 0;
  uint8_t ownAddrType = BLE_OWN_ADDR_PUBLIC;
  ble_addr_t targetAddr {};
  bool hasTargetAddr = false;
  ble_addr_t peerAddr {};
  char deviceName[32] = "muffin";
  char localAddr[13] {};
  char connectedAddr[13] {};
  std::array<uint8_t, 1024> rxFifo {};
  size_t rxHead = 0;
  size_t rxTail = 0;
  std::array<DiscoveredDevice, MAX_DISCOVERED_DEVICES> discovered {};
  SemaphoreHandle_t mutex = nullptr;
  StaticSemaphore_t mutexStorage {};
};

BtContext g_bt;

static ble_uuid128_t kBtServiceUuid = BLE_UUID128_INIT(
    0x54, 0x58, 0x45, 0x67, 0x64, 0x65, 0x54, 0x58,
    0x42, 0x4c, 0x55, 0x45, 0x54, 0x4f, 0x4f, 0x54);

static ble_uuid128_t kBtDataUuid = BLE_UUID128_INIT(
    0x54, 0x58, 0x45, 0x67, 0x64, 0x65, 0x54, 0x58,
    0x42, 0x4c, 0x55, 0x45, 0x44, 0x41, 0x54, 0x41);

void lockBt()
{
  if (g_bt.mutex == nullptr) {
    g_bt.mutex = xSemaphoreCreateMutexStatic(&g_bt.mutexStorage);
  }
  xSemaphoreTake(g_bt.mutex, portMAX_DELAY);
}

void unlockBt()
{
  if (g_bt.mutex != nullptr) {
    xSemaphoreGive(g_bt.mutex);
  }
}

void formatAddr(const ble_addr_t &addr, char *out)
{
  std::snprintf(out, 13, "%02X%02X%02X%02X%02X%02X", addr.val[5], addr.val[4],
                addr.val[3], addr.val[2], addr.val[1], addr.val[0]);
}

int countPeerCb(const peer *peer, void *arg)
{
  auto *count = static_cast<int *>(arg);
  ++(*count);
  return 0;
}

int peerCount()
{
  int count = 0;
  peer_traverse_all(countPeerCb, &count);
  return count;
}

bool parseAddr(const char *text, ble_addr_t &out)
{
  if (!text || std::strlen(text) != 12) {
    return false;
  }

  for (int i = 0; i < 6; ++i) {
    unsigned int value = 0;
    if (std::sscanf(&text[i * 2], "%2x", &value) != 1) {
      return false;
    }
    out.val[5 - i] = value;
  }

  return true;
}

void fifoPushByte(uint8_t byte)
{
  size_t next = (g_bt.rxHead + 1) % g_bt.rxFifo.size();
  if (next == g_bt.rxTail) {
    g_bt.rxTail = (g_bt.rxTail + 1) % g_bt.rxFifo.size();
  }
  g_bt.rxFifo[g_bt.rxHead] = byte;
  g_bt.rxHead = next;
}

void fifoPushBuffer(const void *data, size_t len)
{
  const auto *bytes = static_cast<const uint8_t *>(data);
  lockBt();
  for (size_t i = 0; i < len; ++i) {
    fifoPushByte(bytes[i]);
  }
  unlockBt();
}

int fifoPopByte(uint8_t *data)
{
  lockBt();
  if (g_bt.rxHead == g_bt.rxTail) {
    unlockBt();
    return 0;
  }

  *data = g_bt.rxFifo[g_bt.rxTail];
  g_bt.rxTail = (g_bt.rxTail + 1) % g_bt.rxFifo.size();
  unlockBt();
  return 1;
}

void clearDiscoveryList()
{
  for (auto &entry : g_bt.discovered) {
    entry.used = false;
    entry.text[0] = '\0';
  }
}

bool addDiscoveredDevice(const ble_addr_t &addr)
{
  char text[13];
  formatAddr(addr, text);

  for (const auto &entry : g_bt.discovered) {
    if (entry.used && std::strcmp(entry.text, text) == 0) {
      return false;
    }
  }

  for (auto &entry : g_bt.discovered) {
    if (!entry.used) {
      entry.used = true;
      entry.addr = addr;
      std::strcpy(entry.text, text);
      return true;
    }
  }

  return false;
}

bool findDiscoveredDevice(const char *addrText, ble_addr_t &addr)
{
  for (const auto &entry : g_bt.discovered) {
    if (entry.used && std::strcmp(entry.text, addrText) == 0) {
      addr = entry.addr;
      return true;
    }
  }
  return false;
}

void setLocalAddress()
{
  ble_addr_t addr {};
  if (ble_hs_id_copy_addr(g_bt.ownAddrType, addr.val, nullptr) == 0) {
    addr.type = g_bt.ownAddrType;
    formatAddr(addr, g_bt.localAddr);
  }
}

int gapEvent(struct ble_gap_event *event, void *arg);
void startAdvertising();
void stopAdvertising();
void startScan();
void stopScan();
void disconnectPeer();

int notifyAccess(uint16_t conn_handle, uint16_t attr_handle,
                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (len == 0) {
    return 0;
  }

  std::array<uint8_t, 255> buffer {};
  len = std::min<uint16_t>(len, buffer.size());
  os_mbuf_copydata(ctxt->om, 0, len, buffer.data());
  fifoPushBuffer(buffer.data(), len);
  return 0;
}

const struct ble_gatt_svc_def gattSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kBtServiceUuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]) {
                {
                    .uuid = &kBtDataUuid.u,
                    .access_cb = notifyAccess,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                             BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &g_bt.localValueHandle,
                },
                {
                    0,
                },
            },
    },
    {
        0,
    },
};

void ensureGattServer()
{
  static bool registered = false;
  if (registered) {
    return;
  }

  ble_svc_gatt_init();
  ble_gatts_count_cfg(gattSvcs);
  ble_gatts_add_svcs(gattSvcs);
  registered = true;
}

void hostTask(void *param)
{
  ESP_LOGI(TAG, "NimBLE host task started");
  nimble_port_run();
  vTaskDelete(nullptr);
}

void startHost()
{
  if (g_bt.hostStarted) {
    return;
  }

  ensureGattServer();

  ble_hs_cfg.reset_cb = [](int reason) { ESP_LOGW(TAG, "BLE reset: %d", reason); };
  ble_hs_cfg.sync_cb = []() {
    lockBt();
    g_bt.synced = true;
    if (ble_hs_id_infer_auto(0, &g_bt.ownAddrType) == 0) {
      ble_hs_util_ensure_addr(0);
      setLocalAddress();
    }
    bool enabled = g_bt.enabled;
    BluetoothPlatformRole role = g_bt.role;
    bool connecting = g_bt.connecting;
    unlockBt();

    ble_svc_gap_device_name_set(g_bt.deviceName);
    ble_store_config_init();

    if (!enabled) {
      return;
    }

    if (role == BLUETOOTH_ROLE_PERIPHERAL) {
      startAdvertising();
    }
    else if (role == BLUETOOTH_ROLE_CENTRAL && connecting) {
      startScan();
    }
  };
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), PEER_MAX_SERVICES,
            PEER_MAX_CHARACTERISTICS, PEER_MAX_DESCRIPTORS);
  ble_svc_gap_device_name_set(g_bt.deviceName);
  ble_store_config_init();

  nimble_port_freertos_init(hostTask);
  g_bt.hostStarted = true;
}

void stopAdvertising()
{
  if (g_bt.advertising) {
    ble_gap_adv_stop();
    g_bt.advertising = false;
  }
}

void startAdvertising()
{
  lockBt();
  bool ready = g_bt.enabled && g_bt.synced &&
               g_bt.role == BLUETOOTH_ROLE_PERIPHERAL && !g_bt.connected;
  unlockBt();
  if (!ready) {
    return;
  }

  ble_gap_adv_stop();

  ble_hs_adv_fields fields {};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.name = reinterpret_cast<const uint8_t *>(g_bt.deviceName);
  fields.name_len = std::strlen(g_bt.deviceName);
  fields.name_is_complete = 1;
  fields.uuids128 = &kBtServiceUuid;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  if (ble_gap_adv_set_fields(&fields) != 0) {
    return;
  }

  ble_gap_adv_params params {};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  if (ble_gap_adv_start(g_bt.ownAddrType, nullptr, BLE_HS_FOREVER, &params,
                        gapEvent, nullptr) == 0) {
    lockBt();
    g_bt.advertising = true;
    unlockBt();
  }
}

void stopScan()
{
  if (g_bt.scanning) {
    ble_gap_disc_cancel();
    g_bt.scanning = false;
  }
}

void maybeConnectDiscovered(const ble_gap_disc_desc *disc)
{
  lockBt();
  if (!g_bt.connecting || !g_bt.hasTargetAddr) {
    unlockBt();
    return;
  }

  bool match = disc->addr.type == g_bt.targetAddr.type &&
               std::memcmp(disc->addr.val, g_bt.targetAddr.val,
                           sizeof(disc->addr.val)) == 0;
  unlockBt();
  if (!match) {
    return;
  }

  ble_gap_disc_cancel();
  if (ble_gap_connect(g_bt.ownAddrType, &disc->addr, 30000, nullptr, gapEvent,
                      nullptr) == 0) {
    lockBt();
    g_bt.scanning = false;
    unlockBt();
  }
}

void startScan()
{
  lockBt();
  bool ready = g_bt.enabled && g_bt.synced &&
               g_bt.role == BLUETOOTH_ROLE_CENTRAL && !g_bt.connected;
  unlockBt();
  if (!ready) {
    return;
  }

  ble_gap_disc_cancel();
  ble_gap_disc_params params {};
  params.filter_duplicates = 1;
  params.passive = 1;

  if (ble_gap_disc(g_bt.ownAddrType, DISCOVERY_TIME_MS, &params, gapEvent,
                   nullptr) == 0) {
    lockBt();
    g_bt.scanning = true;
    unlockBt();
  }
}

void disconnectPeer()
{
  if (g_bt.connected) {
    ble_gap_terminate(g_bt.connHandle, BLE_ERR_REM_USER_CONN_TERM);
  }
}

int onClientSubscribeComplete(uint16_t conn_handle, const ble_gatt_error *error,
                              struct ble_gatt_attr *attr, void *arg)
{
  if (error->status != 0) {
    ESP_LOGE(TAG, "subscribe write failed: %d", error->status);
    return 0;
  }

  ble_gap_conn_desc desc {};
  if (ble_gap_conn_find(conn_handle, &desc) == 0) {
    lockBt();
    formatAddr(desc.peer_id_addr, g_bt.connectedAddr);
    g_bt.connectedEvent = true;
    unlockBt();
  }
  return 0;
}

void onDiscoveryComplete(const peer *peer, int status, void *arg)
{
  if (status != 0 || peer == nullptr) {
    if (peer != nullptr) {
      ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return;
  }

  const peer_chr *chr = peer_chr_find_uuid(peer, &kBtServiceUuid.u, &kBtDataUuid.u);
  ble_uuid16_t cccdUuid = BLE_UUID16_INIT(CCCD_UUID16);
  const peer_dsc *cccd =
      peer_dsc_find_uuid(peer, &kBtServiceUuid.u, &kBtDataUuid.u, &cccdUuid.u);
  if (chr == nullptr || cccd == nullptr) {
    ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return;
  }

  lockBt();
  g_bt.peerValueHandle = chr->chr.val_handle;
  unlockBt();

  uint8_t cccdValue[2] = {0x01, 0x00};
  ble_gattc_write_flat(peer->conn_handle, cccd->dsc.handle, cccdValue,
                       sizeof(cccdValue), onClientSubscribeComplete, nullptr);
}

int gapEvent(struct ble_gap_event *event, void *arg)
{
  switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
      ble_hs_adv_fields fields {};
      if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                  event->disc.length_data) != 0) {
        return 0;
      }

      bool hasService = false;
      for (int i = 0; i < fields.num_uuids128; ++i) {
        if (ble_uuid_cmp(&fields.uuids128[i].u, &kBtServiceUuid.u) == 0) {
          hasService = true;
          break;
        }
      }
      if (!hasService) {
        return 0;
      }

      lockBt();
      addDiscoveredDevice(event->disc.addr);
      unlockBt();
      maybeConnectDiscovered(&event->disc);
      return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
      lockBt();
      g_bt.scanning = false;
      if (!g_bt.connecting) {
        g_bt.discoveryFinished = true;
      }
      unlockBt();
      return 0;

    case BLE_GAP_EVENT_CONNECT: {
      ble_gap_conn_desc desc {};

      lockBt();
      g_bt.scanning = false;
      unlockBt();

      if (event->connect.status != 0) {
        lockBt();
        g_bt.connecting = false;
        g_bt.discoveryFinished = true;
        unlockBt();
        return 0;
      }

      lockBt();
      g_bt.connected = true;
      g_bt.connecting = false;
      g_bt.connHandle = event->connect.conn_handle;
      unlockBt();

      ble_gap_conn_find(event->connect.conn_handle, &desc);
      lockBt();
      g_bt.peerAddr = desc.peer_id_addr;
      if (g_bt.role == BLUETOOTH_ROLE_PERIPHERAL) {
        formatAddr(desc.peer_id_addr, g_bt.connectedAddr);
        g_bt.connectedEvent = true;
      }
      unlockBt();

      ESP_LOGI(TAG, "Adding peer for conn_handle=%d active_peers=%d max=%d",
               event->connect.conn_handle, peerCount(),
               MYNEWT_VAL(BLE_MAX_CONNECTIONS));
      int rc = peer_add(event->connect.conn_handle);
      if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add peer; rc=%d active_peers=%d max=%d", rc,
                 peerCount(), MYNEWT_VAL(BLE_MAX_CONNECTIONS));
      }
      else {
        ESP_LOGI(TAG, "Added peer for conn_handle=%d active_peers=%d max=%d",
                 event->connect.conn_handle, peerCount(),
                 MYNEWT_VAL(BLE_MAX_CONNECTIONS));
      }
      if (g_bt.role == BLUETOOTH_ROLE_CENTRAL) {
        peer_disc_all(event->connect.conn_handle, onDiscoveryComplete, nullptr);
      }
      return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT:
      lockBt();
      g_bt.connected = false;
      g_bt.notificationsEnabled = false;
      g_bt.connHandle = BLE_HS_CONN_HANDLE_NONE;
      g_bt.peerValueHandle = 0;
      g_bt.connectedAddr[0] = '\0';
      g_bt.connectedEvent = false;
      g_bt.disconnectedEvent = true;
      unlockBt();
      ESP_LOGI(TAG, "Deleting peer for conn_handle=%d active_peers=%d max=%d",
               event->disconnect.conn.conn_handle, peerCount(),
               MYNEWT_VAL(BLE_MAX_CONNECTIONS));
      peer_delete(event->disconnect.conn.conn_handle);
      ESP_LOGI(TAG, "Deleted peer for conn_handle=%d active_peers=%d max=%d",
               event->disconnect.conn.conn_handle, peerCount(),
               MYNEWT_VAL(BLE_MAX_CONNECTIONS));
      if (g_bt.enabled && g_bt.role == BLUETOOTH_ROLE_PERIPHERAL) {
        startAdvertising();
      }
      return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
      if (event->subscribe.attr_handle == g_bt.localValueHandle) {
        lockBt();
        g_bt.notificationsEnabled = event->subscribe.cur_notify;
        unlockBt();
      }
      return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
      if (event->notify_rx.om != nullptr) {
        uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (len > 0) {
          std::array<uint8_t, 255> buffer {};
          len = std::min<uint16_t>(len, buffer.size());
          os_mbuf_copydata(event->notify_rx.om, 0, len, buffer.data());
          fifoPushBuffer(buffer.data(), len);
        }
      }
      return 0;

    default:
      return 0;
  }
}

void writeBleData(const void *buffer, uint32_t len)
{
  if (!g_bt.connected || len == 0) {
    return;
  }

  if (g_bt.role == BLUETOOTH_ROLE_CENTRAL) {
    if (g_bt.peerValueHandle != 0) {
      ble_gattc_write_no_rsp_flat(g_bt.connHandle, g_bt.peerValueHandle, buffer,
                                  len);
    }
  }
  else if (g_bt.notificationsEnabled && g_bt.localValueHandle != 0) {
    os_mbuf *om = ble_hs_mbuf_from_flat(buffer, len);
    ble_gatts_notify_custom(g_bt.connHandle, g_bt.localValueHandle, om);
  }
}

}  // namespace

void bluetoothInit(uint32_t baudrate, bool enable)
{
  (void)baudrate;

  bluetoothEnsureHostStarted();

  lockBt();
  g_bt.enabled = enable;
  unlockBt();

  if (enable && g_bt.synced) {
    setLocalAddress();
  }
}

void bluetoothDisable()
{
  lockBt();
  g_bt.enabled = false;
  g_bt.connecting = false;
  g_bt.discoveryFinished = false;
  g_bt.connectedEvent = false;
  g_bt.disconnectedEvent = false;
  g_bt.rxHead = 0;
  g_bt.rxTail = 0;
  clearDiscoveryList();
  unlockBt();

  stopScan();
  stopAdvertising();
  disconnectPeer();
}

void bluetoothWrite(const void *buffer, uint32_t len)
{
  if (!g_bt.enabled || buffer == nullptr || len == 0) {
    return;
  }

  lockBt();
  bool connected = g_bt.connected;
  unlockBt();
  if (connected) {
    writeBleData(buffer, len);
  }
}

int bluetoothRead(uint8_t *data)
{
  return fifoPopByte(data);
}

uint8_t bluetoothIsWriting()
{
  return false;
}

void bluetoothSetName(const char *name)
{
  if (!name || !name[0]) {
    return;
  }

  lockBt();
  std::strncpy(g_bt.deviceName, name, sizeof(g_bt.deviceName) - 1);
  g_bt.deviceName[sizeof(g_bt.deviceName) - 1] = '\0';
  unlockBt();

  if (g_bt.synced) {
    ble_svc_gap_device_name_set(g_bt.deviceName);
    if (g_bt.role == BLUETOOTH_ROLE_PERIPHERAL) {
      stopAdvertising();
      startAdvertising();
    }
  }
}

void bluetoothSetRole(BluetoothPlatformRole role)
{
  lockBt();
  if (g_bt.role == role) {
    unlockBt();
    return;
  }
  g_bt.role = role;
  unlockBt();

  stopScan();
  stopAdvertising();

  if (role == BLUETOOTH_ROLE_PERIPHERAL) {
    startAdvertising();
  }
}

bool bluetoothGetLocalAddress(char *addr, size_t len)
{
  if (!addr || len < 13) {
    return false;
  }

  lockBt();
  bool valid = g_bt.localAddr[0] != '\0';
  if (valid) {
    std::strncpy(addr, g_bt.localAddr, len - 1);
    addr[len - 1] = '\0';
  }
  unlockBt();
  return valid;
}

void bluetoothClearDiscoveryResults()
{
  lockBt();
  clearDiscoveryList();
  g_bt.discoveryFinished = false;
  unlockBt();
}

void bluetoothStartDiscovery()
{
  lockBt();
  g_bt.connecting = false;
  g_bt.hasTargetAddr = false;
  g_bt.discoveryFinished = false;
  unlockBt();
  startScan();
}

uint8_t bluetoothGetDiscoveryResultCount()
{
  uint8_t count = 0;
  lockBt();
  for (const auto &entry : g_bt.discovered) {
    if (entry.used) {
      ++count;
    }
  }
  unlockBt();
  return count;
}

bool bluetoothGetDiscoveryResult(uint8_t index, char *addr, size_t len)
{
  if (!addr || len < 13) {
    return false;
  }

  bool found = false;
  lockBt();
  uint8_t current = 0;
  for (const auto &entry : g_bt.discovered) {
    if (!entry.used) {
      continue;
    }
    if (current == index) {
      std::strncpy(addr, entry.text, len - 1);
      addr[len - 1] = '\0';
      found = true;
      break;
    }
    ++current;
  }
  unlockBt();
  return found;
}

bool bluetoothTakeDiscoveryFinished()
{
  lockBt();
  bool ready = g_bt.discoveryFinished;
  g_bt.discoveryFinished = false;
  unlockBt();
  return ready;
}

bool bluetoothConnectToAddress(const char *addrText)
{
  ble_addr_t addr {};
  if (!findDiscoveredDevice(addrText, addr)) {
    if (!parseAddr(addrText, addr)) {
      return false;
    }
    addr.type = BLE_ADDR_PUBLIC;
  }

  lockBt();
  g_bt.targetAddr = addr;
  g_bt.hasTargetAddr = true;
  g_bt.connecting = true;
  g_bt.discoveryFinished = false;
  unlockBt();

  startScan();
  return true;
}

void bluetoothDisconnectPeer()
{
  lockBt();
  g_bt.connecting = false;
  g_bt.hasTargetAddr = false;
  unlockBt();
  disconnectPeer();
}

bool bluetoothTakeConnected(char *addr, size_t len)
{
  lockBt();
  bool ready = g_bt.connectedEvent;
  if (ready) {
    if (addr && len) {
      std::strncpy(addr, g_bt.connectedAddr, len - 1);
      addr[len - 1] = '\0';
    }
    g_bt.connectedEvent = false;
  }
  unlockBt();
  return ready;
}

bool bluetoothTakeDisconnected()
{
  lockBt();
  bool ready = g_bt.disconnectedEvent;
  g_bt.disconnectedEvent = false;
  unlockBt();
  return ready;
}

bool bluetoothIsConnected()
{
  lockBt();
  bool connected = g_bt.connected;
  unlockBt();
  return connected;
}

bool bluetoothEnsureHostStarted()
{
  if (g_bt.mutex == nullptr) {
    g_bt.mutex = xSemaphoreCreateMutexStatic(&g_bt.mutexStorage);
  }

  if (!g_bt.initialized) {
    startHost();
    g_bt.initialized = true;
  }

  return g_bt.hostStarted;
}

bool bluetoothIsHostStarted()
{
  lockBt();
  bool started = g_bt.hostStarted;
  unlockBt();
  return started;
}

bool bluetoothIsHostSynced()
{
  lockBt();
  bool synced = g_bt.synced;
  unlockBt();
  return synced;
}
