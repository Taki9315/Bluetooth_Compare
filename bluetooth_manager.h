#ifndef BLUETOOTH_MANAGER_H__
#define BLUETOOTH_MANAGER_H__


#include <csrtypes.h>


/** Enumerates the Bluetooth modes. */
typedef enum bluetooth_mode_t
{
    /** Source (Audio Gateway). */
    BLUETOOTH_MODE_SOURCE,

    /** Sink (Headset). */
    BLUETOOTH_MODE_SINK
} bluetooth_mode_t;


/**
 * Initialise the Bluetooth Manager.
 *
 * \note This must be invoked before any other function in this subsystem.
 */
extern void
bluetooth_manager_init(void);


/**
 * Turn Bluetooth on/off.
 *
 * @param on    TRUE for on, FALSE for off.
 */
extern void
bluetooth_manager_on(bool on);


/**
 * Set Bluetooth mode (Source/Audio Gateway or Sink/Headset).
 *
 * @param mode  The mode to enter.
 */
extern void
bluetooth_manager_set_mode(bluetooth_mode_t mode);


/**
 * Get the current Bluetooth mode (Source/Audio Gateway or Sink/Headset).
 *
 * @returns the current mode.
 */
extern bluetooth_mode_t
bluetooth_manager_get_mode(void);


/**
 * Get the Sink for the current Bluetooth HFP (AG or HS) connection.
 *
 * @returns the Sink for the active connection if there is one, else NULL.
 */
extern Sink
bluetooth_manager_get_hfp_sink(void);


/**
 * Turn source mode sync on/off.
 *
 * @param on    TRUE for on, FALSE for off.
 */
extern void
bluetooth_manager_source_mode_sync(bool on);


/**
 * Turn sink mode sync on/off.
 *
 * @param on    TRUE for on, FALSE for off.
 */
extern void
bluetooth_manager_sink_mode_sync(bool on);


/**
 * Indicates if a sync is currently in progress
 *
 * @returns TRUE if a sync is in progress, else FALSE.
 */
extern bool
bluetooth_sync_in_progress(void);

#endif
