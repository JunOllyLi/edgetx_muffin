/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "esp_log.h"
#include "nvs_flash.h"
/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "modlog/modlog.h"
#include "esp_central.h"

struct ble_hs_adv_fields;
struct ble_gap_conn_desc;
struct ble_hs_cfg;
union ble_store_value;
union ble_store_key;

TaskHandle_t pwrup_task_handle = NULL;

static SemaphoreHandle_t mutex_handle = NULL;
static StaticSemaphore_t mutex_struct;

#define TAG "POWERUP"
static ble_uuid128_t PwrUpUUID = BLE_UUID128_INIT(
    0xD6, 0x8C, 0x76, 0x00, 0xB3, 0x26, 0x17, 0xA1, 0xD9, 0x40,
    0x71, 0xF1,
    0x0E, 0x81, 0xC3, 0x86);

static ble_uuid128_t THRcharUUID = BLE_UUID128_INIT(
    0xD6, 0x8C, 0x76, 0x00, 0xB3, 0x26, 0x17, 0xA1, 0xD9, 0x40,
    0x10, 0x00,
    0x0E, 0x81, 0xC3, 0x86);

static ble_uuid128_t RDRcharUUID = BLE_UUID128_INIT(
    0xD6, 0x8C, 0x76, 0x00, 0xB3, 0x26, 0x17, 0xA1, 0xD9, 0x40,
    0x21, 0x00,
    0x0E, 0x81, 0xC3, 0x86);

static const struct peer *g_pwrup = NULL;
static const struct peer_chr *thr = NULL;
static const struct peer_chr *rdr = NULL;

static const char *tag = TAG;
static int blecent_gap_event(struct ble_gap_event *event, void *arg);

void ble_store_config_init(void);

static uint8_t current_thr = 0U;
static int8_t current_rdr = 0U;

static int
pwrup_on_write(uint16_t conn_handle, const struct ble_gatt_error *error,
                 struct ble_gatt_attr *attr, void *arg)
{
    xTaskNotifyGive(pwrup_task_handle);
    return 0;
}

void ble_write_pwrup_thottle(uint8_t data) {
    current_thr = data;
}

void ble_write_pwrup_rudder(int8_t data) {
    current_rdr = data;
}

void task_pwrup(void * pdata) {
    int next_send = 0;

    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        xSemaphoreTake(mutex_handle, portMAX_DELAY);
        switch (next_send) {
        case 0:
            next_send = 1;
            if ((NULL != g_pwrup) && (NULL != thr)) {
                ble_gattc_write_flat(g_pwrup->conn_handle, thr->chr.val_handle, &current_thr, 1, pwrup_on_write, NULL);
            }
            break;
        case 1:
            next_send = 0;
            if ((NULL != g_pwrup) && (NULL != rdr)) {
                ble_gattc_write_flat(g_pwrup->conn_handle, rdr->chr.val_handle, &current_rdr, 1, pwrup_on_write, NULL);
            }
            break;
        }
        xSemaphoreGive(mutex_handle);
    }
}
/**
 * Called when service discovery of the specified peer has completed.
 */
static void
blecent_on_disc_complete(const struct peer *peer, int status, void *arg)
{

    if (status != 0) {
        /* Service discovery failed.  Terminate the connection. */
        ESP_LOGE(TAG, "Error: Service discovery failed; status=%d "
                    "conn_handle=%d\n", status, peer->conn_handle);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    g_pwrup = peer;

    thr = peer_chr_find_uuid(peer, &PwrUpUUID.u, &THRcharUUID.u);
    rdr = peer_chr_find_uuid(peer, &PwrUpUUID.u, &RDRcharUUID.u);

    if ((NULL == thr) || (NULL == rdr)) {
        ESP_LOGE(TAG, "Failed to find the service/charactors");
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        g_pwrup = NULL;
        thr = NULL;
        rdr = NULL;
        return;
    } else {
        xTaskNotifyGive(pwrup_task_handle);
    }
    /* Service discovery has completed successfully.  Now we have a complete
     * list of services, characteristics, and descriptors that the peer
     * supports.
     */
    ESP_LOGI(TAG, "Service discovery complete; status=%d "
                "conn_handle=%d\n", status, peer->conn_handle);

    /* Now user can perform GATT procedures against the peer: read,
     * write, and subscribe to notifications.
     */
}

/**
 * Initiates the GAP general discovery procedure.
 */
static void
blecent_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    int rc;

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Tell the controller to filter duplicates; we don't want to process
     * repeated advertisements from the same device.
     */
    disc_params.filter_duplicates = 1;

    /**
     * Perform a passive scan.  I.e., don't send follow-up scan requests to
     * each advertiser.
     */
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      blecent_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error initiating GAP discovery procedure; rc=%d\n",
                    rc);
    }
}

/**
 * Indicates whether we should try to connect to the sender of the specified
 * advertisement.  The function returns a positive result if the device
 * advertises connectability and support for the Alert Notification service.
 */
static int
blecent_should_connect(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    int rc;
    int i;

    /* The device has to be advertising connectability. */
    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
            disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {

        return 0;
    }

    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0) {
        return rc;
    }

    for (i = 0; i < fields.num_uuids128; i++) {
        char buf[BLE_UUID_STR_LEN];
        ESP_LOGI(TAG, "UUID128: %s", ble_uuid_to_str(&fields.uuids128[i].u, buf));
        if (0 == ble_uuid_cmp(&PwrUpUUID.u, &fields.uuids128[i].u)) {
            ESP_LOGI(TAG, "Found POWERUP service");
            return 1;
        }
    }
    return 0;
}

/**
 * Connects to the sender of the specified advertisement if it looks
 * interesting.  A device is "interesting" if it advertises connectability and
 * support for the Alert Notification service.
 */
static void
blecent_connect_if_interesting(void *disc)
{
    uint8_t own_addr_type;
    int rc;
    ble_addr_t *addr;

    /* Don't do anything if we don't care about this advertiser. */
    if (!blecent_should_connect((struct ble_gap_disc_desc *)disc)) {
        return;
    }

    /* Scanning must be stopped before a connection can be initiated. */
    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        ESP_LOGD(TAG, "Failed to cancel scan; rc=%d\n", rc);
        return;
    }

    /* Figure out address to use for connect (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Try to connect the the advertiser.  Allow 30 seconds (30000 ms) for
     * timeout.
     */
    addr = &((struct ble_gap_disc_desc *)disc)->addr;

    rc = ble_gap_connect(own_addr_type, addr, 30000, NULL,
                         blecent_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error: Failed to connect to device; addr_type=%d "
                    "addr=%s; rc=%d\n",
                    addr->type, addr_str(addr->val), rc);
        return;
    }
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that is
 * established.  blecent uses the same callback for all connections.
 *
 * @param event                 The event being signalled.
 * @param arg                   Application-specified argument; unused by
 *                              blecent.
 *
 * @return                      0 if the application successfully handled the
 *                              event; nonzero on failure.  The semantics
 *                              of the return code is specific to the
 *                              particular GAP event being signalled.
 */
static int
blecent_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;
    int rc;
    peer_disc_fn *disc_cb = NULL;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                     event->disc.length_data);
        if (rc != 0) {
            return 0;
        }

        /* An advertisment report was received during GAP discovery. */
        print_adv_fields(&fields);

        /* Try to connect to the advertiser if it looks interesting. */
        blecent_connect_if_interesting(&event->disc);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        if (event->connect.status == 0) {
            /* Connection successfully established. */
            ESP_LOGI(TAG, "Connection established ");

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);
            ESP_LOGI(TAG, "\n");

            disc_cb = blecent_on_disc_complete;

            /* Remember peer. */
            rc = peer_add(event->connect.conn_handle);
            if (rc != 0) {
                ESP_LOGE(TAG, "Failed to add peer; rc=%d\n", rc);
                return 0;
            }

            /* Perform service discovery. */
            rc = peer_disc_all(event->connect.conn_handle,
                               disc_cb, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "Failed to discover services; rc=%d\n", rc);
                return 0;
            }
        } else {
            /* Connection attempt failed; resume scanning. */
            ESP_LOGE(TAG, "Error: Connection failed; status=%d\n",
                        event->connect.status);
            blecent_scan();
        }

        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated. */
        xSemaphoreTake(mutex_handle, portMAX_DELAY);
        g_pwrup = NULL;
        thr = NULL;
        rdr = NULL;
        xSemaphoreGive(mutex_handle);
        ESP_LOGI(TAG, "disconnect; reason=%d ", event->disconnect.reason);
        print_conn_desc(&event->disconnect.conn);
        ESP_LOGI(TAG, "\n");

        /* Forget about peer. */
        peer_delete(event->disconnect.conn.conn_handle);

        /* Resume scanning. */
        blecent_scan();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "discovery complete; reason=%d\n",
                    event->disc_complete.reason);
        return 0;

    default:
        return 0;
    }
}

static void
blecent_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d\n", reason);
}

static void
blecent_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Begin scanning for a peripheral to connect to. */
    blecent_scan();
}

void blecent_host_task(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    vTaskDelete(NULL);
}

static StaticTask_t task_struct;
EXT_RAM_BSS_ATTR static StackType_t task_stack[NIMBLE_HS_STACK_SIZE];

void
esp_start_ble_scan(void)
{
    int rc;
    g_pwrup = NULL;
    thr = NULL;
    rdr = NULL;

    if (NULL == mutex_handle) {
        mutex_handle = xSemaphoreCreateBinaryStatic(&mutex_struct);
        xSemaphoreGive(mutex_handle);
    }

    /* Configure the host. */
    ble_hs_cfg.reset_cb = blecent_on_reset;
    ble_hs_cfg.sync_cb = blecent_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Initialize data structures to track connected peers. */
    rc = peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64);
    assert(rc == 0);

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("blecent-powerup");
    assert(rc == 0);

    /* XXX Need to have template for store */
    ble_store_config_init();

    xTaskCreateStaticPinnedToCore(blecent_host_task, "nimble_host", NIMBLE_HS_STACK_SIZE,
            NULL, (configMAX_PRIORITIES - 4), task_stack, &task_struct, NIMBLE_CORE);
}

void esp_end_ble(void) {
    g_pwrup = NULL;
    thr = NULL;
    rdr = NULL;
    nimble_port_stop();
}