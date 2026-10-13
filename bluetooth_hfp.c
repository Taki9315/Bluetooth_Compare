#include "bluetooth_private.h"


#include <hfp.h>
#include <panic.h>
#include <stdio.h>
#include <string.h>


/* Uncomment this to enable logging in this file */
#define DEBUG_ENABLED
#include "log.h"


/**
 * Handle the HFP_INIT_CFM event (resulting from HfpInit()).
 *
 * @param cfm   The HFP init confirm.
 */
static void
handle_hfp_init_cfm(HFP_INIT_CFM_T *cfm)
{
    LOG_DEBUG(("HFP initialised, status %u\n", cfm->status));
    /*
     * According to the API (hfp_init_status enum in src\lib\hfp\hfp.h of the
     * ADK) errors after hfp_init_reinit_fail are fatal. hfp_init_reinit_fail
     * simply means that HFP is already initialised.
     */
    PanicFalse(cfm->status <= hfp_init_reinit_fail);
    bluetooth_manager_hfp_initialised();
}


/**
 * Handle the HFP_SLC_CONNECT_CFM event.
 *
 * @param cfm   The HFP SLC connect confirm.
 */
static void
handle_hfp_slc_connect_cfm(HFP_SLC_CONNECT_CFM_T *cfm)
{
    /* See the hfp_connect_status enum in src\lib\hfp\hfp.h of the ADK for
     * cfm->status values. */
    if (cfm->status == hfp_connect_success)
    {
        LOG_DEBUG(("HFP connection established\n"));
        bluetooth_manager_hfp_connection_start(TRUE, &(cfm->bd_addr));
    }
    else
    {
        LOG_DEBUG(("HFP connect failed with status : %u\n", cfm->status));
        bluetooth_manager_hfp_connection_start(FALSE, NULL);
    }
}


/**
 * Handle the HFP_CALL_STATE_IND event.
 *
 * This indicates changes in call state.
 *
 * @param ind   The HFP call state indication.
 */
static void
handle_hfp_call_state_ind(HFP_CALL_STATE_IND_T *ind)
{
    /* See the hfp_call_state enum in src\lib\hfp\hfp.h of the ADK for
     * ind->call_state values. */
    if (ind->call_state == hfp_call_state_idle)
        LOG_DEBUG(("Call ended\n"));
    else if (ind->call_state >= hfp_call_state_active)
        LOG_DEBUG(("Call started %u\n", ind->call_state));
    else
        LOG_DEBUG(("Call state change %u\n", ind->call_state));
}


/**
 * Handle the HFP_RING_IND event.
 *
 * This indicates an incoming call.
 *
 * @param ind   The HFP ring indication.
 */
static void
handle_hfp_ring_ind(HFP_RING_IND_T *ind)
{
    LOG_DEBUG(("Ring ring\n"));
}


/**
 * Handle the HFP_AUDIO_CONNECT_CFM event.
 *
 * This indicates that the audio connection attempt has succeeded or failed.
 *
 * @param cfm   The HFP audio connect indication.
 */
static void
handle_hfp_audio_connect_cfm(HFP_AUDIO_CONNECT_CFM_T *cfm)
{
    Sink hfp_sink;

    if (cfm->status != hfp_audio_connect_success)
    {
        LOG_DEBUG(("HFP_AUDIO_CONNECT_CFM failed %u\n", cfm->status));
        return;
    }

    LOG_DEBUG(("HFP_AUDIO_CONNECT_IND\n"));
    HfpLinkGetAudioSink(cfm->priority, &hfp_sink);
    bluetooth_manager_sink_changed(hfp_sink);
}


/**
 * Handle the HFP_AUDIO_CONNECT_IND event.
 *
 * This indicates that the AG is requesting an audio connection.
 *
 * @param ind   The HFP audio connect indication.
 */
static void
handle_hfp_audio_connect_ind(HFP_AUDIO_CONNECT_IND_T *ind)
{
    /* Accept the audio connection. We will get an HFP_AUDIO_CONNECT_CFM which
     * will indicate success (or otherwise) */
    HfpAudioConnectResponse(ind->priority, TRUE, sync_all_sco | sync_all_esco,
                            NULL, FALSE);
}


/**
 * Handle the HFP_AUDIO_DISCONNECT_IND event.
 *
 * This indicates that the audio has disconnected.
 *
 * @param ind   The HFP audio disconnect indication.
 */
static void
handle_hfp_audio_disconnect_ind(HFP_AUDIO_DISCONNECT_IND_T *ind)
{
    bluetooth_manager_sink_changed(NULL);
    LOG_DEBUG(("HFP_AUDIO_DISCONNECT_IND\n"));
}


/**
 * Handle the HFP_VOLUME_SYNC_SPEAKER_GAIN_IND event.
 *
 * This indicates changes in speaker volume.
 *
 * @param ind   The HFP volume sync speaker gain indication.
 */
static void
handle_hfp_sync_speaker_gain_ind(HFP_VOLUME_SYNC_SPEAKER_GAIN_IND_T *ind)
{
    LOG_DEBUG(("HFP volume gain = %d\n", ind->volume_gain));
}


/* Handles HFP events. The events are enumerated in the HfpMessageId enum in
 * src\lib\hfp\hfp.h of the ADK. */
void
bluetooth_hfp_msg_handler(Task task, MessageId id, Message message)
{
    switch (id)
    {
    case HFP_INIT_CFM:
        handle_hfp_init_cfm((HFP_INIT_CFM_T *)message);
        break;

    case HFP_SLC_CONNECT_CFM:
        handle_hfp_slc_connect_cfm((HFP_SLC_CONNECT_CFM_T *)message);
        break;

    case HFP_SLC_CONNECT_IND:
        /* 0x5a02 */
        LOG_DEBUG(("HFP_SLC_CONNECT_IND\n"));
        break;

    case HFP_SLC_DISCONNECT_IND:
        /* 0x5a04 */
        LOG_DEBUG(("HFP_SLC_DISCONNECT_IND\n"));
        break;

    case HFP_CALL_STATE_IND:
        handle_hfp_call_state_ind((HFP_CALL_STATE_IND_T *)message);
        break;

    case HFP_RING_IND:
        handle_hfp_ring_ind((HFP_RING_IND_T *)message);
        break;

    case HFP_AUDIO_CONNECT_CFM:
        handle_hfp_audio_connect_cfm((HFP_AUDIO_CONNECT_CFM_T *)message);
        break;

    case HFP_AUDIO_CONNECT_IND:
        handle_hfp_audio_connect_ind((HFP_AUDIO_CONNECT_IND_T *)message);
        break;

    case HFP_AUDIO_DISCONNECT_IND:
        handle_hfp_audio_disconnect_ind((HFP_AUDIO_DISCONNECT_IND_T *)message);
        break;

    case HFP_VOLUME_SYNC_SPEAKER_GAIN_IND:
        handle_hfp_sync_speaker_gain_ind(
                (HFP_VOLUME_SYNC_SPEAKER_GAIN_IND_T *)message);
        break;

    case HFP_SERVICE_IND:
        /* 0x5a08 */
        LOG_DEBUG(("HFP_SERVICE_IND\n"));
        break;

    case HFP_SIGNAL_IND:
        /* 0x5a09 */
        LOG_DEBUG(("HFP_SIGNAL_IND\n"));
        break;

    case HFP_ROAM_IND:
        /* 0x5a0a */
        LOG_DEBUG(("HFP_ROAM_IND\n"));
        break;

    case HFP_BATTCHG_IND:
        /* 0x5a0b */
        LOG_DEBUG(("HFP_BATTCHG_IND\n"));
        break;

    case HFP_CURRENT_CALLS_CFM:
        /* 0x5a2b */
        LOG_DEBUG(("HFP_CURRENT_CALLS_CFM\n"));
        break;

    default:
        LOG_DEBUG(("Unhandled HFP message 0x%04x\r\n", id));
        break;
    }
}


void
bluetooth_hfp_disconnect(void)
{
    HfpSlcDisconnectRequest(hfp_primary_link);
}


void
bluetooth_hfp_init(void)
{
    hfp_init_params hfp_params;

    memset(&hfp_params, 0, sizeof(hfp_init_params));

    hfp_params.supported_profile = hfp_handsfree_and_headset;

    HfpInit(bluetooth_data.task, &hfp_params, NULL);

    /*
     * Set codecs to CVSD. The second parameter is whether
     * to send a notification, and as we don't have a remote device,
     * we will provide FALSE
     */
    HfpWbsSetSupportedCodecs(hfp_wbs_codec_mask_cvsd, FALSE);
    LOG_DEBUG(("HFP initialising\r\n"));
}
