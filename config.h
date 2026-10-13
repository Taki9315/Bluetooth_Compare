#ifndef CONFIG_H__
#define CONFIG_H__


#include <stdlib.h>
#include <vm.h>


/** Length of the local_name array. */
#define BT_AT_DEVICE_NAME_LENGTH (32)


/*
 * -------------------------------------------------------------------------
 *                        Configuration keys
 * -------------------------------------------------------------------------
 *
 * These configuration keys correspond to the USR PSKEYs.  The indexes
 * match up to those used by the Sink example application.
 */


/* Persistent store key allocation. */
#define CONFIG_BASE  (0)


/**
 * Enumeration of configuration keys.  These correspond to USR PSKEYs.
 */
typedef enum
{
    CONFIG_SESSION_DATA        = CONFIG_BASE,
    CONFIG_APP                 = 1
} config_ids;



/** Application-specific configuration data structure. */
typedef struct app_config
{
    /** AGHFP-specific data. */
    struct
    {
        /** Boolean value indicating whether \ref prev_addr_headset is valid. */
        bool prev_addr_headset_valid;

        /** BD_ADDR of previously connected headset. */
        bdaddr prev_addr_headset;
    } aghfp;

    /** HFP-specific data */
    struct
    {
        /** Boolean value indicating whether \ref prev_addr_phone is valid. */
        bool prev_addr_phone_valid;

        /** BD_ADDR of previously connected phone. */
        bdaddr prev_addr_phone;
    } hfp;

    /** Audio-specific data */
    struct
    {
        uint16 pio_volume;
        uint16 pio_volume_max;
        uint16 pio_volume_min;
        uint16 pio_side;
        uint16 pio_volumeb;
        uint16 pio_volume_max2;
        uint16 pio_volume_min2;
        uint16 pio_involume;
        uint16 pio_involumeb;

        /** BT headset speaker boost (dB): 0, 6, 12, 18, or 26. Wired PIO volumes
         *  are unaffected; this only drives Kalimba SET_LEVELS (USB_B.in). */
        uint16 bt_spk_boost_db;

      /** Boolean value indicating whether the above settings are valid. */
        bool valid;
    } audio;
} app_config_t;


/** Data structure for configuration data loaded from persistent store. */
typedef struct config
{
    /** Local BD_ADDR. */
    bdaddr local_bd_addr;

    /** Application configuration. */
    app_config_t app;
} config_t;


/** Global configuration store. */
extern const config_t *cfg;


/**
 * Enumeration of status codes.
 */
typedef enum status
{
    STATUS_OK                                           = 0,
    STATUS_INSUFFICIENT_SPACE                           = 1,
    STATUS_MEMORY_ALLOCATION_FAILURE                    = 2,
    STATUS_PS_READ_FAILURE                              = 3,
    STATUS_PS_WRITE_FAILURE                             = 4,
    STATUS_INCOMPLETE                                   = 5,
    STATUS_DISABLED                                     = 6,
    STATUS_INVALID_ARGUMENT                             = 7,
    STATUS_INVALID_STATE                                = 8,
    STATUS_OTHER_ERROR                                  = 0xFF
} status_t;


/**
 * Initialise the configuration manager, loading configuration data.
 */
extern status_t
config_manager_init(void);


/**
 * Factory reset the configuration.
 *
 * @returns STATUS_OK on success, else an appropriate error code.
 */
extern status_t
config_manager_factory_reset(void);


/**
 * This function returns a modifiable copy of the app config that can
 * be used to make changes to and stored back to app config by invoking
 * @ref config_manager_store_app_config() or
 * @ref config_manager_store_temporary_app_config().
 *
 * @returns A copy of the current application config as stored in
 *          persistent store (i.e., without any temporary changes)
 *          in malloc'd memory. NULL if memory allocation failed.
 */
extern app_config_t *
config_manager_get_app_config_copy(void);


/**
 * Update the Application configuration data in persistent store.
 *
 * @param new_app_config    The new Application configuration data to
 *                          write into persistent store. This must be
 *                          in malloc'd memory as it will be freed by
 *                          this function.
 *
 * @returns STATUS_OK on success, else an appropriate error code.
 */
extern status_t
config_manager_store_app_config(app_config_t *new_app_config);


/**
 * Update the Application configuration in RAM without modifying
 * persistent store. This can be undone by executing
 * @ref config_manager_restore_app_config().
 *
 * @param new_app_config    The new Application configuration data to
 *                          write into RAM.
 *
 * @return STATUS_OK on success, else an appropriate error code.
 */
extern status_t
config_manager_store_temporary_app_config(app_config_t *new_app_config);


/**
 * Restore Application configuration in RAM from persistent store.
 *
 * @return STATUS_OK on success, else an appropriate error code.
 */
extern status_t
config_manager_restore_app_config(void);


#endif

