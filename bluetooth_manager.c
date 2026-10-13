#include "bluetooth_private.h"


#include <bdaddr.h>
#include <message.h>
#include <panic.h>
#include <stdio.h>
#include <string.h>
#include <hfp.h>


#include "config.h"
#include "audio_manager.h"
#include "main.h"


/* Uncomment this to enable logging in this file */
/* #define DEBUG_ENABLED */
#include "log.h"


/**
 * General/Unlimited Inquiry Access Code (GIAC). For more information
 * visit https://www.bluetooth.com/specifications/assigned-numbers/baseband
 */
#define GIAC                        (0x9E8B33)


/**
 * Inquiry scan interval. Corresponds to 1.28s.
 *
 * See Bluetooth Specification v5.0 Vol 2, Part E Section 6.2.
 */
#define INQUIRY_SCAN_INTERVAL       (0x0800)


/**
 * Inquiry scan window. Corresponds to 11.25ms.
 *
 * See Bluetooth Specification v5.0 Vol 2, Part E Section 6.3.
 */
#define INQUIRY_SCAN_WINDOW         (0x0012)


/**
 * Page scan interval. Corresponds to 1.28s.
 *
 * See Bluetooth Specification v5.0 Vol 2, Part E Section 6.8.
 */
#define PAGE_SCAN_INTERVAL          (0x0800)


/**
 * Page scan window. Corresponds to 11.25ms.
 *
 * See Bluetooth Specification v5.0 Vol 2, Part E Section 6.9.
 */
#define PAGE_SCAN_WINDOW            (0x0012)


/** Instance of Bluetooth data. */
bluetooth_data_t bluetooth_data;


/** The Bluetooth task. */
static TaskData bluetooth_task;


/**
 * Delay between attempts to reconnect to the bonded headset, in ms. Was 0
 * (a failed attempt was retried at once); the gap also lets a connection
 * that is still closing finish first.
 */
#define RECONNECT_DELAY_MS          (1000)


/** Timer task for reconnecting to the bonded headset. */
static TaskData reconnect_task;


/**
 * Indicates the Vendor Id has been assigned by the Bluetooth Special Interest
 * Group.
 */
#define VENDOR_ID_SOURCE_BT_SIG    0x0001
/** Indicates the Vendor Id has been assigned by the USB Implementers Forum. */
#define VENDOR_ID_SOURCE_USB_IF    0x0002
#define VENDOR_ID_SOURCE_UNDEFINED 0xFFFF

/* If DEVICE_ID_CONST is defined, the registered Device Id service record will
 * use the following defines */
#define DEVICE_ID_VENDOR_ID_SOURCE VENDOR_ID_SOURCE_BT_SIG
/* As assigned by the appropriate source */
#define DEVICE_ID_VENDOR_ID        0x01B6
/* Customer specific                     */
#define DEVICE_ID_PRODUCT_ID       0x0018
/* Where 0xABCD = vAB.C.D                */
#define DEVICE_ID_BCD_VERSION      0x1912

#define DATA_EL_UINT_16(value)  (((value)>>8)&0xFF), ((value)&0xFF)

#define DEVICE_ID_OFFSET_VENDOR_ID_ATTR_VAL        18
#define DEVICE_ID_OFFSET_PRODUCT_ID_ATTR_VAL       24
#define DEVICE_ID_OFFSET_VERSION_ATTR_VAL          30
#define DEVICE_ID_OFFSET_VENDOR_ID_SOURCE_ATTR_VAL 41
#define DEVICE_ID_SERVICE_RECORD_SIZE              43

/**
 * Once MAX_INQUIRY_RESPONSE devices have been discovered the inquiry
 * process will terminate and the client task will be notified. According to
 * the connection library (ConnectionInquire() function in connection_no_ble.h),
 * the recommended value must be greater than 0 and not higher than 10.
 */
#define MAX_INQUIRY_RESPONSE        (9)

/**
 * Timeout to terminate the inquiry command. The allowed values are 0x01 to
 * 0x30. This corresponds to an inquiry timeout range of 1.28 to 61.44 seconds.
 * This timeout value is in units of 1.28 seconds. Current timeout is set to
 * 58.88 Seconds.
 */
#define INQUIRY_TIMEOUT             (0x2e)

/**
 * Major device class for Audio/Video. For more information
 * visit https://www.bluetooth.com/specifications/assigned-numbers/baseband
 */
#define AV_MAJOR_DEVICE_CLASS       (0x400)


static const uint8
        device_id_service_record_template[DEVICE_ID_SERVICE_RECORD_SIZE] =
{
    /* DataElUint16, ServiceClassIDList(0x0001) */
    0x09, 0x00, 0x01,
        /* DataElSeq 3 bytes */
        0x35, 0x03,
            /* DataElUuid16, PnPInformation(0x1200) */
            0x19, 0x12, 0x00,

    /* DataElUint16, SpecificationID(0x0200) */
    0x09, 0x02, 0x00,
        /* DataElUint16, 'Device Id Specification v1.3'(0x0103) */
        0x09, 0x01, 0x03,

    /* DataElUint16, VendorID(0x0201) */
    0x09, 0x02, 0x01,
        /* DataElUint16, DEVICE_ID_VENDOR_ID */
        0x09, DATA_EL_UINT_16(DEVICE_ID_VENDOR_ID),

    /* DataElUint16, ProductID(0x0202) */
    0x09, 0x02, 0x02,
        /* DataElUint16, DEVICE_ID_PRODUCT_ID */
        0x09, DATA_EL_UINT_16(DEVICE_ID_PRODUCT_ID),

    /* DataElUint16, Version(0x0203) */
    0x09, 0x02, 0x03,
        /* DataElUint16, DEVICE_ID_BCD_VERSION */
        0x09, DATA_EL_UINT_16(DEVICE_ID_BCD_VERSION),

    /* DataElUint16, PrimaryRecord(0x0204) */
    0x09, 0x02, 0x04,
        /* DataElBool, True(0x01) */
        0x28, 0x01,

    /* DataElUint16, VendorIdSource(0x0205) */
    0x09, 0x02, 0x05,
        /* DataElUint16, DEVICE_ID_VENDOR_ID_SOURCE */
        0x09, DATA_EL_UINT_16(DEVICE_ID_VENDOR_ID_SOURCE)
};


/* TODO - #12849: Check this code and the record/defines above. */
static void
bt_init_device_id_profile(void)
{
    ConnectionRegisterServiceRecord(bluetooth_data.task,
                                    DEVICE_ID_SERVICE_RECORD_SIZE,
                                    device_id_service_record_template);
}


/**
 * Start an Inquiry.
 */
static void
bluetooth_inquiry(void)
{
    PanicFalse(bluetooth_data.state == BLUETOOTH_STATE_SCANNING);

    /* Perform an inquiry (scan) for any Bluetooth device with Audio/Visual
     * major device class. */
    ConnectionInquire(bluetooth_data.task, GIAC, MAX_INQUIRY_RESPONSE,
                      INQUIRY_TIMEOUT, AV_MAJOR_DEVICE_CLASS);
}


/**
 * Set the current Bluetooth state.
 *
 * @param state The state to transition to.
 */
static void
bluetooth_set_state(bluetooth_state_t state);


/**
 * Try to connect to the bonded headset again after RECONNECT_DELAY_MS.
 * Pending attempts are merged into one.
 */
static void
bluetooth_schedule_reconnect(void)
{
    (void)MessageCancelAll(&reconnect_task, 0);
    MessageSendLater(&reconnect_task, 0, 0, RECONNECT_DELAY_MS);
}


/**
 * Reconnect timer (reconnect_task handler). While in CONNECTING there is
 * always either a connection attempt in progress or this timer pending, so
 * the Bluetooth Manager can't get stuck in CONNECTING.
 */
static void
reconnect_handler(Task task, MessageId id, Message message)
{
    (void)task;
    (void)id;
    (void)message;

    if (bluetooth_data.state != BLUETOOTH_STATE_CONNECTING ||
        bluetooth_data.mode != BLUETOOTH_MODE_SOURCE ||
        !bluetooth_data.on ||
        !bluetooth_data.prev_addr_headset_valid)
    {
        return;
    }

    /* AGHFP still busy (old connection closing): try again later */
    if (!bluetooth_manager_aghfp_connect(&bluetooth_data.prev_addr_headset))
        bluetooth_schedule_reconnect();
}


/**
 * Handle the CL_INIT_CFM event (resulting from ConnectionInit()).
 *
 * @param cfm   The connection library init confirm.
 */
static void
handle_cl_init_cfm(CL_INIT_CFM_T *cfm)
{
    static const uint32 giac = GIAC;

    LOG_DEBUG(("Connection init complete %u\n", cfm->status));
    PanicFalse(cfm->status == success);

    /* Set bluetooth state to idle */
    bluetooth_data.state = BLUETOOTH_STATE_IDLE;

    ConnectionSmSetSecurityMode(bluetooth_data.task, sec_mode4_ssp,
                                hci_enc_mode_pt_to_pt);
    /* Disable SDP security */
    ConnectionSmSetSecurityLevel(protocol_l2cap, 1,
                                 ssp_secl4_l0, TRUE, FALSE, FALSE);
    /* WAE - no ACL, Debug keys - off, Legacy pair key missing - on */
    ConnectionSmSecModeConfig(bluetooth_data.task, cl_sm_wae_acl_owner_none,
                              FALSE, TRUE);

    /* Set inquiry tx power and RSSI inquiry mode */
    /* ConnectionWriteInquiryTx(cfm->rssi.tx_power); */
    /* RSSI with EIR data */
    ConnectionWriteInquiryMode(bluetooth_data.task, inquiry_mode_eir);

    /* Initialise service record */
    bt_init_device_id_profile();

    /* Set up Inquiry/Paging parameters */
    ConnectionWritePagescanActivity(PAGE_SCAN_INTERVAL, PAGE_SCAN_WINDOW);
    ConnectionWriteInquiryscanActivity(INQUIRY_SCAN_INTERVAL,
                                       INQUIRY_SCAN_WINDOW);
    ConnectionWriteInquiryAccessCode(bluetooth_data.task, &giac, 1);

    /* Initialise HFP and AGHFP */
    bluetooth_hfp_init();
    bluetooth_aghfp_init();
}


void
bluetooth_manager_hfp_initialised(void)
{
    LOG_DEBUG(("HFP initialised - mode %u, bonded devices: %u\n",
               bluetooth_data.mode,
               bluetooth_data.prev_addr_phone_valid));

    if (bluetooth_data.mode != BLUETOOTH_MODE_SINK)
        return;

    if (bluetooth_data.on)
    {
        if (bluetooth_data.prev_addr_phone_valid)
            bluetooth_set_state(BLUETOOTH_STATE_CONNECTABLE);
        else
            bluetooth_set_state(BLUETOOTH_STATE_IDLE);
    }
    else
    {
        bluetooth_set_state(BLUETOOTH_STATE_IDLE);
    }
}


void
bluetooth_manager_aghfp_initialised(void)
{
    LOG_DEBUG(("AG HFP initialised - mode %u, bonded devices: %u\n",
               bluetooth_data.mode,
               bluetooth_data.prev_addr_headset_valid));

    if (bluetooth_data.mode != BLUETOOTH_MODE_SOURCE)
        return;

    if (bluetooth_data.on)
    {
        if (bluetooth_data.prev_addr_headset_valid)
            bluetooth_set_state(BLUETOOTH_STATE_CONNECTING);
        else
            bluetooth_set_state(BLUETOOTH_STATE_IDLE);
    }
    else
    {
        bluetooth_set_state(BLUETOOTH_STATE_IDLE);
    }
}


/**
 * Handle the CL_DM_ACL_OPENED_IND event.
 *
 * This indicates that an ACL has been opened.
 *
 * @param ind   The CL DM ACL opened indication.
 */
static void
handle_cl_dm_acl_opened_ind(CL_DM_ACL_OPENED_IND_T *ind)
{
    LOG_DEBUG(("CL_DM_ACL_OPENED_IND - "));

    if (ind->incoming)
        LOG_DEBUG(("incoming connection\n"));
    else
        LOG_DEBUG(("outgoing connection\n"));

    if (ind->status != hci_success)
    {
        LOG_DEBUG(("ACL open failed %u\n", ind->status));
        return;
    }

    if (bluetooth_data.state == BLUETOOTH_STATE_PAIRABLE)
    {
        LOG_DEBUG(("Device attempting to pair:\n"));
        LOG_BLUETOOTH_ADDR(ind->bd_addr.addr);
        bluetooth_set_state(BLUETOOTH_STATE_PAIRING);
    }
    else if (bluetooth_data.state == BLUETOOTH_STATE_CONNECTABLE)
    {
        LOG_DEBUG(("Device attempting to connect:\n"));
        LOG_BLUETOOTH_ADDR(ind->bd_addr.addr);
    }
}


/**
 * Handle the CL_DM_ACL_CLOSED_IND event.
 *
 * This indicates that an ACL has been closed.
 *
 * @param ind   The CL DM ACL closed indication.
 */
static void
handle_cl_dm_acl_closed_ind(CL_DM_ACL_CLOSED_IND_T *ind)
{
    /* Always printed. The reason (HCI error code) tells why the link went:
     * 0x08 = link supervision timeout (radio link lost - range,
     * interference, power saving), 0x13 = the headset disconnected (often a
     * protocol problem, e.g. a request left unanswered), 0x16 = disconnected
     * by this device. */
    log_print("BT: ACL closed [%04x:%02x:%06lx] reason 0x%02x, state %u\n",
              ind->taddr.addr.nap, ind->taddr.addr.uap, ind->taddr.addr.lap,
              ind->status, bluetooth_data.state);

    switch (bluetooth_data.state)
    {
    case BLUETOOTH_STATE_CONNECTABLE:
        /* We will get this event if we are connectable but a device tries to
         * pair and we reject it - we stay connectable */
        break;

    case BLUETOOTH_STATE_CONNECTED:
        if (bluetooth_data.mode == BLUETOOTH_MODE_SOURCE)
        {
            if (!bluetooth_data.prev_addr_headset_valid)
            {
                /* No bond to reconnect to */
                bluetooth_set_state(BLUETOOTH_STATE_IDLE);
            }
            else if (BdaddrIsSame(&ind->taddr.addr,
                                  &bluetooth_data.prev_addr_headset))
            {
                /* Connection lost - try to reconnect */
                bluetooth_set_state(BLUETOOTH_STATE_CONNECTING);
            }
            /* else another device's link closed - the headset is still
             * connected */
        }
        else
        {
            /* Connection lost - be available for a reconnection */
            bluetooth_set_state(BLUETOOTH_STATE_CONNECTABLE);
        }
        break;

    case BLUETOOTH_STATE_CONNECTING:
        /* The link went during the connection set-up. Previously nothing
         * happened here and, if no AGHFP_SLC_CONNECT_CFM failure followed
         * (SLC already up), no further attempt was ever made. */
        if (bluetooth_data.mode == BLUETOOTH_MODE_SOURCE)
            bluetooth_schedule_reconnect();
        break;

    case BLUETOOTH_STATE_PAIRING:
        if (bluetooth_data.mode == BLUETOOTH_MODE_SOURCE)
        {
            /* Pairing attempt failed - return to SCANNING */
            bluetooth_set_state(BLUETOOTH_STATE_SCANNING);
        }
        else
        {
            /* Pairing attempt failed - return to PAIRABLE */
            bluetooth_set_state(BLUETOOTH_STATE_PAIRABLE);
        }
        break;

    case BLUETOOTH_STATE_IDLE:
        /* If we are IDLE when a closure occurs then do nothing */
        break;

    default:
        break;
    }
}


/**
 * Handle the CL_SM_PIN_CODE_IND event.
 *
 * This is a request from the remote device for a PIN code.
 *
 * @param ind   The CL SM PIN code indication.
 */
static void
handle_cl_sm_pin_code_ind(CL_SM_PIN_CODE_IND_T *ind)
{
    const uint8 pin_code[4] = { 0, 0, 0, 0 };
    /* If a device asks for a PIN code respond with 0000 */
    ConnectionSmPinCodeResponse(&ind->taddr, sizeof(pin_code), pin_code);
}


/**
 * Handle the CL_SM_IO_CAPABILITY_REQ_IND event.
 *
 * This is a request from the remote device for the local IO capability.
 *
 * @param ind   The CL SM IO capability indication.
 */
static void
handle_cl_sm_io_capability_req_ind(CL_SM_IO_CAPABILITY_REQ_IND_T *ind)
{
    LOG_DEBUG(("CL_SM_IO_CAPABILITY_REQ_IND\n"));
    if (bluetooth_data.state == BLUETOOTH_STATE_PAIRING)
    {
        /* Accept with no IO, and MITM not required */
        ConnectionSmIoCapabilityResponse(&(ind->tpaddr),
                                         cl_sm_io_cap_no_input_no_output,
                                         mitm_not_required,
                                         TRUE, 0,
                                         oob_data_none, 0, 0);
    }
    else
    {
        LOG_DEBUG(("Rejecting pairing in state %u\n", bluetooth_data.state));
        /* Reject all other pairing requests */
        ConnectionSmIoCapabilityResponse(&(ind->tpaddr),
                                         cl_sm_reject_request,
                                         mitm_not_required,
                                         FALSE, 0,
                                         oob_data_none, 0, 0);
    }
}


/**
 * Handle the CL_SM_AUTHORISE_IND event.
 *
 * This is a request from the SM for authorisation when an untrusted or unknown
 * device is attempting to access a service that requires authorisation in
 * security mode 2.
 *
 * @param ind   The CL SM authorise indication.
 */
static void
handle_cl_sm_authorise_ind(CL_SM_AUTHORISE_IND_T *ind)
{
    LOG_DEBUG(("CL_SM_AUTHORISE_IND\n"));
    if (bluetooth_data.state == BLUETOOTH_STATE_CONNECTABLE)
    {
        PanicFalse(bluetooth_data.prev_addr_phone_valid);
        if (memcmp(&(ind->bd_addr), &bluetooth_data.prev_addr_phone,
                   sizeof(bluetooth_data.prev_addr_phone)) != 0)
        {
            LOG_DEBUG(("Device doesn't match bond\n"));
            LOG_BLUETOOTH_ADDR(ind->bd_addr);
            LOG_BLUETOOTH_ADDR(bluetooth_data.prev_addr_phone);
            /* Reject connection attempt */
            ConnectionSmAuthoriseResponse(&(ind->bd_addr), ind->protocol_id,
                                          ind->channel, ind->incoming, FALSE);
            return;
        }
    }
    /* Authorise the remote device */
    ConnectionSmAuthoriseResponse(&(ind->bd_addr), ind->protocol_id,
                                  ind->channel, ind->incoming, TRUE);
}


/**
 * Handle the CL_SM_ENCRYPTION_CHANGE_IND event.
 *
 * @param ind   The CL SM encryption change indication.
 */
static void
handle_cl_sm_encryption_change_ind(CL_SM_ENCRYPTION_CHANGE_IND_T *ind)
{
    LOG_DEBUG(("CL_SM_ENCRYPTION_CHANGE_IND - "));
    if (ind->encrypted)
        LOG_DEBUG(("encrypted\n"));
    else
        LOG_DEBUG(("not encrypted\n"));
}


void
bluetooth_manager_hfp_connection_start(bool success, bdaddr *addr)
{
    if (success)
    {
        status_t status;
        app_config_t *app_config;

        /* Store BD_ADDR in non-volatile storage */
        bluetooth_data.prev_addr_phone_valid = TRUE;
        memcpy(&bluetooth_data.prev_addr_phone, addr,
               sizeof(bluetooth_data.prev_addr_phone));

        /* app_config is malloc'd by config_manager_get_app_config_copy()
         * and free'd by config_manager_store_app_config() below */
        app_config = config_manager_get_app_config_copy();
        PanicFalse(app_config != NULL);
        app_config->hfp.prev_addr_phone_valid = TRUE;
        memcpy(&(app_config->hfp.prev_addr_phone), addr,
               sizeof(bluetooth_data.prev_addr_phone));

        status = config_manager_store_app_config(app_config);
        PanicFalse(status == STATUS_OK);

        bluetooth_set_state(BLUETOOTH_STATE_CONNECTED);
    }
    else
    {
        LOG_DEBUG(("bluetooth_manager_hfp_connection_start FAILED\r\n"));
    }
}


/**
 * Update the sync state boolean based on the current state.
 *
 * @param state     The current bluetooth state.
 */
static void
bluetooth_update_sync_state(bluetooth_state_t state)
{
    if (state == BLUETOOTH_STATE_PAIRING ||
        state == BLUETOOTH_STATE_PAIRABLE ||
        state == BLUETOOTH_STATE_SCANNING)
    {
        bluetooth_data.sync_in_progress = TRUE;
    }
    else
    {
        bluetooth_data.sync_in_progress = FALSE;
    }
}


static void
bluetooth_set_state(bluetooth_state_t state)
{
    LOG_DEBUG(("Bluetooth state change %u -> %u\n",
               bluetooth_data.state, state));
    bluetooth_data.state = state;

    bluetooth_update_sync_state(state);

    switch (state)
    {
    case BLUETOOTH_STATE_INITIALISING:
        LOG_DEBUG(("BLUETOOTH_STATE_INITIALISING\r\n"));
        /* Initialise Connection subsystem - only allow one device in the
         * trusted device list */
        ConnectionInitEx2(bluetooth_data.task, 0, 1);
        break;

    case BLUETOOTH_STATE_IDLE:
        LOG_DEBUG(("BLUETOOTH_STATE_IDLE\r\n"));
        (void)MessageCancelAll(&reconnect_task, 0);
        break;

    case BLUETOOTH_STATE_SCANNING:
        LOG_DEBUG(("BLUETOOTH_STATE_SCANNING\r\n"));
        (void)MessageCancelAll(&reconnect_task, 0);

        /* Start a scan */
        bluetooth_inquiry();
        break;

    case BLUETOOTH_STATE_PAIRABLE:
        LOG_DEBUG(("BLUETOOTH_STATE_PAIRABLE\r\n"));

        /* Become discoverable and connectable */
        ConnectionWriteScanEnable(hci_scan_enable_inq_and_page);
        break;

    case BLUETOOTH_STATE_PAIRING:
        LOG_DEBUG(("BLUETOOTH_STATE_PAIRING\r\n"));
        break;

    case BLUETOOTH_STATE_CONNECTING:
        LOG_DEBUG(("BLUETOOTH_STATE_CONNECTING\r\n"));

        /* Connect (or, if AGHFP is still busy, retry after a delay) */
        PanicFalse(bluetooth_data.prev_addr_headset_valid);
        if (!bluetooth_manager_aghfp_connect(&bluetooth_data.prev_addr_headset))
            bluetooth_schedule_reconnect();
        break;

    case BLUETOOTH_STATE_CONNECTABLE:
        LOG_DEBUG(("BLUETOOTH_STATE_CONNECTABLE\r\n"));

        /* Become discoverable and connectable */
        ConnectionWriteScanEnable(hci_scan_enable_inq_and_page);
        break;

    case BLUETOOTH_STATE_CONNECTED:
        LOG_DEBUG(("BLUETOOTH_STATE_CONNECTED\r\n"));
        (void)MessageCancelAll(&reconnect_task, 0);
        break;
    }
}


void
bluetooth_manager_aghfp_pairing(void)
{
    /* Stop the inquiry: it keeps running until its timeout otherwise, and
     * slows down the connection to the headset that was found */
    ConnectionInquireCancel(bluetooth_data.task);

    bluetooth_set_state(BLUETOOTH_STATE_PAIRING);
}


void
bluetooth_manager_aghfp_connected(bdaddr *addr)
{
    status_t status;
    app_config_t *app_config;
    bool new_headset;

    /* Never store an empty address as the bond: every later reconnect would
     * then page 0000:00:000000 */
    if (addr->lap == 0 && addr->uap == 0 && addr->nap == 0)
    {
        log_print("BT: connected but headset address unknown - "
                  "bond not updated\n");
        bluetooth_set_state(BLUETOOTH_STATE_CONNECTED);
        return;
    }

    /* app_config is malloc'd by config_manager_get_app_config_copy()
     * and free'd by config_manager_store_app_config() below */
    app_config = config_manager_get_app_config_copy();
    PanicFalse(app_config != NULL);

    new_headset = !app_config->aghfp.prev_addr_headset_valid ||
                  !BdaddrIsSame(addr, &app_config->aghfp.prev_addr_headset);

    if (!new_headset)
    {
        /* Same headset reconnected: the bond is already stored, so don't
         * rewrite the persistent store on every (re)connection */
        free(app_config);
        bluetooth_set_state(BLUETOOTH_STATE_CONNECTED);
        return;
    }

    /* Store BD_ADDR in non-volatile storage */
    bluetooth_data.prev_addr_headset_valid = TRUE;
    memcpy(&bluetooth_data.prev_addr_headset, addr,
           sizeof(bluetooth_data.prev_addr_headset));

    app_config->aghfp.prev_addr_headset_valid = TRUE;
    memcpy(&(app_config->aghfp.prev_addr_headset), addr,
           sizeof(bluetooth_data.prev_addr_headset));

    if (new_headset)
    {
        app_config->audio.bt_spk_boost_db = 0;
        LOG_DEBUG(("New headset bond; BT spk boost reset to 0 dB\n"));
    }

    status = config_manager_store_app_config(app_config);
    PanicFalse(status == STATUS_OK);

    bluetooth_set_state(BLUETOOTH_STATE_CONNECTED);
}


void
bluetooth_manager_aghfp_connection_failed(void)
{
    if (bluetooth_data.state == BLUETOOTH_STATE_CONNECTING)
    {
        /* Try again (after a delay) if in connecting state */
        bluetooth_schedule_reconnect();
    }
    else if (bluetooth_data.state == BLUETOOTH_STATE_PAIRING &&
             bluetooth_data.mode == BLUETOOTH_MODE_SOURCE)
    {
        /* The headset found by the scan could not be connected. Previously
         * the device stayed in PAIRING (no scan running) until the radio
         * cancelled the sync; scan again instead. If CL_DM_ACL_CLOSED_IND
         * already did this the state is SCANNING and nothing happens. */
        bluetooth_set_state(BLUETOOTH_STATE_SCANNING);
    }
}


void
bluetooth_manager_aghfp_slc_disconnected(void)
{
    if (bluetooth_data.mode != BLUETOOTH_MODE_SOURCE ||
        !bluetooth_data.on ||
        !bluetooth_data.prev_addr_headset_valid)
    {
        return;
    }

    if (bluetooth_data.state == BLUETOOTH_STATE_CONNECTED)
    {
        /* SLC gone - reconnect, also if the ACL stays up for a while (until
         * now only CL_DM_ACL_CLOSED_IND started the reconnection) */
        bluetooth_set_state(BLUETOOTH_STATE_CONNECTING);
    }
    else if (bluetooth_data.state == BLUETOOTH_STATE_CONNECTING)
    {
        /* SLC came up but went again before audio connected (or a
         * reconnection was postponed while it was closing): try again.
         * Previously this left the device in CONNECTING for good. */
        bluetooth_schedule_reconnect();
    }
}


static void
handle_cl_dm_inquire_result(CL_DM_INQUIRE_RESULT_T *result)
{
    if (result->status == inquiry_status_result)
    {
        /* Parse result */
        LOG_DEBUG(("Inquiry result in state %u\r\n", bluetooth_data.state));
        LOG_BLUETOOTH_ADDR((result->bd_addr));

        /* Ignore results if not in scanning state */
        if (bluetooth_data.state == BLUETOOTH_STATE_SCANNING)
            bluetooth_aghfp_process_inquiry_result(result);
    }
    else
    {
        /* Inquiry completed (result->status == inquiry_status_ready) */
        if (bluetooth_data.state == BLUETOOTH_STATE_SCANNING)
        {
            /* Defer inquiry restart if AGHFP started SDP for a held CoD candidate. */
            if (!bluetooth_aghfp_inquiry_complete())
                bluetooth_inquiry();
        }
    }
}


void
bluetooth_manager_aghfp_sdp_verification_failed(void)
{
    if (bluetooth_data.mode == BLUETOOTH_MODE_SOURCE &&
        bluetooth_data.state == BLUETOOTH_STATE_SCANNING)
    {
        bluetooth_inquiry();
    }
}


void
bluetooth_manager_sink_changed(Sink sink)
{
    bluetooth_data.active_sink = sink;
    audio_manager_set_hfk_call_state(sink != NULL);
    audio_manager_update_routing();
    audio_manager_update_kalimba_levels();
}


/**
 * Handles Connection library messages. The events are enumerated in the
 * ConnectionMessageId enum in src\lib\connection\connection_no_ble.h of the
 * ADK.
 *
 * @param task      The task relevant to the message.
 * @param id        Message ID.
 * @param message   Message content.
 */
static void
connection_msg_handler(Task task, MessageId id, Message message)
{
    switch (id)
    {
    case CL_INIT_CFM:
        handle_cl_init_cfm((CL_INIT_CFM_T *)message);
        break;

    case CL_DM_LINK_SUPERVISION_TIMEOUT_IND:
        /* 0x5003 */
        LOG_DEBUG(("CL_DM_LINK_SUPERVISION_TIMEOUT_IND\n"));
        break;

    case CL_DM_ACL_OPENED_IND:
        handle_cl_dm_acl_opened_ind((CL_DM_ACL_OPENED_IND_T *)message);
        break;

    case CL_DM_ACL_CLOSED_IND:
        handle_cl_dm_acl_closed_ind((CL_DM_ACL_CLOSED_IND_T *)message);
        break;

    case CL_SM_AUTHENTICATE_CFM:
        /* 0x5022 */
        /* Always printed: 0 = auth_status_success, see the
         * authentication_status enum in connection.h of the ADK */
        log_print("BT: authentication result %u\n",
                  ((CL_SM_AUTHENTICATE_CFM_T *)message)->status);

        /* Only a successful pairing is reported to the radio as "paired OK"
         * (it was reported whatever the result) */
        if (((CL_SM_AUTHENTICATE_CFM_T *)message)->status ==
                auth_status_success)
            main_bluetooth_callback(MAIN_BT_CB_PAIRED);
        break;

    case CL_SM_SECURITY_LEVEL_CFM:
        /* 0x5023 */
        LOG_DEBUG(("CL_SM_SECURITY_LEVEL_CFM\n"));
        break;

    case CL_SM_SEC_MODE_CONFIG_CFM:
        /* 0x5024 */
        LOG_DEBUG(("CL_SM_SEC_MODE_CONFIG_CFM\n"));
        break;

    case CL_SM_PIN_CODE_IND:
        handle_cl_sm_pin_code_ind((CL_SM_PIN_CODE_IND_T *)message);
        break;

    case CL_SM_IO_CAPABILITY_REQ_IND:
        handle_cl_sm_io_capability_req_ind(
                (CL_SM_IO_CAPABILITY_REQ_IND_T *)message);
        break;

    case CL_SM_REMOTE_IO_CAPABILITY_IND:
        /* 0x5027 */
        LOG_DEBUG(("CL_SM_REMOTE_IO_CAPABILITY_IND\n"));
        break;

    case CL_SM_AUTHORISE_IND:
        /* 0x502c */
        handle_cl_sm_authorise_ind((CL_SM_AUTHORISE_IND_T *)message);
        break;

    case CL_SM_ENCRYPTION_CHANGE_IND:
        handle_cl_sm_encryption_change_ind(
                (CL_SM_ENCRYPTION_CHANGE_IND_T *)message);
        break;

    case CL_SDP_REGISTER_CFM:
        /* 0x5030 */
        LOG_DEBUG(("CL_SDP_REGISTER_CFM\n"));
        break;

    case CL_SDP_SERVICE_SEARCH_CFM:
        /* AGHFP CoD fallback: HFP_HF SDP verify */
        LOG_DEBUG(("CL_SDP_SERVICE_SEARCH_CFM\n"));
        (void)bluetooth_aghfp_sdp_search_cfm(
                (CL_SDP_SERVICE_SEARCH_CFM_T *)message);
        break;

    case CL_DM_WRITE_INQUIRY_ACCESS_CODE_CFM:
        /* 0x5054 */
        LOG_DEBUG(("CL_DM_WRITE_INQUIRY_ACCESS_CODE_CFM\n"));
        break;

    case CL_DM_WRITE_INQUIRY_MODE_CFM:
        /* 0x5055 */
        LOG_DEBUG(("CL_DM_WRITE_INQUIRY_MODE_CFM\n"));
        break;

    case CL_DM_INQUIRE_RESULT:
        handle_cl_dm_inquire_result((CL_DM_INQUIRE_RESULT_T *)message);
        break;

    default:
        LOG_DEBUG(("Unhandled message 0x%04x\r\n", id));
        break;
    }
}


/**
 * Handles Bluetooth task messages. This function simply directs the relevant
 * messages to their appropriate handlers.
 *
 * @param task      The task relevant to the message.
 * @param id        Message ID.
 * @param message   Message content.
 */
static void
bluetooth_msg_handler(Task task, MessageId id, Message message)
{
    if (id >= CL_MESSAGE_BASE && id < CL_MESSAGE_TOP)
        connection_msg_handler(task, id, message);
    else if (id >= HFP_MESSAGE_BASE && id < HFP_MESSAGE_TOP)
        bluetooth_hfp_msg_handler(task, id, message);
    else if (id >= AGHFP_MESSAGE_BASE && id < AGHFP_MESSAGE_TOP)
        bluetooth_aghfp_msg_handler(task, id, message);
    else
        LOG_DEBUG(("Unhandled message (bluetooth_msg_handler) 0x%04x\r\n", id));
}


void
bluetooth_manager_init(void)
{
    app_config_t *app_config;

    bluetooth_task.handler = bluetooth_msg_handler;
    bluetooth_data.task = &bluetooth_task;
    reconnect_task.handler = reconnect_handler;
    /* Set defaults */
    bluetooth_data.on = TRUE;
    bluetooth_data.mode = BLUETOOTH_MODE_SOURCE;

    /* Get bond data from nvstore */
    /* app_config is malloc'd by config_manager_get_app_config_copy()
     * so must be free'd below */
    app_config = config_manager_get_app_config_copy();
    PanicFalse(app_config != NULL);

    memcpy(&bluetooth_data.prev_addr_headset,
           &(app_config->aghfp.prev_addr_headset),
           sizeof(bluetooth_data.prev_addr_headset));
    bluetooth_data.prev_addr_headset_valid =
            app_config->aghfp.prev_addr_headset_valid;

    if (bluetooth_data.prev_addr_headset_valid)
        LOG_DEBUG(("Have headset bond\n"));
    else
        LOG_DEBUG(("No headset bond stored\n"));


    memcpy(&bluetooth_data.prev_addr_phone,
           &(app_config->hfp.prev_addr_phone),
           sizeof(bluetooth_data.prev_addr_phone));
    bluetooth_data.prev_addr_phone_valid =
            app_config->hfp.prev_addr_phone_valid;

    if (bluetooth_data.prev_addr_phone_valid)
        LOG_DEBUG(("Have phone bond\n"));
    else
        LOG_DEBUG(("No phone bond stored\n"));

    free(app_config);

    bluetooth_set_state(BLUETOOTH_STATE_INITIALISING);
}


void
bluetooth_manager_on(bool on)
{
    if (on == bluetooth_data.on)
        return;

    bluetooth_data.on = on;

    LOG_DEBUG(("Bluetooth turning "));
    if (on)
    {
        LOG_DEBUG(("on\n"));

        if (bluetooth_data.mode == BLUETOOTH_MODE_SINK)
        {
            /* If we have a bonded phone then enter connectable mode, otherwise
             * stay in idle */
            if (bluetooth_data.prev_addr_phone_valid)
                bluetooth_set_state(BLUETOOTH_STATE_CONNECTABLE);
            else
                bluetooth_set_state(BLUETOOTH_STATE_IDLE);
        }
        else
        {
            /* If we have a bonded headset then attempt to connect, otherwise
             * stay in idle */
            if (bluetooth_data.prev_addr_headset_valid)
                bluetooth_set_state(BLUETOOTH_STATE_CONNECTING);
            else
                bluetooth_set_state(BLUETOOTH_STATE_IDLE);
        }
    }
    else
    {
        LOG_DEBUG(("off\n"));

        switch (bluetooth_data.state)
        {
        case BLUETOOTH_STATE_IDLE:
            return;

        case BLUETOOTH_STATE_PAIRING:
        case BLUETOOTH_STATE_CONNECTING:
        case BLUETOOTH_STATE_CONNECTABLE:
        case BLUETOOTH_STATE_CONNECTED:
            if (bluetooth_data.mode == BLUETOOTH_MODE_SINK)
            {
                bluetooth_hfp_disconnect();
            }
            else
            {
                bluetooth_aghfp_disconnect();
            }
            break;

        case BLUETOOTH_STATE_SCANNING:
            ConnectionInquireCancel(bluetooth_data.task);
            break;

        case BLUETOOTH_STATE_PAIRABLE:
            ConnectionWriteScanEnable(hci_scan_enable_off);
            break;

        default:
            break;
        }

        bluetooth_set_state(BLUETOOTH_STATE_IDLE);
    }
}


void
bluetooth_manager_set_mode(bluetooth_mode_t mode)
{
    if (mode == bluetooth_data.mode)
        return;

    LOG_DEBUG(("Bluetooth mode "));
    if (mode == BLUETOOTH_MODE_SOURCE)
        LOG_DEBUG(("Source\n"));
    else
        LOG_DEBUG(("Sink\n"));

    /*
     * If Bluetooth is enabled then disable it, change modes and then enable
     * Bluetooth again.
     */
    if (bluetooth_data.on)
    {
        bluetooth_manager_on(FALSE);
        bluetooth_data.mode = mode;
        bluetooth_manager_on(TRUE);
    }
    else
    {
        bluetooth_data.mode = mode;
    }

    /* Notify the Audio Manager subsystem that there has been a change. */
    audio_manager_update_routing();
}


bluetooth_mode_t
bluetooth_manager_get_mode(void)
{
    return bluetooth_data.mode;
}


void
bluetooth_manager_source_mode_sync(bool on)
{
    if (bluetooth_data.mode != BLUETOOTH_MODE_SOURCE ||
        !bluetooth_data.on ||
        bluetooth_data.state == BLUETOOTH_STATE_INITIALISING)
    {
        /* Do nothing if Bluetooth is off or we are in the other mode */
        return;
    }

    LOG_DEBUG(("Bluetooth Source mode sync "));
    if (on)
        LOG_DEBUG(("on\n"));
    else
        LOG_DEBUG(("off\n"));

    if (on)
    {
        bluetooth_set_state(BLUETOOTH_STATE_SCANNING);
    }
    else
    {
        switch (bluetooth_data.state)
        {
        case BLUETOOTH_STATE_CONNECTED:
        case BLUETOOTH_STATE_CONNECTING:
        case BLUETOOTH_STATE_IDLE:
            return;

        default:
            break;
        }

        if (bluetooth_data.prev_addr_headset_valid)
        {
            /* We have a bond - change to connecting mode */
            bluetooth_set_state(BLUETOOTH_STATE_CONNECTING);
        }
        else
        {
            bluetooth_set_state(BLUETOOTH_STATE_IDLE);
        }
    }
}


void
bluetooth_manager_sink_mode_sync(bool on)
{
    if (bluetooth_data.mode != BLUETOOTH_MODE_SINK ||
        !bluetooth_data.on ||
        bluetooth_data.state == BLUETOOTH_STATE_INITIALISING)
    {
        /* Do nothing if Bluetooth is off, if we are in the other mode or
         * if we haven't initialised Bluetooth yet */
        return;
    }

    LOG_DEBUG(("Bluetooth Sink mode sync "));
    if (on)
        LOG_DEBUG(("on\n"));
    else
        LOG_DEBUG(("off\n"));

    if (on)
    {
        /* Change to pairable mode */
        switch (bluetooth_data.state)
        {
        case BLUETOOTH_STATE_IDLE:
        case BLUETOOTH_STATE_CONNECTABLE:
            bluetooth_set_state(BLUETOOTH_STATE_PAIRABLE);
            break;

        case BLUETOOTH_STATE_PAIRING:
        case BLUETOOTH_STATE_CONNECTED:
            /* Disconnect from current device and set the state to pairable */
            bluetooth_hfp_disconnect();
            bluetooth_set_state(BLUETOOTH_STATE_PAIRABLE);
            break;

        case BLUETOOTH_STATE_PAIRABLE:
        default:
            break;
        }
    }
    else
    {
        switch (bluetooth_data.state)
        {
        case BLUETOOTH_STATE_PAIRING:
            bluetooth_hfp_disconnect();
            /*@fallthrough@*/
        case BLUETOOTH_STATE_PAIRABLE:
            /*
             * If there is a bond enter a connectable state, else go
             * enter an IDLE state.
             */
            if (bluetooth_data.prev_addr_phone_valid)
            {
                bluetooth_set_state(BLUETOOTH_STATE_CONNECTABLE);
            }
            else
            {
                ConnectionWriteScanEnable(hci_scan_enable_off);
                bluetooth_set_state(BLUETOOTH_STATE_IDLE);
            }
            break;

        case BLUETOOTH_STATE_CONNECTABLE:
        case BLUETOOTH_STATE_IDLE:
        case BLUETOOTH_STATE_CONNECTED:
        default:
            break;
        }
    }
}


bool
bluetooth_sync_in_progress(void)
{
    return bluetooth_data.sync_in_progress;
}


Sink
bluetooth_manager_get_hfp_sink(void)
{
    return bluetooth_data.active_sink;
}
