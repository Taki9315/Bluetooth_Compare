#include "config_private.h"

#include <stdlib.h>
#include <string.h>

#include <connection.h>
#include <panic.h>
#include <ps.h>


/* Uncomment this to enable logging in this file */
/* #define DEBUG_ENABLED */
#include "log.h"


/* Global configuration store (read only for public access) */
const config_t *cfg;
/* Global configuration store (read/write for private access) */
config_t *config_rw;


/**
 * Load BD_ADDR from PS.
 *
 * @returns STATUS_OK if the BD_ADDR was retreived, else STATUS_PS_READ_FAILURE.
 */
static status_t
get_local_bdaddr_from_ps(void)
{
    uint16 size = sizeof(config_rw->local_bd_addr);

    if (size == PsFullRetrieve(PSKEY_BDADDR, &config_rw->local_bd_addr, size))
    {
        LOG_DEBUG(("CONF: PSKEY_BDADDR [%04x %02x %06lx]\n",
                    config_rw->local_bd_addr.nap,
                    config_rw->local_bd_addr.uap,
                    config_rw->local_bd_addr.lap));
        return STATUS_OK;
    }
    else
    {
        memset(&config_rw->local_bd_addr, 0, sizeof(config_rw->local_bd_addr));
        return STATUS_PS_READ_FAILURE;
    }
}


/**
 * Load Application-specific config from PS.
 *
 * @returns STATUS_OK.
 */
static status_t
get_app_config_from_ps(void)
{
    uint16 psr_length;

    psr_length = PsRetrieve(CONFIG_APP,
                            &config_rw->app, sizeof(config_rw->app));
    if (psr_length != sizeof(config_rw->app))
    {
        LOG_DEBUG(("Failed to read configuration: APP - using defaults\n"));
        memset(&config_rw->app, 0, sizeof(config_rw->app));
        config_rw->app.hfp.prev_addr_phone_valid = FALSE;
        config_rw->app.aghfp.prev_addr_headset_valid = FALSE;
    }

    return STATUS_OK;
}



status_t
config_manager_init(void)
{
    status_t status;

    config_rw = calloc(sizeof(*config_rw), 1);

    if (config_rw == NULL)
        return STATUS_MEMORY_ALLOCATION_FAILURE;

    cfg = config_rw;

    status = get_app_config_from_ps();
    if (status != STATUS_OK)
        return status;

    status = get_local_bdaddr_from_ps();
    if (status != STATUS_OK)
        return status;

    return STATUS_OK;
}


status_t
config_manager_factory_reset(void)
{
    /* Clear the CONFIG_APP user key in PS */
    PsStore(CONFIG_APP, NULL, 0);

    ConnectionSmDeleteAllAuthDevices(0);

    return STATUS_OK;
}


app_config_t *
config_manager_get_app_config_copy(void)
{
    app_config_t *app_config = (app_config_t *)malloc(sizeof(*app_config));
    if (app_config == NULL)
        return NULL;

    memcpy(app_config, &(cfg->app), sizeof(*app_config));
    return app_config;
}


status_t
config_manager_store_app_config(app_config_t *new_app_config)
{
    status_t status;
    uint16 len = PsStore(CONFIG_APP, new_app_config, sizeof(*new_app_config));
    if (len != sizeof(*new_app_config))
    {
        LOG_DEBUG(("Failed to write app config to PS\n"));
        status = STATUS_PS_WRITE_FAILURE;
        goto done;
    }

    status = get_app_config_from_ps();
    if (status != STATUS_OK)
    {
        LOG_DEBUG(("Failed on readback of app config from PS\n"));
        goto done;
    }

    if (memcmp(new_app_config, &(config_rw->app), sizeof(config_rw->app)))
    {
        LOG_DEBUG(("Failed on readback of app config from PS (mismatch)\n"));
        status = STATUS_PS_READ_FAILURE;
        goto done;
    }

    status = STATUS_OK;

done:
    free(new_app_config);
    return status;
}


status_t
config_manager_store_temporary_app_config(app_config_t *new_app_config)
{
    memcpy(&(config_rw->app), new_app_config, sizeof(config_rw->app));
    return STATUS_OK;
}


/**
 * Restore Application configuration in RAM from persistent store.
 */
status_t
config_manager_restore_app_config(void)
{
    return get_app_config_from_ps();
}


/** \} */
