#include <stdlib.h>
#include <bdaddr.h>
#include <message.h>
#include <panic.h>
#include <sink.h>

#include "bluetooth_private.h"
#include "aghfp.h"

/* Uncomment this to enable logging in this file */
/* #define DEBUG_ENABLED */
#include "log.h"


/** Delay before retrying a failed audio (SCO/eSCO) connection, in ms. */
#define AUDIO_RETRY_DELAY_MS    (1000)

/** Audio connection attempts before the SLC is dropped and reconnected. */
#define AUDIO_RETRY_MAX         (3)

/** Operator name sent in reply to AT+COPS? */
static uint8 network_operator[] = "MULTIVOICE";


/** AGHFP State Machine */
typedef enum
{
    AGHFP_STATE_DISCONNECTED = 0,
    AGHFP_STATE_CONNECTING_REMOTE,
    AGHFP_STATE_CONNECTED,
    AGHFP_STATE_CONNECTING_AUDIO_REMOTE,
    AGHFP_STATE_CONNECTED_AUDIO,
    AGHFP_STATE_DISCONNECTING_AUDIO,
    AGHFP_STATE_DISCONNECTING
} AGHFP_STATE_T;


/** Structure holding the AGHFP data */
typedef struct aghfpInstance
{
    TaskData aghfpTask;
    AGHFP_STATE_T aghfp_state;
    bdaddr addr;
    AGHFP *aghfp;
    Sink slc_sink;
    Sink audio_sink;
} aghfpInstance;


/** Values used to identify profiles */
typedef enum
{
    PROFILE_NONE   = 0x00,
    PROFILE_AGHFP  = 0x01
} PROFILES_T;


/** Inquiry structures */
typedef struct
{
    int16 path_loss;
    PROFILES_T profiles:4;
    unsigned profiles_complete:1;
    unsigned has_uuid16_service_list:1;
} INQUIRY_EIR_DATA_T;


/* EIR tags */
#define EIR_TYPE_LOCAL_NAME_COMPLETE            (0x09)
#define EIR_TYPE_UUID16_PARTIAL                 (0x02)
#define EIR_TYPE_INQUIRY_TX                     (0x0A)
#define EIR_TYPE_UUID16_COMPLETE                (0x03)
#define EIR_TYPE_UUID16_SHORTENED               (0x02)

/* Device UUIDs */
#define BT_UUID_SERVICE_CLASS_A2DP              (0x110D)
#define BT_UUID_SERVICE_CLASS_AUDIO_SOURCE      (0x110A)
#define BT_UUID_SERVICE_CLASS_AUDIO_SINK        (0x110B)
#define BT_UUID_SERVICE_CLASS_AVRCP             (0x110E)
#define BT_UUID_SERVICE_CLASS_AVRCP_CT          (0x110F)
#define BT_UUID_SERVICE_CLASS_AVRCP_TG          (0x110C)
#define BT_UUID_SERVICE_CLASS_HFP_AG            (0x111F)
#define BT_UUID_SERVICE_CLASS_HFP_HF            (0x111E)

#define BT_COD_MAJOR_CLASS_MASK                 (0x1F00)
#define BT_COD_MAJOR_CLASS_AUDIO_VIDEO          (0x0400)
#define BT_COD_MINOR_CLASS_MASK                 (0x00FC)
#define BT_COD_AV_MINOR_WEARABLE_HEADSET        (0x04)
#define BT_COD_AV_MINOR_HANDSFREE               (0x08)
#define BT_COD_AV_MINOR_HEADPHONES              (0x18)


/** Maximum Inquiry TX power. */
#define INQUIRY_TX_POWER_MAX                    (127)


/* Stores a aghfp instance to be used for the current connection. */
static aghfpInstance instance;

/** Timer task that retries the audio connection. */
static TaskData audio_retry_task;

/** Failed audio connection attempts since the SLC came up. */
static uint16 audio_retry_count = 0;

static uint16 PREVIOUS_VOLUME_GAIN = 0;

/* CoD-only inquiry candidate: defer connect until inquiry ends, then SDP HFP_HF. */
typedef enum
{
    COD_CANDIDATE_NONE = 0,
    COD_CANDIDATE_PENDING,
    COD_CANDIDATE_VERIFYING
} cod_candidate_state_t;

static struct
{
    cod_candidate_state_t state;
    bdaddr addr;
} cod_candidate;

/* SDP search pattern: 16-bit UUID HFP Hands-Free (0x111E). */
static const uint8 sdp_search_hfp_hf_pattern[] =
{
    0x35, 0x03, 0x19,
    (uint8)((BT_UUID_SERVICE_CLASS_HFP_HF >> 8) & 0xFF),
    (uint8)(BT_UUID_SERVICE_CLASS_HFP_HF & 0xFF)
};

static void
cod_candidate_clear(void)
{
    cod_candidate.state = COD_CANDIDATE_NONE;
    BdaddrSetZero(&cod_candidate.addr);
}

static bool
inquiry_cod_suggests_hfp_hf(const CL_DM_INQUIRE_RESULT_T *result)
{
    uint32 cod = result->dev_class;
    uint32 minor;

    if ((cod & BT_COD_MAJOR_CLASS_MASK) != BT_COD_MAJOR_CLASS_AUDIO_VIDEO)
        return FALSE;

    minor = cod & BT_COD_MINOR_CLASS_MASK;

    /* Exclude minor 0x18 (headphones); many A2DP-only devices use it. */
    return (minor == BT_COD_AV_MINOR_WEARABLE_HEADSET) ||
           (minor == BT_COD_AV_MINOR_HANDSFREE);
}

/**
 * Extracts the profiles from the uuids.
 *
 * @param size_uuids       Size of uuids in octets.
 * @param uuids            An array of uuids.
 *
 * @returns PROFILE_AGHFP if it is supported, else PROFILE_NONE.
 */
static PROFILES_T
inquiry_parse_eir_uuids(const uint8 size_uuids, const uint8 *uuids)
{
    PROFILES_T profiles = PROFILE_NONE;
    uint8 k;
    uint16 uuid;

    for (k = 0; k < size_uuids; k += 2)
    {
        /* Get UUID from data */
        uuid = uuids[k] | (uuids[k+1] << 8);

        if (uuid == BT_UUID_SERVICE_CLASS_HFP_HF)
            profiles |= PROFILE_AGHFP;
    }
    return profiles;
}


/**
 * Extracts the required EIR data found in the inquiry result.
 *
 * @param size_eir_data     The length of the eir_data.
 * @param eir_data          The eir data to extract information from.
 *
 * @returns the extracted eir data.
 */
static INQUIRY_EIR_DATA_T
inquiry_parse_eir_data(const uint8 size_eir_data, const uint8* eir_data)
{
    const uint8* p = eir_data;
    INQUIRY_EIR_DATA_T result;

    /* Default to max possible Inquiry Tx Power */
    result.path_loss = INQUIRY_TX_POWER_MAX;
    result.profiles = PROFILE_NONE;
    result.profiles_complete = FALSE;
    result.has_uuid16_service_list = FALSE;

    /* Search until no more to read */
    for (p = eir_data; (uint8)(p - eir_data) != size_eir_data; p += (*p)+1)
    {
        uint8 field_length = *p;

        if ((field_length == 0) ||
            ((uint16)(p - eir_data) + field_length >= size_eir_data))
            break;

        /* Check type fields for UUIDs or Inquiry Tx Power */
        if (*(p+1) == EIR_TYPE_INQUIRY_TX)
        {
            /* Initially set path loss to Inquiry Tx Power*/
            result.path_loss = *p+2;
        }
        else if ((*(p+1) == EIR_TYPE_UUID16_COMPLETE) ||
                 (*(p+1) == EIR_TYPE_UUID16_SHORTENED))
        {
            /* Get the profile information */
            result.profiles_complete = (*(p+1) == EIR_TYPE_UUID16_COMPLETE);
            result.has_uuid16_service_list = TRUE;
            result.profiles |= inquiry_parse_eir_uuids((uint8)(*p - 1), p+2);
        }
    }

    /* Return number of UUIDs found in EIR data */
    return result;
}


/**
 * Connect to a provided Bluetooth address.
 *
 * @param bd_addr       The Bluetooth address to connect with.
 */
static void
bluetooth_aghfp_connect(const bdaddr *bd_addr)
{
    instance.addr = *bd_addr;
    /* Busy until AGHFP_SLC_CONNECT_CFM: no second connect, and inquiry
     * results are ignored meanwhile */
    instance.aghfp_state = AGHFP_STATE_CONNECTING_REMOTE;
    AghfpSlcConnect(instance.aghfp, &(instance.addr));
}


/**
 * Process an inquiry result.
 *
 * @param result        An inquiry result to process.
 */
void
bluetooth_aghfp_process_inquiry_result(CL_DM_INQUIRE_RESULT_T *result)
{
    INQUIRY_EIR_DATA_T parsed_result =
            inquiry_parse_eir_data(result->size_eir_data, result->eir_data);

    /*
     * Ignore all inquiry results if we are in any state other than disconnected
     */
    if (instance.aghfp_state != AGHFP_STATE_DISCONNECTED)
    {
        LOG_DEBUG(("Ignoring inquiry result in state %u\n",
                   instance.aghfp_state));
        return;
    }

    /* EIR lists HFP_HF: connect immediately (same as original behaviour). */
    if ((parsed_result.profiles & PROFILE_AGHFP) == PROFILE_AGHFP)
    {
        LOG_DEBUG(("Headset found, attempting to connect...\n"));
        LOG_BLUETOOTH_ADDR((result->bd_addr));
        cod_candidate_clear();
        bluetooth_manager_aghfp_pairing();
        bluetooth_aghfp_connect(&(result->bd_addr));
        return;
    }

    /* No UUID16 list in EIR but CoD looks like HF: hold first candidate; SDP after inquiry. */
    if (!parsed_result.has_uuid16_service_list &&
        inquiry_cod_suggests_hfp_hf(result))
    {
        if (cod_candidate.state == COD_CANDIDATE_NONE)
        {
            LOG_DEBUG(("CoD candidate held for SDP (HFP_HF):\n"));
            LOG_BLUETOOTH_ADDR((result->bd_addr));
            cod_candidate.state = COD_CANDIDATE_PENDING;
            cod_candidate.addr = result->bd_addr;
        }
        return;
    }

    LOG_DEBUG(("Ignoring inquiry result (profiles 0x%x, has_uuid16 %u, "
               "dev_class 0x%lx)\n",
               parsed_result.profiles,
               parsed_result.has_uuid16_service_list,
               result->dev_class));
}


/* Inquiry finished: if a CoD candidate is pending, start SDP; TRUE defers inquiry restart. */
bool
bluetooth_aghfp_inquiry_complete(void)
{
    /* EIR path already started SLC: drop any pending CoD candidate. */
    if (instance.aghfp_state != AGHFP_STATE_DISCONNECTED)
    {
        cod_candidate_clear();
        return FALSE;
    }

    if (cod_candidate.state != COD_CANDIDATE_PENDING)
        return FALSE;

    LOG_DEBUG(("Inquiry done; SDP verify HFP_HF on CoD candidate:\n"));
    LOG_BLUETOOTH_ADDR(cod_candidate.addr);

    cod_candidate.state = COD_CANDIDATE_VERIFYING;
    ConnectionSdpServiceSearchRequest(bluetooth_data.task,
                                      &cod_candidate.addr,
                                      1,
                                      sizeof(sdp_search_hfp_hf_pattern),
                                      sdp_search_hfp_hf_pattern);
    return TRUE;
}


/* Our SDP HFP_HF search completed; TRUE if this CFM was for us. */
bool
bluetooth_aghfp_sdp_search_cfm(CL_SDP_SERVICE_SEARCH_CFM_T *message)
{
    if (cod_candidate.state != COD_CANDIDATE_VERIFYING)
        return FALSE;

    if (!BdaddrIsSame(&message->bd_addr, &cod_candidate.addr))
        return FALSE;

    /* Mode/state changed during SDP: discard candidate. */
    if (bluetooth_data.state != BLUETOOTH_STATE_SCANNING ||
        instance.aghfp_state != AGHFP_STATE_DISCONNECTED)
    {
        LOG_DEBUG(("SDP CFM ignored (bt_state %u, aghfp_state %u)\n",
                   bluetooth_data.state,
                   instance.aghfp_state));
        cod_candidate_clear();
        return TRUE;
    }

    if (message->status == sdp_response_success && message->num_records > 0)
    {
        bdaddr addr = cod_candidate.addr;

        LOG_DEBUG(("SDP: HFP_HF OK, connecting:\n"));
        LOG_BLUETOOTH_ADDR(addr);

        cod_candidate_clear();
        bluetooth_manager_aghfp_pairing();
        bluetooth_aghfp_connect(&addr);
    }
    else
    {
        LOG_DEBUG(("SDP: no HFP_HF (status %u, records %u)\n",
                   message->status,
                   message->num_records));
        cod_candidate_clear();
        bluetooth_manager_aghfp_sdp_verification_failed();
    }

    return TRUE;
}


/**
 * Reset the instance before connecting to a new device.
 */
static void
reset_instance(void)
{
    BdaddrSetZero(&instance.addr);
    instance.aghfp_state = AGHFP_STATE_DISCONNECTED;
    cod_candidate_clear();
}


void
bluetooth_aghfp_disconnect(void)
{
    AghfpSlcDisconnect(instance.aghfp);
}


/**
 * Retry the audio connection (audio_retry_task handler). Only while the SLC
 * is up without audio.
 */
static void
audio_retry_handler(Task task, MessageId id, Message message)
{
    (void)task;
    (void)id;
    (void)message;

    if (bluetooth_data.on && instance.aghfp_state == AGHFP_STATE_CONNECTED)
    {
        log_print("BT: retrying audio connection (%u)\n", audio_retry_count);
        AghfpAudioConnect(instance.aghfp, sync_all_pkt_types, NULL);
    }
}


/**
 * Stop audio connection retries (SLC up with audio, or SLC gone).
 */
static void
audio_retry_cancel(void)
{
    (void)MessageCancelAll(&audio_retry_task, 0);
    audio_retry_count = 0;
}


void
bluetooth_aghfp_init(void)
{
    aghfp_profile profile = aghfp_handsfree_17_profile;
    uint16 features = aghfp_voice_recognition | aghfp_enhanced_call_status |
                      aghfp_codec_negotiation | aghfp_esco_s4_supported |
                      aghfp_hf_indicators;

    audio_retry_task.handler = audio_retry_handler;

    /* This section is from source_aghfp.c */
    reset_instance();

    AghfpInit(bluetooth_data.task, profile, features);
    LOG_DEBUG(("AGHFP_INIT\n"));
}


bool
bluetooth_manager_aghfp_connect(bdaddr *addr)
{
    /* One connection at a time: while a connect is outstanding or the SLC is
     * still up (e.g. CL_DM_ACL_CLOSED_IND arrived before
     * AGHFP_SLC_DISCONNECT_IND) the caller retries later */
    if (instance.aghfp_state != AGHFP_STATE_DISCONNECTED)
    {
        LOG_DEBUG(("AGHFP busy (state %u), connect postponed\n",
                   instance.aghfp_state));
        return FALSE;
    }

    log_print("BT: connecting to headset [%04x:%02x:%06lx]\n",
              addr->nap, addr->uap, addr->lap);

    bluetooth_aghfp_connect(addr);
    return TRUE;
}


/**
 * Handle the service layer connection disconnecting.
 *
 * @param message       A message from the aghfp library.
 */
static void
aghfp_msg_slc_disconnect_ind(AGHFP_SLC_DISCONNECT_IND_T *message)
{
    /* Always printed: 0 = normal disconnect (by the headset or by us),
     * 1 = link loss, see aghfp_disconnect_status in aghfp.h */
    log_print("BT: SLC disconnected, status %u\n", message->status);

    audio_retry_cancel();

    /* Set the status to disconnected */
    instance.aghfp_state = AGHFP_STATE_DISCONNECTED;

    /* Reset instance */
    reset_instance();

    bluetooth_manager_aghfp_slc_disconnected();
}


/**
 * Handles AGHFP library message AGHFP_INIT_CFM.
 *
 * @param message       Message from the AGHFP library.
 */
static void
aghfp_msg_init_cfm(const AGHFP_INIT_CFM_T *message)
{
    instance.aghfp = message->aghfp;
    bluetooth_manager_aghfp_initialised();
}


/**
 * Handle a aghfp_msg_slc_connect_cfm event.
 *
 * @param message       Message from the AGHFP library.
 */
static void
aghfp_msg_slc_connect_cfm(const AGHFP_SLC_CONNECT_CFM_T *message)
{
    /* If the connection was successful t */
    if (message->status == aghfp_success)
    {
        tp_bdaddr tpaddr;

        /* Set the state to connected */
        instance.aghfp_state = AGHFP_STATE_CONNECTED;

        /* Store Sink for the connection */
        instance.slc_sink = message->rfcomm_sink;

        /* Take the headset address from the link itself: instance.addr is
         * not set for an incoming SLC, and is cleared by reset_instance()
         * if an old SLC's disconnect arrives during this connection. It is
         * stored as the bond when audio connects. */
        if (SinkGetBdAddr(message->rfcomm_sink, &tpaddr))
            instance.addr = tpaddr.taddr.addr;

        log_print("BT: SLC connected [%04x:%02x:%06lx]\n",
                  instance.addr.nap, instance.addr.uap, instance.addr.lap);

        audio_retry_cancel();

        /* Now we have a service connection establish an audio connection */
        AghfpAudioConnect(instance.aghfp, sync_all_pkt_types, NULL);
        
        LOG_DEBUG(("==============Simulating Active Call=================\n"));
        /* Simulate an active call (off-hook state) */
        AghfpSendCallIndicator(instance.aghfp, aghfp_call_active); /* Call is active */
        AghfpSendCallSetupIndicator(instance.aghfp, aghfp_call_setup_none); /* No call setup */
    }
    else
    {
        log_print("BT: SLC connect failed, status %u\n", message->status);

        /* Set the state to disconnected */
        instance.aghfp_state = AGHFP_STATE_DISCONNECTED;
        bluetooth_manager_aghfp_connection_failed();
    }
}


/**
 * Handle a aghfp_msg_slc_connect_ind event.
 *
 * @param message       Message from the AGHFP library.
 */
static void
aghfp_msg_slc_connect_ind(const AGHFP_SLC_CONNECT_IND_T *message)
{
    LOG_DEBUG(("Incoming AGHFP SLC from\n"));
    LOG_BLUETOOTH_ADDR(message->bd_addr);
    LOG_DEBUG(("Accepting\n"));

    /* Busy until AGHFP_SLC_CONNECT_CFM (no outgoing connect meanwhile) */
    instance.addr = message->bd_addr;
    instance.aghfp_state = AGHFP_STATE_CONNECTING_REMOTE;

    AghfpSlcConnectResponse(message->aghfp, TRUE);
}


/**
 * Handles the indication that a audio connection is being attempted.
 *
 * @param message       Message from the AGHFP library.
 */
static void
aghfp_msg_audio_connect_ind(AGHFP_AUDIO_CONNECT_IND_T *message)
{
    /* The headset is opening the SCO/eSCO link itself. The library waits
     * for this answer (it was never sent, so the request hung until the
     * headset timed out, and the library refused later audio connects as
     * "in progress"). Accept unless audio is already up. */
    bool accept = (instance.aghfp_state != AGHFP_STATE_CONNECTED_AUDIO);

    log_print("BT: headset requests audio - %s\n",
              accept ? "accepting" : "rejecting (audio already up)");

    if (accept)
        instance.aghfp_state = AGHFP_STATE_CONNECTING_AUDIO_REMOTE;

    AghfpAudioConnectResponse(message->aghfp, accept, sync_all_pkt_types,
                              NULL);
}


/**
 * Handles the event that a audio connection is confirmed.
 *
 * @param message       Message from the AGHFP library.
 */
static void
aghfp_msg_audio_connect_cfm(AGHFP_AUDIO_CONNECT_CFM_T *message)
{
    LOG_DEBUG(("  AGHFP Audio : cl_status:[%d] link_type[%d] audio_sink[0x%x] "
               "rx_bandwidth[0x%lx] tx_bandwidth[0x%lx] using_wbs[%d] "
               "wbs_codec[%d]\n",
               message->cl_status,
               message->link_type,
               (uint16)message->audio_sink,
               message->rx_bandwidth,
               message->tx_bandwidth,
               message->using_wbs,
               message->wbs_codec));

    if (message->status == aghfp_audio_connect_success)
    {
        log_print("BT: audio connected\n");
        audio_retry_cancel();

        instance.audio_sink = message->audio_sink;
        instance.aghfp_state = AGHFP_STATE_CONNECTED_AUDIO;
        /* All connected */
        bluetooth_manager_aghfp_connected(&instance.addr);
        bluetooth_manager_sink_changed(message->audio_sink);
    }
    else if (message->status == aghfp_audio_connect_in_progress ||
             message->status == aghfp_audio_connect_have_audio)
    {
        /* Another audio connect (e.g. started by the headset) is running and
         * its own AGHFP_AUDIO_CONNECT_CFM follows, or audio is already up:
         * not a failure, nothing to retry */
        log_print("BT: audio connect not started, status %u\n",
                  message->status);
    }
    else
    {
        log_print("BT: audio connect failed, status %u, hci 0x%02x\n",
                  message->status, message->cl_status);

        if (instance.aghfp_state == AGHFP_STATE_DISCONNECTED)
            return;     /* SLC already gone */

        instance.aghfp_state = AGHFP_STATE_CONNECTED;

        /* The device only works with audio up. Previously nothing happened
         * here: the SLC stayed up without audio and the Bluetooth Manager
         * stayed in CONNECTING, so nothing ever reconnected. */
        if (++audio_retry_count < AUDIO_RETRY_MAX)
        {
            MessageSendLater(&audio_retry_task, 0, 0, AUDIO_RETRY_DELAY_MS);
        }
        else
        {
            log_print("BT: no audio after %u attempts - "
                      "dropping SLC to reconnect\n", audio_retry_count);
            audio_retry_count = 0;
            AghfpSlcDisconnect(instance.aghfp);
        }
    }
}


/**
 * Handles the event when the audio connection disconnects.
 *
 * @param message       Message from the AGHFP library.
 */
static void
aghfp_msg_audio_disconnect_ind(AGHFP_AUDIO_DISCONNECT_IND_T *message)
{
    /* Always printed, see aghfp_audio_disconnect_status in aghfp.h */
    log_print("BT: audio disconnected, status %u\n", message->status);

    /* Only the audio link closed: the SLC is still up (was set to
     * DISCONNECTED, as if the SLC were gone). If the SLC is closing too,
     * AGHFP_SLC_DISCONNECT_IND follows and sets DISCONNECTED. */
    if (instance.aghfp_state != AGHFP_STATE_DISCONNECTED)
    {
        instance.aghfp_state = AGHFP_STATE_CONNECTED;

        /* Re-open the audio (the retry handler does nothing if the SLC is
         * gone by then) */
        MessageCancelAll(&audio_retry_task, 0);
        MessageSendLater(&audio_retry_task, 0, 0, AUDIO_RETRY_DELAY_MS);
    }

    bluetooth_manager_sink_changed(NULL);
}

static void
aghfp_msg_audio_volume_ind(AGHFP_SYNC_SPEAKER_VOLUME_IND_T *message)
{
    LOG_DEBUG(("Volume - %u\n",
                   ((AGHFP_SYNC_SPEAKER_VOLUME_IND_T *)message)->volume));
    if (message->volume > PREVIOUS_VOLUME_GAIN)  /* Volume Up detected */
    {
        LOG_DEBUG(("===============Volume UP==============\n"));
        handle_button_event(VOLUME_UP);
        PREVIOUS_VOLUME_GAIN = message->volume;
    }
    else if (message->volume < PREVIOUS_VOLUME_GAIN)  /* Volume Down detected */
    {
        LOG_DEBUG(("==============Volume Down=============\n"));
        handle_button_event(VOLUME_DOWN);
        PREVIOUS_VOLUME_GAIN = message->volume;
    }
}

void
bluetooth_aghfp_msg_handler(Task task, MessageId id, Message message)
{
    switch (id)
    {
    /* Confirmation that the aghfp lib is initialised */
    case AGHFP_INIT_CFM:
        LOG_DEBUG(("AGHFP_INIT_CFM [%d]:\n",
                   ((AGHFP_INIT_CFM_T *)message)->status));
        aghfp_msg_init_cfm((AGHFP_INIT_CFM_T *)message);
        return;

    /* Confirmation of a connection attempt */
    case AGHFP_SLC_CONNECT_CFM:
        LOG_DEBUG(("AGHFP_SLC_CONNECT_CFM [%d]:\n",
                   ((AGHFP_SLC_CONNECT_CFM_T *)message)->status));
        aghfp_msg_slc_connect_cfm((AGHFP_SLC_CONNECT_CFM_T *)message);
        return;

    /* Confirmation of a connection attempt */
    case AGHFP_SLC_CONNECT_IND:
        aghfp_msg_slc_connect_ind((AGHFP_SLC_CONNECT_IND_T *)message);
        return;

    /* Indication that the connection has been lost */
    case AGHFP_SLC_DISCONNECT_IND:
        LOG_DEBUG(("AGHFP_SLC_DISCONNECT_IND:\n"));
        aghfp_msg_slc_disconnect_ind((AGHFP_SLC_DISCONNECT_IND_T *)message);
        return;

    /* Confirmation of an audio connection request */
    case AGHFP_AUDIO_CONNECT_CFM:
        LOG_DEBUG(("AGHFP_AUDIO_CONNECT_CFM [%d]:\n",
                   ((AGHFP_AUDIO_CONNECT_CFM_T *)message)->status));
        aghfp_msg_audio_connect_cfm((AGHFP_AUDIO_CONNECT_CFM_T *)message);
        return;

    /* Indication that a remote device is trying to connect audio */
    case AGHFP_AUDIO_CONNECT_IND:
        LOG_DEBUG(("AGHFP_AUDIO_CONNECT_IND:\n"));
        aghfp_msg_audio_connect_ind((AGHFP_AUDIO_CONNECT_IND_T *)message);
        return;

    /* Indication that the audio has disconnected */
    case AGHFP_AUDIO_DISCONNECT_IND:
        LOG_DEBUG(("AGHFP_AUDIO_DISCONNECT_IND:\n"));
        aghfp_msg_audio_disconnect_ind((AGHFP_AUDIO_DISCONNECT_IND_T *)message);
        return;

    /* Indication that the audio parameters need to be supplied */
    case AGHFP_APP_AUDIO_PARAMS_REQUIRED_IND:
        /*
         * The headset asked to open audio (AT+BCC). The library answers it
         * (OK/ERROR, then codec negotiation and the audio connection) only
         * once the parameters are supplied - the same ones as
         * AghfpAudioConnect() in aghfp_msg_slc_connect_cfm().
         */
        log_print("BT: headset requests audio (AT+BCC)\n");
        AghfpSetAudioParams(instance.aghfp, sync_all_pkt_types, NULL);
        return;

    /* Indication of microphone gain sent from remote device */
    case AGHFP_SYNC_MICROPHONE_GAIN_IND:
        /*
         * The microphone gain can be extracted from
         * (((AGHFP_SYNC_MICROPHONE_GAIN_IND_T *)message)->gain)
         * to be sync.
         */
        LOG_DEBUG(("AGHFP_SYNC_MICROPHONE_GAIN_IND:\n"));
        return;

    /* Indication of speaker volume sent from remote device */
    case AGHFP_SYNC_SPEAKER_VOLUME_IND:
        /*
         * The volume can be extracted from
         * (((AGHFP_SYNC_SPEAKER_VOLUME_IND_T *)message)->volume) and used
         * to sync the speaker volumes.
         */
        LOG_DEBUG(("AGHFP_SYNC_SPEAKER_VOLUME_IND:\n"));
        LOG_DEBUG(("    - %u\n",
                   ((AGHFP_SYNC_SPEAKER_VOLUME_IND_T *)message)->volume));
        
        aghfp_msg_audio_volume_ind((AGHFP_SYNC_SPEAKER_VOLUME_IND_T *)message);
        
        return;

    /* Confirmation of setting microphone gain */
    case AGHFP_SET_REMOTE_MICROPHONE_GAIN_CFM:
        LOG_DEBUG(("AGHFP_SET_REMOTE_MICROPHONE_GAIN_CFM:\n"));
        return;

    /* Confirmation of setting speaker volume */
    case AGHFP_SET_REMOTE_SPEAKER_VOLUME_CFM:
        LOG_DEBUG(("AGHFP_SET_REMOTE_SPEAKER_VOLUME_CFM:\n"));
        return;

    /* HF wants voice recognition  */
    case AGHFP_VOICE_RECOGNITION_ENABLE_CFM:
        LOG_DEBUG(("AGHFP_VOICE_RECOGNITION_ENABLE_CFM:\n"));
        return;

    /* Confirmation of sending call indicator */
    case AGHFP_SEND_CALL_INDICATOR_CFM:
        LOG_DEBUG(("AGHFP_SEND_CALL_INDICATOR_CFM:\n"));
        return;

    /* Confirmation of sending call setup indicator */
    case AGHFP_SEND_CALL_SETUP_INDICATOR_CFM:
        LOG_DEBUG(("AGHFP_SEND_CALL_SETUP_INDICATOR_CFM:\n"));
        return;

    /* Confirmation of sending ring alert */
    case AGHFP_SEND_RING_ALERT_CFM:
        LOG_DEBUG(("AGHFP_SEND_RING_ALERT_CFM:\n"));
        return;

    /* Confirmation of sending inband ring enable */
    case AGHFP_INBAND_RING_ENABLE_CFM:
        LOG_DEBUG(("AGHFP_INBAND_RING_ENABLE_CFM:\n"));
        return;

    /* Confirmation of sending battery charge */
    case AGHFP_SEND_BATT_CHG_INDICATOR_CFM:
        LOG_DEBUG(("AGHFP_SEND_BATT_CHG_INDICATOR_CFM:\n"));
        return;

    /* Confirmation of sending service indicator */
    case AGHFP_SEND_SERVICE_INDICATOR_CFM:
        LOG_DEBUG(("AGHFP_SEND_SERVICE_INDICATOR_CFM:\n"));
        return;

    /* Confirmation of sending HF indicator */
    case AGHFP_SEND_BIND_INDICATOR_STATE_CFM:
        LOG_DEBUG(("AGHFP_SEND_BIND_INDICATOR_STATE_CFM:\n"));
        return;


    /* Confirmation of sending network operator */
    case AGHFP_NETWORK_OPERATOR_CFM:
        LOG_DEBUG(("AGHFP_NETWORK_OPERATOR_CFM:\n"));
        return;

    /* Indication of unrecognised AT command */
    case AGHFP_UNRECOGNISED_AT_CMD_IND:
        LOG_DEBUG(("AGHFP_UNRECOGNISED_AT_CMD_IND:\n"));
        AghfpSendError(((AGHFP_UNRECOGNISED_AT_CMD_IND_T *)message)->aghfp);
        return;

    /* Indication of response hold status request */
    case AGHFP_RESPONSE_HOLD_STATUS_REQUEST_IND:
        LOG_DEBUG(("AGHFP_RESPONSE_HOLD_STATUS_REQUEST_IND:\n"));
        /* Just send OK back since we do not support Response and Hold */
        AghfpSendOk(
                ((AGHFP_RESPONSE_HOLD_STATUS_REQUEST_IND_T *)message)->aghfp);
        return;

    /* Confirmation of sending response hold state request */
    case AGHFP_SEND_RESPONSE_HOLD_STATE_CFM:
        LOG_DEBUG(("AGHFP_SEND_RESPONSE_HOLD_STATE_CFM:\n"));
        return;

    /* Indication of setting response hold status request */
    case AGHFP_SET_RESPONSE_HOLD_STATUS_IND:
        LOG_DEBUG(("AGHFP_SET_RESPONSE_HOLD_STATUS_IND:\n"));
        /* Not in a hold state so don't need AghfpConfirmResponseHoldState so
         * just send OK*/
        AghfpSendOk(((AGHFP_SET_RESPONSE_HOLD_STATUS_IND_T *)message)->aghfp);
        return;

    /* Confirmation of response hold state request */
    case AGHFP_CONFIRM_RESPONSE_HOLD_STATE_CFM:
        LOG_DEBUG(("AGHFP_CONFIRM_RESPONSE_HOLD_STATE_CFM:\n"));
        return;

    /* Indication of indicator activation */
    case AGHFP_INDICATORS_ACTIVATION_IND:
        LOG_DEBUG(("AGHFP_INDICATORS_ACTIVATION_IND:\n"));
        return;

    /* Indication of indicator event reporting */
    case AGHFP_INDICATOR_EVENTS_REPORTING_IND:
        LOG_DEBUG(("AGHFP_INDICATOR_EVENTS_REPORTING_IND: mode[%d] ind[%d]\n",
                   ((AGHFP_INDICATOR_EVENTS_REPORTING_IND_T *)message)->mode,
                   ((AGHFP_INDICATOR_EVENTS_REPORTING_IND_T *)message)->ind));
        return;

    /* HF wants to answer call */
    case AGHFP_ANSWER_IND:
        LOG_DEBUG(("AGHFP_ANSWER_IND:\n"));
        /* Respond with OK */
        AghfpSendOk(((AGHFP_ANSWER_IND_T *)message)->aghfp);
        return;

    /* HF wants to hang up call */
    case AGHFP_CALL_HANG_UP_IND:
        LOG_DEBUG(("AGHFP_CALL_HANG_UP_IND:\n"));
        
        /* Handle Mute/On/Off button press */
        handle_button_event(MUTE_ON_OFF);
        /* Respond with OK */
        AghfpSendOk(((AGHFP_CALL_HANG_UP_IND_T *)message)->aghfp);
        return;

    /* HF wants to dial number */
    case AGHFP_DIAL_IND:
        LOG_DEBUG(("AGHFP_DIAL_IND:\n"));
        return;

    /*
     * For the commands below the AGHFP library sends nothing to the headset
     * until the application answers. Unanswered, the headset waits for
     * OK/ERROR, times out and - depending on the headset - disconnects.
     */

    /* HF wants to dial number from memory (ATD>n) - no phone book */
    case AGHFP_MEMORY_DIAL_IND:
        log_print("BT: AT ATD> (memory dial) - ERROR\n");
        AghfpSendError(((AGHFP_MEMORY_DIAL_IND_T *)message)->aghfp);
        return;

    /* HF wants a last number redial (AT+BLDN) - no phone */
    case AGHFP_LAST_NUMBER_REDIAL_IND:
        log_print("BT: AT+BLDN (redial) - ERROR\n");
        AghfpSendError(((AGHFP_LAST_NUMBER_REDIAL_IND_T *)message)->aghfp);
        return;

    /* Indication of event to get the current calls (AT+CLCC) */
    case AGHFP_CURRENT_CALLS_IND:
    {
        AGHFP_CURRENT_CALLS_IND_T *ind = (AGHFP_CURRENT_CALLS_IND_T *)message;

        if (ind->last_idx == 0)
        {
            /* First: report the call simulated in aghfp_msg_slc_connect_cfm()
             * ("+CLCC: 1,0,0,0,0"). The library then sends this message again
             * with last_idx 1. */
            aghfp_call_info call;

            log_print("BT: AT+CLCC - reporting the active call\n");

            call.idx = 1;
            call.dir = aghfp_call_dir_outgoing;
            call.status = aghfp_call_state_active;
            call.mode = aghfp_call_mode_voice;
            call.mpty = aghfp_call_not_mpty;
            call.type = 0;
            call.size_number = 0;
            call.number = NULL;
            AghfpSendCurrentCall(ind->aghfp, &call);
        }
        else
        {
            /* All calls listed: OK */
            AghfpSendCurrentCallsComplete(ind->aghfp);
        }
        return;
    }

    /* HF asks for the network operator (AT+COPS?) */
    case AGHFP_NETWORK_OPERATOR_IND:
        log_print("BT: AT+COPS? - answering\n");
        AghfpSendNetworkOperator(((AGHFP_NETWORK_OPERATOR_IND_T *)message)->aghfp,
                                 0, sizeof(network_operator) - 1,
                                 network_operator);
        return;

    /* HF wants the AG's echo cancelling/noise reduction off (AT+NREC=0) */
    case AGHFP_NREC_SETUP_IND:
        log_print("BT: AT+NREC=%u - OK\n",
                  ((AGHFP_NREC_SETUP_IND_T *)message)->enable);
        AghfpSendOk(((AGHFP_NREC_SETUP_IND_T *)message)->aghfp);
        return;

    case AGHFP_SUBSCRIBER_NUMBER_IND:
        LOG_DEBUG(("AGHFP_SUBSCRIBER_NUMBER_IND:\n"));
        /* Ignore this message for now */
        LOG_DEBUG(("  - ignored\n"));
        AghfpSendSubscriberNumbersComplete(
                ((AGHFP_SUBSCRIBER_NUMBER_IND_T *)message)->aghfp);
        return;

    case AGHFP_SUBSCRIBER_NUMBER_CFM:
        LOG_DEBUG(("AGHFP_SUBSCRIBER_NUMBER_CFM:\n"));
        return;

    case AGHFP_PHONE_NUMBER_REQUEST_IND:
        LOG_DEBUG(("AGHFP_PHONE_NUMBER_REQUEST_IND:\n"));
        /* Ignore this request, send ERROR */
        LOG_DEBUG(("  - ignored, send ERROR\n"));
        AghfpSendError(((AGHFP_PHONE_NUMBER_REQUEST_IND_T *)message)->aghfp);
        return;

    case AGHFP_SET_CALLER_ID_DETAILS_CFM:
        LOG_DEBUG(("AGHFP_SET_CALLER_ID_DETAILS_CFM:\n"));
        return;

    case AGHFP_CALLER_ID_SETUP_IND:
        LOG_DEBUG(("AGHFP_CALLER_ID_SETUP_IND:\n"));
        return;

    case AGHFP_CALL_WAITING_SETUP_IND:
        LOG_DEBUG(("AGHFP_CALL_WAITING_SETUP_IND:\n"));
        return;

    /*HF indicator reported by HF */
    case AGHFP_HF_INDICATORS_REPORT_IND:
        LOG_DEBUG(("AGHFP_HF_INDICATORS_REPORT_IND HF indicator mask = %x\n",
            ((AGHFP_HF_INDICATORS_REPORT_IND_T*)message)->hf_indicators));
        return;

    case AGHFP_HF_INDICATORS_STATUS_REQUEST_IND:
         LOG_DEBUG(("AGHFP_HF_INDICATORS_STATUS_REQUEST_IND:\n"));
         AghfpHfIndicatorsInitStatusResponse(
                 ((AGHFP_HF_INDICATORS_STATUS_REQUEST_IND_T*)message)->aghfp,
                 aghfp_hf_indicator_on << aghfp_battery_level_mask |
                 aghfp_hf_indicator_on << aghfp_enhanced_safety);
         break;

    case AGHFP_SEND_SIGNAL_INDICATOR_CFM:
        LOG_DEBUG(("AGHFP_SEND_SIGNAL_INDICATOR_CFM:\n"));
        return;

    /* Confirmation of the roaming indicator */
    case AGHFP_SEND_ROAM_INDICATOR_CFM:
        LOG_DEBUG(("AGHFP_SEND_ROAM_INDICATOR_CFM:\n"));
        return;

    /* Confirmation of the caller id */
    case AGHFP_SEND_CALLER_ID_CFM:
        LOG_DEBUG(("AGHFP_SEND_CALLER_ID_CFM %d:\n",
                   ((AGHFP_SEND_CALLER_ID_CFM_T *)message)->status));
        return;

    /* Indication that a call is waiting */
    case AGHFP_SEND_CALL_WAITING_NOTIFICATION_CFM:
        LOG_DEBUG(("AGHFP_SEND_CALL_WAITING_NOTIFICATION_CFM:\n"));
        return;

    /* Indication of remote QTIL supported features */
    case AGHFP_CSR_SUPPORTED_FEATURES_IND:
        LOG_DEBUG(("AGHFP_CSR_SUPPORTED_FEATURES_IND:\n"));
        return;

    /* Indication of QTIL feature negotiation */
    case AGHFP_CSR_FEATURE_NEGOTIATION_IND:
        LOG_DEBUG(("AGHFP_CSR_FEATURE_NEGOTIATION_IND:\n"));
        return;

    /* Indication of QTIL feature negotiation request */
    case AGHFP_CSR_FEATURE_NEGOTIATION_REQ_IND:
        LOG_DEBUG(("AGHFP_CSR_FEATURE_NEGOTIATION_REQ_IND:\n"));
        return;

    default:
        /* Always printed: an unhandled message here may be a request the
         * headset is waiting on */
        log_print("BT: unhandled AGHFP message 0x%04x\n", id);
    }
}
