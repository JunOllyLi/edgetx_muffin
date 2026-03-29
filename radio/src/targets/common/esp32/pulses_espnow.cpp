/*
 * Copyright (C) OpenTX
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
 
#include "edgetx.h"

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
//#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_task.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include "rom/crc.h"
#include "esprc.h"
#include "esprc_packet.h"

#include "pulses_esp32.h"

static const char *TAG = "tx.cpp";
static xQueueHandle evtQueue;
static esp_now_peer_info_t rxPeer;
static DRAM_ATTR int16_t locChannelOutputs[MAX_OUTPUT_CHANNELS] = {0};
static QueueHandle_t xPulsesQueue;
static volatile TXPacket_t packet;
static volatile LinkState_t linkState = IDLE;
uint32_t volatile packSent = 0;
uint32_t volatile packAckn = 0;
uint32_t volatile sendPeriod = 0;
static bool volatile pulsesON = false;
static bool volatile paused = false;
static TXState_t volatile txState = PAUSED;

#define ESPNOW_STACK_SIZE (1024 * 4)
StackType_t espnow_stack[ESPNOW_STACK_SIZE];
StaticTask_t espnowTaskBuffer;

void packet_prepare()
{
    packet.type = DATA;
    packet.idx++;
    packet.crc = 0;
    xQueueReceive( xPulsesQueue, locChannelOutputs,0);
    memcpy((void *)packet.ch,locChannelOutputs,sizeof(packet.ch));
    packet.crc = crc16_le(0, (uint8_t const *) &packet, sizeof(packet));
}

void bind_packet_prepare()
{
    packet.type = BIND;
    packet.idx = g_model.moduleData[INTERNAL_MODULE].espnow.ch;
    xQueueReceive( xPulsesQueue, locChannelOutputs,0);
    memcpy((void *)packet.ch,locChannelOutputs,sizeof(packet.ch));
    packet.crc = 0;
    packet.crc = crc16_le(0, (uint8_t const *) &packet, sizeof(packet));
}

inline void process_data(Event_t &evt) {
    if (!memcmp(evt.mac_addr,rxPeer.peer_addr, sizeof(ESPNOW_ETH_ALEN))) {
        RXPacket_t *rp = (RXPacket_t *) evt.data;
        switch (rp->type){
        case ACK:
            if (rp->idx == packet.idx && rp->crc == packet.crc){
                linkState = GOTACKN;
                packAckn++;
            } else {
                ESP_LOGE(TAG, "Ack failed: idx: %d vs %d, crc: %d vs %d, ", rp->idx , packet.idx, rp->crc , packet.crc);
            }
            break;
        case TELE:
            break;
        default:
            ESP_LOGE(TAG,"Incorrect RX packet type: %d",rp->type);
            break;
        }
    } else {
        ESP_LOGE(TAG, "Wrong packet MAC: " MACSTR, MAC2STR(evt.mac_addr));
    }
}

inline void process_bind(Event_t &evt) {
    RXPacket_t *rp = (RXPacket_t *) evt.data;
    if (BIND == rp->type) {
        uint16_t crc = rp->crc;
        rp->crc = 0;
        rp->crc = crc16_le(0, (uint8_t const *) rp, sizeof(RXPacket_t));
        if(crc == rp->crc){
            ESP_LOGW(TAG, "Got bind MAC: " MACSTR, MAC2STR(evt.mac_addr));
            memcpy(rxPeer.peer_addr, evt.mac_addr, ESPNOW_ETH_ALEN);
            memcpy(g_model.moduleData[INTERNAL_MODULE].espnow.rx_mac_addr, rxPeer.peer_addr,  ESPNOW_ETH_ALEN);
            storageDirty(EE_MODEL);

            rxPeer.channel = g_model.moduleData[INTERNAL_MODULE].espnow.ch;
            rxPeer.ifidx = (wifi_interface_t)ESP_IF_WIFI_STA;
            rxPeer.encrypt = false;
            if (esp_now_is_peer_exist(rxPeer.peer_addr) == false) {
                esp_now_add_peer(&rxPeer);
            } else {
                esp_now_mod_peer(&rxPeer);
            }
            esp_wifi_set_channel(g_model.moduleData[INTERNAL_MODULE].espnow.ch, (wifi_second_chan_t)0);
            txState = PULSES;
        } else {
            ESP_LOGE(TAG,"RX packet CRC error: %d vs %d", crc, rp->crc);
        }
    } else {
        ESP_LOGE(TAG,"Incorrect RX packet type: %d", rp->type);
    }
}

static void tx_task(void *pvParameter)
{
    Event_t evt;
    //vTaskDelay(5000 / portTICK_RATE_MS);
    while (pulsesON) {
        if(xQueueReceive(evtQueue, &evt, TX_PERIOD_MS/portTICK_RATE_MS) == pdTRUE) {
            switch (evt.id) {
            case TX:
                ESP_LOGD(TAG, "TX: evt.status: %d", evt.status);
                if( ESP_NOW_SEND_SUCCESS == evt.status){
                    linkState = WAITACKN;
                    packSent++;
                } else {
                    linkState = SENDERR;
                }
                break;
            case RX:
                if ( sizeof(RXPacket_t) == evt.data_len ){
                    switch (txState) {
                    case PULSES:
                        process_data(evt);
                        break;
                    case BIND:
                        process_bind(evt);
                        break;
                    default:
                        break;
                    }
                } else {
                    ESP_LOGE(TAG,"Wrong RX packet size.");
                }
                free(evt.data);
                break;
            }
        } else {
            switch (txState) {
            case PULSES:
            {
                if(IS_BROADCAST_ADDR(rxPeer.peer_addr)) break;
                ESP_LOGD(TAG, "Sending data packet: linkState: %d", linkState);
                static uint64_t last_send_time = 0;
                uint64_t curr_time = esp_timer_get_time();
                sendPeriod = (curr_time - last_send_time)/1000;
                last_send_time = curr_time;
                if( linkState != QUEUED){
                    packet_prepare();
                    esp_err_t ret = esp_now_send(rxPeer.peer_addr, (const uint8_t *) &packet, sizeof(packet));
                    if ( ret != ESP_OK) {
                        ESP_LOGE(TAG, "Send txPacket error: %s", esp_err_to_name(ret));
                    } else {
                        linkState = QUEUED;
                    }
                }
                break;
            }
            case BINDING:
                if( linkState != QUEUED){
                    ESP_LOGD(TAG, "Sending bind packet: linkState: %d", linkState);
                    bind_packet_prepare();
                    esp_err_t ret = esp_now_send(broadcast_mac, (const uint8_t *) &packet, sizeof(packet));
                    if ( ret != ESP_OK) {
                        ESP_LOGE(TAG, "Send txPacket error: %s", esp_err_to_name(ret));
                    } else {
                        linkState = QUEUED;
                    }
                }
                break;
            default:
                break;
            }
        }
    }
    esp_now_deinit();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    stopWiFiESPNow();
    vTaskDelete(NULL);
}

static void send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Event_t evt;
  
    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }
  
    evt.id = TX;
    evt.status = status;
    if (xQueueSend(evtQueue, &evt, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "TX: Send queue failed.");
    }
}

static void recv_cb(const esp_now_recv_info_t * esp_now_info, const uint8_t *data, int len)
{
    Event_t evt;

    if (esp_now_info == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    evt.id = RX;
    memcpy(evt.mac_addr, esp_now_info->src_addr, ESPNOW_ETH_ALEN);
    evt.data = (uint8_t *)malloc(len);
    if (evt.data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(evt.data, data, len);
    evt.data_len = len;
    if (xQueueSend(evtQueue, &evt, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "RX: Send queue failed.");
        free(evt.data);
    }
}

esp_err_t initTX(){
    if (evtQueue == NULL) {
        evtQueue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(Event_t));
        if (evtQueue == NULL) {
            ESP_LOGE(TAG, "Create queue failed.");
            return ESP_FAIL;
        }
    }
  
    if(xPulsesQueue == NULL) {
        xPulsesQueue=xQueueCreate( 1, sizeof( locChannelOutputs ) );
        if(NULL == xPulsesQueue){
            ESP_LOGE(TAG, "Failed to create queue: xPulsesQueue!");
        }
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_now_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_now_register_send_cb(send_cb));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_now_register_recv_cb(recv_cb));
  
    rxPeer.ifidx = (wifi_interface_t)ESP_IF_WIFI_STA;
    rxPeer.encrypt = false;
    memcpy(rxPeer.peer_addr, broadcast_mac, ESPNOW_ETH_ALEN);
    rxPeer.channel = BIND_CH;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_now_add_peer(&rxPeer));
    rxPeer.channel = g_model.moduleData[INTERNAL_MODULE].espnow.ch;
    memcpy(rxPeer.peer_addr, g_model.moduleData[INTERNAL_MODULE].espnow.rx_mac_addr, ESPNOW_ETH_ALEN);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_now_add_peer(&rxPeer));

    linkState = IDLE;
    pulsesON = true;
    txState = PULSES;
    if (NULL == xTaskCreateStaticPinnedToCore(tx_task, "tx_task", ESPNOW_STACK_SIZE, NULL, ESP_TASK_PRIO_MAX-6, espnow_stack, &espnowTaskBuffer, PULSES_TASK_CORE)) {
        ESP_LOGE(TAG, "Failed to create tx_task");
    }
    return ESP_OK;
}

void pause_espnow(){
    ESP_LOGI(TAG, "Pause ESP-NOW");
    txState = PAUSED;
}

void init_bind_espnow(){
    ESP_LOGI(TAG, "ESP-NOW init binding.");
    esp_wifi_set_channel(BIND_CH, (wifi_second_chan_t)WIFI_SECOND_CHAN_NONE);
    txState = BINDING;
}

void stop_bind_espnow(){
    ESP_LOGI(TAG, "ESP-NOW stop binding.");
    esp_wifi_set_channel(g_model.moduleData[INTERNAL_MODULE].espnow.ch, (wifi_second_chan_t)0);
    txState = PULSES;
}

bool is_binding_espnow(){
    return BINDING == txState;
}

void resume_espnow(){
    ESP_LOGI(TAG, "Resume ESP-NOW");
    txState = PULSES;
}

void init_espnow()
{
    ESP_LOGI(TAG, "init_espnow");
    startWiFiESPNow();
    initTX();
}

static void* espNowInit(uint8_t module)
{
    (void)module;
    init_espnow();
    return (void *)1;
}

static void espNowDeInit(void* context)
{
    pulsesON = false;
}

static void espNowSetupPulses(void* context, int16_t* channels, uint8_t nChannels)
{
  // nothing to do
}

static void espNowSendPulses(void* context, uint8_t* buffer, int16_t* channels, uint8_t nChannels)
{
    if (xPulsesQueue){
        xQueueOverwrite( xPulsesQueue, channelOutputs );
    }
}

static int espNowGetByte(void* context, uint8_t* data)
{
#if 0
    return IntmoduleSerialDriver.getByte(context, data);
#endif
    return 0;
}

static void espNowProcessData(void* context, uint8_t data, uint8_t* buffer, uint8_t* len)
{
#if 0
    processMultiTelemetryData(data, INTERNAL_MODULE);
#endif
}

#include "hal/module_driver.h"

const etx_proto_driver_t EspNowDriver = {
    .protocol = PROTOCOL_CHANNELS_ESPNOW,
    .init = espNowInit,
    .deinit = espNowDeInit,
    //.setupPulses = espNowSetupPulses,
    .sendPulses = espNowSendPulses,
    //.getByte = espNowGetByte,
    //.processData = espNowProcessData,
};