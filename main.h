#ifndef MAIN_H__
#define MAIN_H__


/**
 * Bluetooth events to indicate to main.
 */
typedef enum main_bt_cb_events_t
{
    /** Device paired. */
    MAIN_BT_CB_PAIRED
} main_bt_cb_events_t;


/**
 * Called when a Bluetooth event occurs that the main application needs to be
 * notified about.
 *
 * @param event     The event code for the Bluetooth event that just occured.
 */
extern void
main_bluetooth_callback(main_bt_cb_events_t event);

#endif
