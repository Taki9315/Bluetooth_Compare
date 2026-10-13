#ifndef BLUETOOTH_PRIVATE_H__
#define BLUETOOTH_PRIVATE_H__


#include "audio_manager.h"
#include "bluetooth_manager.h"


#include <connection.h>
#include <hfp.h>

#include "aghfp.h"


/**
 * Bluetooth state.
 */
typedef enum bluetooth_state_t
{
    /** Initialising. */
    BLUETOOTH_STATE_INITIALISING,

    /** Idle. */
    BLUETOOTH_STATE_IDLE,

    /** Pairing. */
    BLUETOOTH_STATE_PAIRING,

    /** Connected. */
    BLUETOOTH_STATE_CONNECTED,

    /*----- Source/Audio Gateway states -----*/

    /** Scanning. */
    BLUETOOTH_STATE_SCANNING,

    /** Connecting. */
    BLUETOOTH_STATE_CONNECTING,

    /*----- Sink/Headset states -------------*/

    /** Pairable. */
    BLUETOOTH_STATE_PAIRABLE,

    /** Connectable. */
    BLUETOOTH_STATE_CONNECTABLE
} bluetooth_state_t;


/**
 * Bluetooth data structure definition.
 */
typedef struct bluetooth_data_t
{
    /** Bluetooth task pointer (points to \ref bluetooth_task). */
    Task task;

    /** State. */
    bluetooth_state_t state;

    /** Mode (Source/Audio Gateway or Sink/Headset). */
    bluetooth_mode_t mode;

    /** Tracks whether Bluetooth is on (TRUE) or off (FALSE). */
    bool on;

    /** Boolean value indicating whether \ref prev_addr_headset is valid. */
    bool prev_addr_headset_valid;

    /** BD_ADDR of previously connected headset. */
    bdaddr prev_addr_headset;

    /** Boolean value indicating whether \ref prev_addr_phone is valid. */
    bool prev_addr_phone_valid;

    /** BD_ADDR of previously connected phone. */
    bdaddr prev_addr_phone;

    /** Indicates if a sync is in progress. */
    bool sync_in_progress;

    /** The active audio sink (NULL if no audio connection active). */
    Sink active_sink;
} bluetooth_data_t;


/** Instance of Bluetooth data. */
extern bluetooth_data_t bluetooth_data;


/*---------- HFP Headset block functions ---------- */


/**
 * Initialise Bluetooth HFP Headset.
 *
 * @note This should be invoked during device initialisation. \ref
 *       bluetooth_manager_hfp_initialised() will be invoked by the HFP
 *       subsystem as a result.
 */
extern void
bluetooth_hfp_init(void);


/**
 * Invoked by the HFP block to indicate that it has finished initialisation.
 */
extern void
bluetooth_manager_hfp_initialised(void);


/**
 * Handle Bluetooth HFP messages.
 *
 * @param task      The task relevant to the message.
 * @param id        Message ID.
 * @param message   Message content.
 */
extern void
bluetooth_hfp_msg_handler(Task task, MessageId id, Message message);


/**
 * Invoked by the HFP block to indicate success or failure of a new connection.
 *
 * @param success   TRUE if the connection completed successfully, else FALSE.
 * @param addr      If the connection was successful then this gives the address
 *                  of the device which connected.
 */
extern void
bluetooth_manager_hfp_connection_start(bool success, bdaddr *addr);


/**
 * Disconnect from the connected audio gateway.
 */
extern void
bluetooth_hfp_disconnect(void);


/*---------- AGHFP block functions ---------- */


/**
 * Initialise Bluetooth HFP Audio Gateway.
 *
 * \note This should be invoked during device initialisation. \ref
 *       bluetooth_manager_hfp_initialised() will be invoked by the HFP
 *       subsystem as a result.
 */
extern void
bluetooth_aghfp_init(void);


/**
 * Invoked by the AGHFP block to indicate that it has finished initialisation.
 */
extern void
bluetooth_manager_aghfp_initialised(void);


/**
 * Handle AGHFP messages.
 *
 * @param task      The task the message is associated with.
 * @param id        The message ID.
 * @param message   The message data.
 */
extern void
bluetooth_aghfp_msg_handler(Task task, MessageId id, Message message);


/**
 * Process inquiry results to find a device to connect with.
 *
 * @param result     A result from the inquiry.
 */
extern void
bluetooth_aghfp_process_inquiry_result(CL_DM_INQUIRE_RESULT_T *result);


/**
 * Called by the Bluetooth Manager when an inquiry round completes
 * (CL_DM_INQUIRE_RESULT with status inquiry_status_ready) and the manager
 * is still in BLUETOOTH_STATE_SCANNING.
 *
 * If a CoD-only inquiry candidate has been held back during the round
 * (no EIR UUID16 list, but CoD looks like a HFP HF device) the AGHFP
 * block will issue an SDP service search for the HFP_HF UUID and return
 * TRUE.  The manager MUST NOT restart the inquiry while this verification
 * is in flight; the result will arrive via
 * bluetooth_aghfp_sdp_search_cfm().
 *
 * If no candidate is pending the function returns FALSE and the manager
 * should resume scanning as before.
 */
extern bool
bluetooth_aghfp_inquiry_complete(void);


/**
 * Handle the SDP service search confirmation that follows an HFP_HF
 * verification request started in bluetooth_aghfp_inquiry_complete().
 *
 * @param message   The CL_SDP_SERVICE_SEARCH_CFM message body.
 *
 * @returns TRUE if the AGHFP block consumed the message (the search was
 *          ours), FALSE if it belongs to some other subsystem.
 */
extern bool
bluetooth_aghfp_sdp_search_cfm(CL_SDP_SERVICE_SEARCH_CFM_T *message);


/**
 * Invoked by the AGHFP block when an SDP verification of a CoD-only
 * fallback candidate failed (the remote does not advertise HFP_HF).
 * The manager should resume scanning.
 */
extern void
bluetooth_manager_aghfp_sdp_verification_failed(void);


/**
 * Invoked by the AGHFP block to indicate that it is attempting to pair to a
 * device.
 */
extern void
bluetooth_manager_aghfp_pairing(void);


/**
 * Invoked by the AGHFP block to indicate that it is has established an audio
 * connection.
 *
 * @param addr  The address of the device the connection was made to.
 */
extern void
bluetooth_manager_aghfp_connected(bdaddr *addr);


/**
 * Invoked by the AGHFP block to indicate that a connection attempt has failed.
 */
extern void
bluetooth_manager_aghfp_connection_failed(void);


/**
 * Invoked by the AGHFP block when the Service Level Connection to the headset
 * has gone (AGHFP_SLC_DISCONNECT_IND).
 */
extern void
bluetooth_manager_aghfp_slc_disconnected(void);


/**
 * Invoked by the HFP or AGFP block to indicate that the active audio
 * Sink has changed.
 *
 * @param sink  The HFP audio Sink (\c NULL if the audio link has
 *              been disconnected).
 */
extern void
bluetooth_manager_sink_changed(Sink sink);



/**
 * Invoked by the Bluetooth Manager to get AGHFP to attempt to connect to a
 * previously connected headset.
 *
 * @param addr  The address of the device to connect to.
 *
 * @returns TRUE if the connection was started, FALSE if AGHFP is still busy
 *          with a connection (the caller should try again later).
 */
extern bool
bluetooth_manager_aghfp_connect(bdaddr *addr);


/**
 * Disconnect from the connected headset.
 */
extern void
bluetooth_aghfp_disconnect(void);

/* Define button events */
#define VOLUME_UP      0x01
#define VOLUME_DOWN    0x02
#define MUTE_ON_OFF    0x03
#define NO_BUTTON      0x00

extern void
handle_button_event(uint16 button_event);

#endif
