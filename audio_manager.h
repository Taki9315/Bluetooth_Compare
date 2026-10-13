#ifndef AUDIO_MANAGER_H__
#define AUDIO_MANAGER_H__

#include <csrtypes.h>
#include <hfp.h>


/** Initialise the audio manager subsystem. */
extern void
audio_manager_init(void);


/**
 * Enumeration of Bluetooth audio routes. This enumeration is used to
 * choose whether Bluetooth overrides headset 1 or headset 2 when in
 * Source (AG) mode.
 */
typedef enum
{
    BT_AG_AUDIO_OVERRIDES_HS1,
    BT_AG_AUDIO_OVERRIDES_HS2
} audio_manager_bt_ag_audio_route_t;


/**
 * Connect up audio routing to the Kalimba block.
 *
 * This function should be invoked once, on Kalimba firmware boot.
 */
extern void
audio_manager_connect_kalimba(void);


/**
 * Choose whether Bluetooth overrides headset 1 or headset 2 when in
 * Source (AG) mode.
 *
 * @note This implicitly triggers @ref audio_manager_update_routing().
 *
 * @param bt_ag_audio_route     Specifies the route to use for BT in AG mode.
 */
extern void
audio_manager_set_bt_ag_audio_route(
        audio_manager_bt_ag_audio_route_t bt_ag_audio_route);


/** Default BT speaker boost sent via SET_LEVELS (USB_B.in). Middle of 0/6/12/18/26. */
#define BT_SPK_BOOST_DB_DEFAULT       12u


/** Clamp a stored value to 0, 6, 12, 18, or 26 dB. */
extern uint16
audio_manager_normalize_bt_spk_boost_db(uint16 db);


/** Step BT speaker boost up or down along 0, 6, 12, 18, 26 dB. */
extern uint16
audio_manager_step_bt_spk_boost_db(uint16 current, bool increase);


/**
 * Update the sidetone, mic, and speaker levels in Kalimba.
 */
extern void
audio_manager_update_kalimba_levels(void);


/** Notify Kalimba DSP of SCO connect (TRUE) or disconnect (FALSE). */
extern void
audio_manager_set_hfk_call_state(bool connected);


/** Apply wired codec gains; HS1 is muted while SCO is active. */
extern void
audio_manager_apply_wired_codec_gains(void);


/**
 * This function triggers the audio manager to re-evaluate its routing
 * based on the current state and make any necessary changes.
 *
 * @note This function should not be invoked before
 *       @ref audio_manager_connect_kalimba().
 */
extern void
audio_manager_update_routing(void);


#endif
