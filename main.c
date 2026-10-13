/***********************  Multivoice *********************
 *                                                       *
 *  Wired ADC/DAC to I2S                                 *
 ********************************************************/
/* used by receive only */
/* Includes */
#include <audio.h>
#include <file.h>
#include <string.h>
#include <panic.h>
#include <source.h>
#include <sink.h>
#include <stream.h>
#include <connection.h>
#include <pblock.h>
#include <psu.h>
#include <pio.h>
#include <charger.h>
#include <print.h>
#include <message.h>
#include <kalimba.h>

#include "csr_ag_audio_plugin.h"
#include "audio_manager.h"
#include "bluetooth_manager.h"
#include "config.h"
#include "kalimba_constants.h"

#include "bluetooth_private.h"

#include "main.h"

/* Uncomment this to enable logging in this file */
/* #define DEBUG_ENABLED */
#include "log.h"


/** If defined then test buttons will be enabled for the cns10001. */
/* #define PIO_TESTING */

#ifdef PIO_TESTING

/* Define test PIO button masks */

/** Bit mask for PIO button to toggle bluetooth on and off. */
#define BUTTON_TOGGLE_BLUETOOTH_MASK         (1 << 10)

/** Bit mask for PIO button that toggles the mode between source and sink. */
#define BUTTON_TOGGLE_MODE_MASK              (1 << 11)

/** Bit mask for PIO button that toggles source on and off. */
#define BUTTON_TOGGLE_SOURCE_MASK            (1 << 12)

/** Bit mask for PIO button that toggles sink on and off. */
#define BUTTON_TOGGLE_SINK_MASK              (1 << 13)

#endif

/* Defines */
/*#define VC_MASK    0xC5C0      bit6,7,8,10,14,15--- bits 1 and 3 */
#define VC_MASK  0xC5C0     /* bit6,7,8,10,14,15--- bits 1 and 3 */
#define VC_MASKB 0xC500     /* bit8,10,14,15--- bits 1 and 3 */
#define VC_MASK3 0xC540     /* bit8,10,14,15--- bits 1 and 3 */
#define VC_BIT3  0x0080     /* bit 3 used for clocking etc */
#define VC_BIT1  0x0040     /* bit 1 used for clocking etc */ 
#define VC_vol   0x0000     /*0 if vc_maskb = C5 when 000 then use VC_vol*/
#define VC_side  0x0100     /*1 if vc_maskb = C5 when 00100 then use VC_side*/
#define VC_vol2   0x0400     /*2 if vc_maskb = C5 when 00400 then use VC_xxx*/
#define VC_bi2c   0x0500     /*3 if vc_maskb = C5 when 00500 then use VC_xxx Bluetooth I2S select*/
#define VC_bls   0x4000     /*4 if vc_maskb = C5 when 04000 then use VC_xxx Bluetooth for source mode sync*/
#define VC_bypk   0x4100     /*5 if vc_maskb = C5 when 04100 then use VC_xxx bypass kalimba main channel*/
/*#define VC_bypk2   0x4400     6 if vc_maskb = C5 when 04400 then use VC_xxx Bypass kalimba for 2nd audio channel control*/
#define VC_rst    0x4400     /* 6 if vc_maskb = C5 when 04400 then use VC_xxx factory reset*/
#define VC_wir_blu   0x4500     /*7 if vc_maskb = C5 when 04500 then use VC_xxx use wired/bluetooth on main*/
#define VC_sp1r   0x08000     /*8 if vc_maskb = C5 when 08000 then use VC_xxx speaker 1 volume range control*/
#define VC_mic1   0x08100     /*9 if vc_maskb = C5 when 08100 then use VC_xxx microphone 1 input gain control*/
#define VC_sp2r   0x08400     /*A if vc_maskb = C5 when 08400 then use VC_xxx speaker 2 volume range control*/
#define VC_mic2   0x08500     /*B if vc_maskb = C5 when 08500 then use VC_xxx microphone 2 input gain control*/
#define VC_bsms   0x0C000     /*C if vc_maskb = C5 when 0C000 then use VC_xxx Sync for Bluetooth for sink mode sync*/
#define VC_bbnc   0x0C100     /*D if vc_maskb = C5 when 0C100 then use VC_xxx BT speaker boost (6 dB steps via SET_LEVELS)*/
#define VC_bmode  0x0C400     /*E if vc_maskb = C5 when 0C400 then use VC_xxx Switch between source and sync modes*/
#define VC_blueack  0x0C500  /*if vc_maskb = C5 when 0C500 then respond with 1 on csr pio7 in output mode to show bluetooth paired OK*/


#define VC_DELAY 20         /* ms */
#define PIO_VOLUME_MIN         6      /* ???????????????????????????? */
#define PIO_VOLUME_MAX         16      /*22 is max ???????????????????????????? */
#define PIO_VOLUME_MAXMAX      16      /*22 is max ???????????????????????????? */
#define PIO_VOLUME_MINMIN      6
#define PIO_VOLUME_INC         1      /* ???????????????????????????? */
#define PIO_VOLUME_DEFAULT     13      /* ???????????????????????????? */
#define PIO_VOLUME_MINDEFAULT  6      /* ???????????????????????????? */
#define PIO_VOLUME_MAXDEFAULT  16      /*22 is max ???????????????????????????? */
#define PIO_side_MIN           0      /* ???????????????????????????? */
#define PIO_side_MAX           7      /* ???????????????????????????? */
#define PIO_side_INC           1      /* ???????????????????????????? */
#define PIO_side_DEFAULT       0      /* ???????????????????????????? */
#define PIO_VOLUME_MIN2        6      /* ???????????????????????????? */
#define PIO_VOLUME_MAX2        16      /*22 is max ???????????????????????????? */
#define PIO_VOLUME_MAXMAX2     16      /*22 is max ???????????????????????????? */
#define PIO_VOLUME_MINMIN2     6
#define PIO_VOLUME_INC2        1      /* ???????????????????????????? */
#define PIO_VOLUME_DEFAULT2    10      /* ????????????????????????????10 */
#define PIO_VOLUME_MINDEFAULT2 6      /* ???????????????????????????? */
#define PIO_VOLUME_MAXDEFAULT2 16      /*22 is max ???????????????????????????? */
#define PIO_inVOLUME_MIN       0      /* ???????????????????????????? */
#define PIO_inVOLUME_MAX       CODEC_INPUT_GAIN_RANGE      /* ???????????????????????????? */
#define PIO_inVOLUME_INC       1      /* ???????????????????????????? */
#define PIO_inVOLUME_DEFAULT   15      /* ???????????????????????????? */
#define PIO_inVOLUME_MIN2      0      /* ???????????????????????????? */
#define PIO_inVOLUME_MAX2      CODEC_INPUT_GAIN_RANGE      /* ???????????????????????????? */
#define PIO_inVOLUME_INC2      1      /* ???????????????????????????? */
#define PIO_inVOLUME_DEFAULT2  3        /*15  9 = 0, 10 = 3db, 11=6, 12 = 9db*/
/* 9 = 0, 10 = 3db, 11=6, 12 = 9db */

/** Blueack pin high delay in ms. */
#define VC_BLUEACK_DELAY       (90)

/* Timer for button release detection */
#define BUTTON_RELEASE_TIMER_DELAY 100  /* 3000ms */

/** Blueack PIO mask. */
#define VC_BLUEACK_PIO_MASK    (1 << 0)
#define VC_PIO0_MASK           (1 << 0)
#define VC_PIO1_MASK           (1 << 1)


/** Bitflag to indicate that a sync is in progress. */
#define VC_SYNC_IN_PROGRESS        (0x0000)

/** Bitflag to indicate that a sync is not in progress.*/
#define VC_SYNC_NOT_IN_PROGRESS    (0x0100)


/* Globals */
TaskData KalVCPTask;
TaskData PioTask;
TaskData BlueackTask;
TaskData button_release_task;  /* Timer task for button release */

/** Waiting to send a blueack response if pairing is successful. */
static bool waiting_for_blueack_response = FALSE;

/** Delay (in PIO loop ticks) before a radio sync made while the headset was
 * already connected is answered with "paired OK" - a real pairing also
 * takes a while, so the radio is listening by then. */
#define SYNC_ACK_DELAY_TICKS   (1000 / VC_DELAY)

/** Ticks left until that answer, 0 = none pending. */
static uint16 sync_ack_ticks = 0;

/** PIO loop ticks between ALIVE (heartbeat) log lines: ~10 s. */
#define ALIVE_LOG_TICKS        (10000 / VC_DELAY)

/* Typedefs */
typedef struct
{
    uint16 id;
    uint16 a;
    uint16 b;
    uint16 c;
    uint16 d;
} KAL_MESSAGE;

/** 
 * Set blueack PIO high or low
 *
 * @param high  If @c TRUE set the blueack PIO high, else set it to low.
 */
static void
set_blueack_pio(bool high)
{
    if (high)
    {
        /* Set PIO0 HIGH */
        PioSet32(VC_BLUEACK_PIO_MASK, VC_BLUEACK_PIO_MASK);
    }
    else
    {
        /* Set PIO0 LOW */
        PioSet32(VC_BLUEACK_PIO_MASK, 0);
    }

    /* Ensure PIO0 is always configured as an output */
    PioSetDir32(VC_BLUEACK_PIO_MASK, VC_BLUEACK_PIO_MASK);
}


/**
 * Blueack task handler
 *
 * @param task      The task the message is associated with.
 * @param id        The message ID.
 * @param message   The message data.
 */
static void
blueack_handler(Task task, MessageId id, Message payload)
{
    LOG_DEBUG(("%s\r\n", __FUNCTION__));
    set_blueack_pio(FALSE);
    
    UNUSED(task);
    UNUSED(id);
    UNUSED(payload);
}


/**
 * Cancel blueack request.
 */
static void
blueack_cancel(void)
{
    (void)MessageCancelAll(&BlueackTask, 0);
    
    set_blueack_pio(FALSE);    
    waiting_for_blueack_response = FALSE;
    sync_ack_ticks = 0;
}

/* Handle button events */
void handle_button_event(uint16 button_event)
{
    if (bluetooth_data.state != BLUETOOTH_STATE_CONNECTED)
    {
        LOG_DEBUG(("Ignoring bluetooth button event cause it is in PARING mode.\n"));
        return;
    }

    LOG_DEBUG(("Setting PIO according to the bluetooth button event(%d).\n", button_event));
    
    /* Reset the button release timer */
    /* MessageCancelAll(&button_release_task, 0);  Cancel any pending timer */
    /* MessageSendLater(&button_release_task, 0, NULL, BUTTON_RELEASE_TIMER_DELAY); */

    if (button_event == VOLUME_UP)
    {
        PioSet32(VC_PIO0_MASK, VC_PIO0_MASK);
        PioSetDir32(VC_PIO0_MASK, VC_PIO0_MASK);
        MessageSendLater(&button_release_task, 0 /* Custom message */, 0, BUTTON_RELEASE_TIMER_DELAY);
    }
    else if (button_event == VOLUME_DOWN)
    {
        PioSet32(VC_PIO1_MASK, VC_PIO1_MASK);
        PioSetDir32(VC_PIO1_MASK, VC_PIO1_MASK);
        MessageSendLater(&button_release_task, 0 /* Custom message */, 0, BUTTON_RELEASE_TIMER_DELAY);
    }
    else if (button_event == MUTE_ON_OFF)
    {
        PioSet32(VC_PIO0_MASK, VC_PIO0_MASK);
        PioSet32(VC_PIO1_MASK, VC_PIO1_MASK);
        PioSetDir32(VC_PIO0_MASK, VC_PIO0_MASK);
        PioSetDir32(VC_PIO1_MASK, VC_PIO1_MASK);
        MessageSendLater(&button_release_task, 0 /* Custom message */, 0, BUTTON_RELEASE_TIMER_DELAY);
    }
    else
    {
        PioSet32(VC_PIO0_MASK, 0);
        PioSet32(VC_PIO1_MASK, 0);
        PioSetDir32(VC_PIO0_MASK, VC_PIO0_MASK);
        PioSetDir32(VC_PIO1_MASK, VC_PIO1_MASK);
    }
}

/* Button release handler */
static void
button_release_handler(Task task, MessageId id, Message message)
{
    LOG_DEBUG(("%s\r\n", __FUNCTION__));

    PioSet32(VC_PIO0_MASK, 0);  /* Set PIO0 LOW */
    PioSet32(VC_PIO1_MASK, 0);  /* Set PIO1 LOW */
    
    PioSetDir32(VC_PIO0_MASK, VC_PIO0_MASK);
    PioSetDir32(VC_PIO1_MASK, VC_PIO1_MASK);
    
    UNUSED(task);
    UNUSED(id);
    UNUSED(message);
}

#ifndef PIO_TESTING
/**
 * TRUE if the stored speaker volumes are inside their PIO ranges, i.e. the
 * clamping in PioControlHandler() has nothing to do.
 */
static bool
pio_volumes_in_range(void)
{
    return cfg->app.audio.pio_volume >= cfg->app.audio.pio_volume_min &&
           cfg->app.audio.pio_volume <= cfg->app.audio.pio_volume_max &&
           cfg->app.audio.pio_volumeb >= cfg->app.audio.pio_volume_min2 &&
           cfg->app.audio.pio_volumeb <= cfg->app.audio.pio_volume_max2;
}
#endif


/* Handles PIO interface to control volumes */
static void PioControlHandler( Task t, MessageId id, Message payload )
{
#ifdef PIO_TESTING
    /* States to toggle */
    static bool bluetooth_on = TRUE;
    static bluetooth_mode_t bluetooth_mode = BLUETOOTH_MODE_SOURCE;
    static bool bluetooth_sink_on = FALSE;
    static bool bluetooth_source_on = FALSE;
    static uint32_t old_pio_bits = 0;

    /* Take a current reading of the pio flag and find rising edges */
    uint32_t pio_bits = PioGet32();
    uint32_t pio_delta = pio_bits ^ old_pio_bits;
    uint32_t pio_rising = pio_delta & pio_bits;
    
    UNUSED(blueack_cancel);

    if ((pio_rising & BUTTON_TOGGLE_BLUETOOTH_MASK)
        == BUTTON_TOGGLE_BLUETOOTH_MASK)
    {
        bluetooth_on = !bluetooth_on;
        bluetooth_manager_on(bluetooth_on);
    }
    else if ((pio_rising & BUTTON_TOGGLE_MODE_MASK)
             == BUTTON_TOGGLE_MODE_MASK)
    {
        if (bluetooth_mode == BLUETOOTH_MODE_SOURCE)
            bluetooth_mode = BLUETOOTH_MODE_SINK;
        else
            bluetooth_mode = BLUETOOTH_MODE_SOURCE;

        bluetooth_manager_set_mode(bluetooth_mode);
    }
    else if ((pio_rising & BUTTON_TOGGLE_SINK_MASK)
             == BUTTON_TOGGLE_SINK_MASK)
    {
        bluetooth_sink_on = !bluetooth_sink_on;
        bluetooth_manager_sink_mode_sync(bluetooth_sink_on);
    }
    else if ((pio_rising & BUTTON_TOGGLE_SOURCE_MASK)
             == BUTTON_TOGGLE_SOURCE_MASK)
    {
        bluetooth_source_on = !bluetooth_source_on;
        bluetooth_manager_source_mode_sync(bluetooth_source_on);
    }

    old_pio_bits = pio_bits;
#else
    uint32 pio_bits;

    /* When we detect a falling edge on PIO6 or PIO7 we use the address
     * from the previous iteration of this function. This gives us a
     * setup time of VC_DELAY and a hold time of 0. If we sample the
     * address PIOs when we detect the falling edge then we have a setup
     * time of 0 and hold time of VC_DELAY. */
    static uint32 prev_pio_bits = 0;
    /*    uint32 pio_cnttest;*/
    static uint8 bit1 = 0;
    uint8 bit1_last;
    static uint8 bit3 = 0;
    uint8 bit3_last;
    bool  bit1_falling_edge = 0;
    bool  bit3_falling_edge = 0;

    bool kalimba_update_required = FALSE;

    bool config_update = FALSE;

    /* Failed config copies (out of memory), for the log */
    static uint16 no_memory_count = 0;

    /* Heartbeat (ALIVE line) counters */
    static uint16 alive_ticks = 0;
    static uint16 alive_seconds = 0;

    app_config_t *app_config;

    bit1_last = bit1;  /* save the last bit 1 value for edge detection*/
    bit3_last = bit3;  /* save the last bit 3 value for edge detection*/

    pio_bits = PioGet32();  /* Read new pio bits*/


    bit1 = ((pio_bits & VC_BIT1) == 0) ? 0 : 1 ;    /* get bit 1 value (0 or 1)*/
    bit3 = ((pio_bits & VC_BIT3) == 0) ? 0 : 1 ;    /* get bit 3 value (0 or 1)*/

    bit1_falling_edge = ((bit1_last == 1) && (bit1 == 0));  /* falling edge;*/
    bit3_falling_edge = ((bit3_last == 1) && (bit3 == 0));  /* falling edge;*/

    /* Always printed every ~10 s: time since boot and whether a block the
     * size of the config copy (the malloc that once failed at boot) can
     * still be allocated. The last ALIVE line before a chip reset shows
     * when it happened and whether memory was running out. */
    if (++alive_ticks >= ALIVE_LOG_TICKS)
    {
        app_config_t *probe = config_manager_get_app_config_copy();

        alive_ticks = 0;
        alive_seconds += (ALIVE_LOG_TICKS * VC_DELAY) / 1000;
        log_print("ALIVE ~%us, memory %s\n", alive_seconds,
                  (probe != NULL) ? "ok" : "LOW (malloc failed)");
        if (probe != NULL)
            free(probe);
    }

    /* A radio sync made while the headset was already connected (VC_bls
     * below) is answered once SYNC_ACK_DELAY_TICKS have passed */
    if (sync_ack_ticks > 0)
    {
        sync_ack_ticks--;
        if (sync_ack_ticks == 0)
            waiting_for_blueack_response = TRUE;
    }

    /* The radio is waiting for "paired OK" and a headset is connected: send
     * it the PIO0 pulse now. This used to happen only when a new pairing
     * completed, so after the headset reconnected by itself the radio never
     * learned it was there and didn't send it its tone. */
    if (waiting_for_blueack_response &&
        bluetooth_data.state == BLUETOOTH_STATE_CONNECTED)
    {
        log_print("RADIO: headset connected - sending paired OK (PIO0)\n");
        main_bluetooth_callback(MAIN_BT_CB_PAIRED);
    }

    /* No PIO command this tick and nothing to clamp: the configuration
     * can't change, so don't copy it. Previously the config was malloc'd
     * and freed every VC_DELAY (50 times a second) and PanicNull() reset
     * the chip the first time memory was short - e.g. while Bluetooth and
     * the HFP/AGHFP libraries were starting up or connecting. */
    if (!bit1_falling_edge && !bit3_falling_edge && pio_volumes_in_range())
    {
        /* set volume (as at the end of this function) */
        audio_manager_apply_wired_codec_gains();

        prev_pio_bits = pio_bits;
        MessageSendLater(t, 0, 0, VC_DELAY);  /* Reschedule this function*/
        return;
    }

    app_config = config_manager_get_app_config_copy();
    if (app_config == NULL)
    {
        /* Out of memory for the moment. Was PanicNull(), which reset the
         * chip. Undo the edge detection so this PIO command is handled on
         * the next tick instead of being lost. */
        no_memory_count++;
        if (no_memory_count == 1 || (no_memory_count % 50) == 0)
            log_print("PIO: no memory for config copy (%u) - retrying\n",
                      no_memory_count);
        bit1 = bit1_last;
        bit3 = bit3_last;
        MessageSendLater(t, 0, 0, VC_DELAY);  /* Reschedule this function*/
        return;
    }

    /* Always printed: each command from the radio (address on the VC_MASKB
     * pins, clocked by PIO6 or PIO7) */
    if (bit1_falling_edge)
        log_print("RADIO: command 0x%04x on PIO6\n",
                  (uint16)(prev_pio_bits & VC_MASKB));
    if (bit3_falling_edge)
        log_print("RADIO: command 0x%04x on PIO7\n",
                  (uint16)(prev_pio_bits & VC_MASKB));

 
    /*if bitside is high then doing side tone stuff else doing regular volume*/
    if ((prev_pio_bits & VC_MASKB) == VC_side)
    {
        /* Increase volume based on pin 1 falling edge*/
        if(bit1_falling_edge)  /* falling edge*/
        {
            app_config->audio.pio_side = app_config->audio.pio_side + PIO_side_INC;  /* increment pio volume*/
            app_config->audio.pio_side = (app_config->audio.pio_side < PIO_side_MAX) ? app_config->audio.pio_side : PIO_side_MAX ;
            kalimba_update_required = TRUE;
            config_update = TRUE;
            LOG_DEBUG(("Sidetone+ (%d)\n", app_config->audio.pio_side));
        }

        /* Decrease volume based on pin 3 falling edge*/
        if(bit3_falling_edge)  /* falling edge*/
        {
            app_config->audio.pio_side = (app_config->audio.pio_side >= PIO_side_INC) ? app_config->audio.pio_side - PIO_side_INC : 0 ;  /* decrement pio volume*/
            app_config->audio.pio_side = (app_config->audio.pio_side > PIO_side_MIN) ? app_config->audio.pio_side : PIO_side_MIN ;
            kalimba_update_required = TRUE;
            config_update = TRUE;
            LOG_DEBUG(("Sidetone- (%d)\n", app_config->audio.pio_side));
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_vol)
    {

        /* Increase volume based on pin 1 falling edge*/
        if(bit1_falling_edge)  /* falling edge*/
        {
            app_config->audio.pio_volume = app_config->audio.pio_volume + PIO_VOLUME_INC;  /* increment pio volume*/
            app_config->audio.pio_volume = (app_config->audio.pio_volume < app_config->audio.pio_volume_max) ? app_config->audio.pio_volume : app_config->audio.pio_volume_max ;
            config_update = TRUE;
            kalimba_update_required = TRUE;
            LOG_DEBUG(("Spk1 volume+ (%d)\n", app_config->audio.pio_volume));
        }

        /* Decrease volume based on pin 3 falling edge*/
        if(bit3_falling_edge)  /* falling edge*/
        {
            app_config->audio.pio_volume = (app_config->audio.pio_volume >= PIO_VOLUME_INC) ? app_config->audio.pio_volume - PIO_VOLUME_INC : 0 ;  /* decrement pio volume*/
            app_config->audio.pio_volume = (app_config->audio.pio_volume > app_config->audio.pio_volume_min) ? app_config->audio.pio_volume : app_config->audio.pio_volume_min ;
            config_update = TRUE;
            kalimba_update_required = TRUE;
            LOG_DEBUG(("Spk1 volume- (%d)\n", app_config->audio.pio_volume));
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_vol2)       /* VC_vol2   0x0400  if vc_maskb = C5 when 00400 then use VC_xxx*/
    {
        /* Increase volume based on pin 1 falling edge*/
        if(bit1_falling_edge)  /* falling edge*/
        {
            app_config->audio.pio_volumeb = app_config->audio.pio_volumeb + PIO_VOLUME_INC2;  /* increment pio volume*/
            app_config->audio.pio_volumeb = (app_config->audio.pio_volumeb < app_config->audio.pio_volume_max2) ? app_config->audio.pio_volumeb : app_config->audio.pio_volume_max2 ;
            config_update = TRUE;
            LOG_DEBUG(("Spk2 volume+ (%d)\n", app_config->audio.pio_volumeb));
        }

        /* Decrease volume based on pin 3 falling edge*/
        if(bit3_falling_edge)  /* falling edge*/
        {
            app_config->audio.pio_volumeb = (app_config->audio.pio_volumeb >= PIO_VOLUME_INC2) ? app_config->audio.pio_volumeb - PIO_VOLUME_INC2 : 0 ;  /* decrement pio volume*/
            app_config->audio.pio_volumeb = (app_config->audio.pio_volumeb > app_config->audio.pio_volume_min2) ? app_config->audio.pio_volumeb : app_config->audio.pio_volume_min2 ;
            config_update = TRUE;
            LOG_DEBUG(("Spk2 volume- (%d)\n", app_config->audio.pio_volumeb));
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_bi2c)
    {
        /* If pio6 pin is toggled, then the Gateway mode Bluetooth goes to
         * I2S slot1 */
        if (bit1_falling_edge)  /* falling edge*/
        {
            audio_manager_set_bt_ag_audio_route(BT_AG_AUDIO_OVERRIDES_HS1);
        }

        /* If pio7 pin is toggled, then the Gateway mode Bluetooth is routed
         * to I2S slot2 */
        if (bit3_falling_edge)  /* falling edge*/
        {
            audio_manager_set_bt_ag_audio_route(BT_AG_AUDIO_OVERRIDES_HS2);
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_bls)     /*VC_bls   0x4000   if vc_maskb = C5 when 04000 then use VC_xxx Bluetooth for source mode sync*/
    {
        /* Set up Bluetooth for source mode sync */
        if (bit1_falling_edge)  /* falling edge*/
        {
            if (bluetooth_data.mode == BLUETOOTH_MODE_SOURCE &&
                bluetooth_data.state == BLUETOOTH_STATE_CONNECTED)
            {
                /* The saved headset is already connected (e.g. it
                 * reconnected by itself): keep it and report paired OK
                 * instead of dropping it for a new pairing. To pair another
                 * headset, switch this one off first. */
                log_print("RADIO: sync - headset already connected, "
                          "keeping it\n");
                sync_ack_ticks = SYNC_ACK_DELAY_TICKS;
            }
            else
            {
                bluetooth_manager_source_mode_sync(TRUE);
            }
        }

        /* Cancel sync */
        if (bit3_falling_edge)  /* falling edge*/
        {
            bluetooth_manager_source_mode_sync(FALSE);
            blueack_cancel();
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_bypk)     /*VC_bypk   0x4100   if vc_maskb = C5 when 04100 then use VC_xxx bypass kalimba main channel
            this simply means to turn off noise cancelling*/
    {
        /* Bypass noise cancelling main channel*/
        if (bit1_falling_edge)  /* falling edge*/
        {
            PanicFalse(KalimbaSendMessage(CVC_SETMODE_MSG, SYSMODE_PSTHRGH, 0,
                                          CALLST_CONNECTED, 0));
        }

        /* Do not bypass noise cancelling main channel*/
        if (bit3_falling_edge)  /* falling edge*/
        {
            PanicFalse(KalimbaSendMessage(CVC_SETMODE_MSG, SYSMODE_HFK, 0,
                                          CALLST_CONNECTED, 0));
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_rst) /* VC_st 0x4400 factory reset*/
    {
        /* Factory reset */
        if (bit1_falling_edge)  /* falling edge*/
        {
            config_manager_factory_reset();
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_wir_blu)    /*VC_wir_blu   0x4500      if vc_maskb = C5 when 04500 then use VC_xxx use wired/bluetooth on main*/
    {
        /* Use wired/bluetooth on main*/
        if (bit1_falling_edge)  /* falling edge*/
        {
            bluetooth_manager_on(TRUE);
        }

        /* Use wired only on main */
        if (bit3_falling_edge)  /* falling edge*/
        {
            bluetooth_manager_on(FALSE);
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_sp1r)    /*VC_sp1r   0x08000      if vc_maskb = C5 when 08000 then use VC_xxx speaker 1 volume range control*/
    {
        /* Increase speaker 1 volume range*/
        if(bit1_falling_edge)  /* falling edge*/
        {
            if (app_config->audio.pio_volume_max < PIO_VOLUME_MAXMAX)
            {
                app_config->audio.pio_volume_max++;
                app_config->audio.pio_volume_min++;
                config_update = TRUE;
                LOG_DEBUG(("Spk1 range+ (%d-%d)\n",
                           app_config->audio.pio_volume_min,
                           app_config->audio.pio_volume_max));
            }
        }

        /* Decrease speaker 1 volume range*/
        if(bit3_falling_edge)  /* falling edge*/
        {
            if (app_config->audio.pio_volume_min > PIO_VOLUME_MINMIN)
            {
                app_config->audio.pio_volume_max--;
                app_config->audio.pio_volume_min--;
                config_update = TRUE;
                LOG_DEBUG(("Spk1 range- (%d-%d)\n",
                           app_config->audio.pio_volume_min,
                           app_config->audio.pio_volume_max));
            }
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_mic1)   /* VC_mic1   0x08100      if vc_maskb = C5 when 08100 then use VC_xxx microphone 1 input gain control*/
    {
        /* Increase microphone 1 input gain*/
        if(bit1_falling_edge)  /* falling edge*/
        {
            app_config->audio.pio_involume = app_config->audio.pio_involume + PIO_inVOLUME_INC;  /* increment pio volume*/
            app_config->audio.pio_involume = (app_config->audio.pio_involume < PIO_inVOLUME_MAX) ? app_config->audio.pio_involume : PIO_inVOLUME_MAX ;
            config_update = TRUE;
            kalimba_update_required = TRUE;
            LOG_DEBUG(("Mic1 volume+ (%d)\n", app_config->audio.pio_involume));
        }

        /* Decrease microphone 1 input gain*/
        if(bit3_falling_edge)  /* falling edge*/
        {
            app_config->audio.pio_involume = (app_config->audio.pio_involume >= PIO_inVOLUME_INC) ? app_config->audio.pio_involume - PIO_inVOLUME_INC : 0 ;  /* decrement pio volume*/
            app_config->audio.pio_involume = (app_config->audio.pio_involume > PIO_inVOLUME_MIN) ? app_config->audio.pio_involume : PIO_inVOLUME_MIN ;
            config_update = TRUE;
            kalimba_update_required = TRUE;
            LOG_DEBUG(("Mic1 volume- (%d)\n", app_config->audio.pio_involume));
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_sp2r)   /*VC_sp2r   0x08400      if vc_maskb = C5 when 08400 then use VC_xxx speaker 2 volume range control*/
    {
        /* Increase speaker 2 volume range*/
        if(bit1_falling_edge)  /* falling edge*/
        {
            if (app_config->audio.pio_volume_max2 < PIO_VOLUME_MAXMAX2)
            {
                app_config->audio.pio_volume_max2++;
                app_config->audio.pio_volume_min2++;
                config_update = TRUE;
                LOG_DEBUG(("Spk2 range+ (%d-%d)\n",
                           app_config->audio.pio_volume_min2,
                           app_config->audio.pio_volume_max2));
            }
        }

        /* Decrease speaker 2 volume range*/
        if(bit3_falling_edge)  /* falling edge*/
        {
            if (app_config->audio.pio_volume_min2 > PIO_VOLUME_MINMIN2)
            {
                app_config->audio.pio_volume_max2--;
                app_config->audio.pio_volume_min2--;
                config_update = TRUE;
                LOG_DEBUG(("Spk2 range- (%d-%d)\n",
                           app_config->audio.pio_volume_min2,
                           app_config->audio.pio_volume_max2));
            }
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_mic2)   /* VC_mic2   0x08500      if vc_maskb = C5 when 08500 then use VC_xxx microphone 2 input gain control*/
    {
        /* Increase microphone 2 input gain*/
        if(bit1_falling_edge)  /* falling edge*/
        {
            app_config->audio.pio_involumeb = app_config->audio.pio_involumeb + PIO_inVOLUME_INC2;  /* increment pio volume*/
            app_config->audio.pio_involumeb = (app_config->audio.pio_involumeb < PIO_inVOLUME_MAX2) ? app_config->audio.pio_involumeb : PIO_inVOLUME_MAX2 ;
            config_update = TRUE;
            LOG_DEBUG(("Mic2 volume+ (%d)\n", app_config->audio.pio_involumeb));
        }

        /* Decrease microphone 2 input gain*/
        if(bit3_falling_edge)  /* falling edge*/
        {
            app_config->audio.pio_involumeb = (app_config->audio.pio_involumeb >= PIO_inVOLUME_INC2) ? app_config->audio.pio_involumeb - PIO_inVOLUME_INC2 : 0 ;  /* decrement pio volume*/
            app_config->audio.pio_involumeb = (app_config->audio.pio_involumeb > PIO_inVOLUME_MIN2) ? app_config->audio.pio_involumeb : PIO_inVOLUME_MIN2 ;
            config_update = TRUE;
            LOG_DEBUG(("Mic2 volume- (%d)\n", app_config->audio.pio_involumeb));
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_bsms)   /*VC_bsms   0x0C000      if vc_maskb = C5 when 0C000 then use VC_xxx Sync for Bluetooth for sink mode sync*/
    {
        /* Sync for Bluetooth for sink mode */
        if (bit1_falling_edge)  /* falling edge*/
        {
            bluetooth_manager_sink_mode_sync(TRUE);
        }

        /* Cancel sync */
        if (bit3_falling_edge)  /* falling edge*/
        {
            bluetooth_manager_sink_mode_sync(FALSE);
            blueack_cancel();
        }
    }

    else if ((prev_pio_bits & VC_MASKB) == VC_bbnc)
    {
        /* BT headset speaker boost: 0/6/12/18/26 dB (SET_LEVELS, not wired PIO). */
        if (bit1_falling_edge)
        {
            app_config->audio.bt_spk_boost_db =
                audio_manager_step_bt_spk_boost_db(
                        app_config->audio.bt_spk_boost_db, TRUE);
            config_update = TRUE;
            kalimba_update_required = TRUE;
            LOG_DEBUG(("BT spk boost+ (%u dB)\n",
                       app_config->audio.bt_spk_boost_db));
        }

        if (bit3_falling_edge)
        {
            app_config->audio.bt_spk_boost_db =
                audio_manager_step_bt_spk_boost_db(
                        app_config->audio.bt_spk_boost_db, FALSE);
            config_update = TRUE;
            kalimba_update_required = TRUE;
            LOG_DEBUG(("BT spk boost- (%u dB)\n",
                       app_config->audio.bt_spk_boost_db));
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_bmode)
    {
        /* Set to source (Audio Gateway) mode based on pin 1 falling edge */
        if (bit1_falling_edge)  /* falling edge */
        {
            bluetooth_manager_set_mode(BLUETOOTH_MODE_SOURCE);
            kalimba_update_required = TRUE;
        }

        /* Set to sink (Headset) mode based on pin 3 falling edge */
        if (bit3_falling_edge)  /* falling edge */
        {
            bluetooth_manager_set_mode(BLUETOOTH_MODE_SINK);
            kalimba_update_required = TRUE;
        }
    }
    else if ((prev_pio_bits & VC_MASKB) == VC_blueack)
    {
        /* Update blueack waiting state when PIO6 is toggled */
        if  (bit1_falling_edge)
        {
            waiting_for_blueack_response = TRUE;
        }
    }

    /* Ensure volumes are in min/max ranges */
    if (app_config->audio.pio_volume < app_config->audio.pio_volume_min)
    {
        app_config->audio.pio_volume = app_config->audio.pio_volume_min;
        config_update = TRUE;
        kalimba_update_required = TRUE;
        LOG_DEBUG(("Spk1 volume clamped (%d)\n",
                   app_config->audio.pio_volume));
    }
    else if (app_config->audio.pio_volume > app_config->audio.pio_volume_max)
    {
        app_config->audio.pio_volume = app_config->audio.pio_volume_max;
        config_update = TRUE;
        kalimba_update_required = TRUE;
        LOG_DEBUG(("Spk1 volume clamped (%d)\n",
                   app_config->audio.pio_volume));
    }

    if (app_config->audio.pio_volumeb < app_config->audio.pio_volume_min2)
    {
        app_config->audio.pio_volumeb = app_config->audio.pio_volume_min2;
        config_update = TRUE;
        LOG_DEBUG(("Spk2 volume clamped (%d)\n",
                   app_config->audio.pio_volumeb));
    }
    else if (app_config->audio.pio_volumeb > app_config->audio.pio_volume_max2)
    {
        app_config->audio.pio_volumeb = app_config->audio.pio_volume_max2;
        config_update = TRUE;
        LOG_DEBUG(("Spk2 volume clamped (%d)\n",
                   app_config->audio.pio_volumeb));
    }

    /* set volume */
    audio_manager_apply_wired_codec_gains();

    if (config_update)
    {
        status_t status = config_manager_store_app_config(app_config);
        if (status != STATUS_OK)
            LOG_DEBUG(("Failed to write config\n"));
        app_config = NULL;
    }
    else
    {
        free(app_config);
        app_config = NULL;
    }

    if (kalimba_update_required)
        audio_manager_update_kalimba_levels();

    prev_pio_bits = pio_bits;
#endif
    MessageSendLater(t, 0, 0, VC_DELAY);  /* Reschedule this function*/
}


/* Handles Kalimba messages from VCP */
static void KalVCPHandler( Task task, uint16 id, Message message )
{
    switch(id)
    {
    case MESSAGE_FROM_KALIMBA:
    {
        const KAL_MESSAGE *m = (const KAL_MESSAGE *) message;
        switch ( m->id )
        {
        case VCP_READY_MSG:
        {
            LOG_DEBUG(("Kalimba ready, connecting audio...\n"));
            audio_manager_update_kalimba_levels();
            audio_manager_connect_kalimba();
        }
        break;

        case VCP_CODEC_MSG:
            LOG_DEBUG(("Received VCP_CODEC message\n"));
        break;

        case CVC_SECPASSED_MSG:
            LOG_DEBUG(("Security passed\n"));
        break;

        case CVC_SECFAILED_MSG:
            LOG_DEBUG(("Security failed\n"));
            Panic();
        break;

        case CVC_LOADPERSIST_MSG:
        {
            /* a=sysid, b=len */
            const pblock_entry* entry = PblockGet(m->a);
            LOG_DEBUG(("Loading Persistent Store block (req id=0x%04x, req len=%d, rsp len=%d)\n",
                       m->a, m->b, entry->size));
            PanicFalse(KalimbaSendLongMessage(CVC_LOADPERSIST_RESP,
                                              entry->size, entry->data));
        }
        break;

        case MSG_WARP_VALUES:
        {
            LOG_DEBUG(("WARP values: 0x%04x 0x%04x 0x%04x 0x%04x\n",
                       m->a, m->b, m->c, m->d));
        }
        break;

        default:
            LOG_DEBUG(("Unrecognised message from Kalimba: id=0x%04x\n",
                    m->id));
            break;
        }
    }
    break;

    default:
        break ;
    }
}


/* Load and run Kalimba, setup message handler */
static void StartKalimba(void)
{
    /* Location of DSP kap file in the file system */
#ifndef KALIMBA_CVC
    static const char kal[] = "vcp_8k_wired_to_24k_wired/vcp_8k_wired_to_24k_wired.kap";
#else
    static const char kal[] = "cvc_headset/cvc_headset.kap";
#endif

    FILE_INDEX index;

    /* Load Kalimba etc */
    KalVCPTask.handler = KalVCPHandler;
    MessageKalimbaTask(&KalVCPTask);
    index = FileFind( FILE_ROOT, (const char *)kal, strlen(kal) );
    PanicFalse( index != FILE_NONE );
    PanicFalse( KalimbaLoad( index ) );
}


/**
 * This function checks whether there is a valid audio configuration
 * currently held by the config subsystem. If not, then it initialises
 * it to the defaults.
 */
static void
load_audio_config(void)
{
    status_t status;
    app_config_t *app_config = config_manager_get_app_config_copy();
    PanicNull(app_config);
    
        app_config->audio.pio_volume = PIO_VOLUME_DEFAULT;
        app_config->audio.pio_volume_max = PIO_VOLUME_MAXDEFAULT;
        app_config->audio.pio_volume_min = PIO_VOLUME_MINDEFAULT;
        app_config->audio.pio_volumeb = PIO_VOLUME_DEFAULT2;
        app_config->audio.pio_volume_max2 = PIO_VOLUME_MAXDEFAULT2;
        app_config->audio.pio_volume_min2 = PIO_VOLUME_MINDEFAULT2;
        app_config->audio.pio_involume = PIO_inVOLUME_DEFAULT;
        app_config->audio.pio_involumeb = PIO_inVOLUME_DEFAULT2;
    
    
    

    if (!app_config->audio.valid)
    {
        LOG_DEBUG(("No audio configuration stored; using defaults.\n"));

        app_config->audio.pio_side = PIO_side_DEFAULT;
        app_config->audio.bt_spk_boost_db = BT_SPK_BOOST_DB_DEFAULT;

        app_config->audio.valid = TRUE;

        /* Note: config_manager_store_temporary_app_config() does not take
         *       ownership of app_config, whereas
         *       config_manager_store_app_config() does. */
        status = config_manager_store_temporary_app_config(app_config);
        PanicFalse(status == STATUS_OK);
    }
    else
    {
        LOG_DEBUG(("Retrieved stored audio configuration.\n"));

        app_config->audio.bt_spk_boost_db =
            audio_manager_normalize_bt_spk_boost_db(
                    app_config->audio.bt_spk_boost_db);

        /* Uncomment any of the following lines to use the default rather
         * than stored value at startup. */
 
        /* app_config->audio.pio_side = PIO_side_DEFAULT; */

        /* Note: config_manager_store_temporary_app_config() does not take
        * ownership of app_config, whereas
        * config_manager_store_app_config() does. */
        status = config_manager_store_temporary_app_config(app_config);
        PanicFalse(status == STATUS_OK);
    }

    LOG_DEBUG(("    spk volume1 %2d (max %2d, min %2d)\n",
               app_config->audio.pio_volume,
               app_config->audio.pio_volume_max,
               app_config->audio.pio_volume_min));
    LOG_DEBUG(("    spk volume2 %2d (max %2d, min %2d)\n",
               app_config->audio.pio_volumeb,
               app_config->audio.pio_volume_max2,
               app_config->audio.pio_volume_min2));
    LOG_DEBUG(("    mic volume1 %2d\n", app_config->audio.pio_involume));
    LOG_DEBUG(("    mic volume2 %2d\n", app_config->audio.pio_involumeb));
    LOG_DEBUG(("    sidetone volume %2d\n", app_config->audio.pio_side));
    LOG_DEBUG(("    BT spk boost %u dB\n", app_config->audio.bt_spk_boost_db));

    free(app_config);
    app_config = NULL;
}


void
main_bluetooth_callback(main_bt_cb_events_t event)
{
    LOG_DEBUG(("Bluetooth event %u\n", event));
   
    if (event == MAIN_BT_CB_PAIRED)
    {
        set_blueack_pio(TRUE);

        MessageSendLater(&BlueackTask, 0, 0, VC_BLUEACK_DELAY);
        waiting_for_blueack_response = FALSE;
    }
}


#define CONFIG_DSP_BASE (50)
#define CONFIG_DSP(x) (CONFIG_DSP_BASE + (x))
#define CONFIG_DSP_SESSION_KEY (CONFIG_DSP(49))

/* Main VM routine */
int main(void)
{
    LOG_DEBUG(("Starting..."));

    /* Always printed: shows which build of main.c is running. If this time
     * doesn't change after a rebuild, the new code was not compiled or
     * flashed (e.g. make kept an old main.o, "Download Unnecessary"). */
    log_print("\nBUILD: main.c compiled " __DATE__ " " __TIME__ "\n");

    /* Initialise the Configuration Manager */
    config_manager_init();

    load_audio_config();

    /* Latch on the power and charge */
   /* PsuConfigure(PSU_ANA_REG_USER, PSU_ANA_REG_OWNER_VM, 1);    */
    PsuConfigure(PSU_SMPS0, PSU_ENABLE, 0);
    PsuConfigure(PSU_SMPS1, PSU_ENABLE, 0);
 /*   PsuConfigure(PSU_LINEAR0, PSU_ENABLE, 1);digital LDO always on */
 /*   PsuConfigure(PSU_LINEAR1, PSU_ENABLE, 1);aux LDO always on */
 /*   PsuConfigure(PSU_LINEAR2, PSU_ENABLE, 1); analog LDO 1.35 v always OFF? */ 
    PsuConfigure(PSU_SMPS_INPUT_SEL_VBAT, PSU_ENABLE, 1); /* keep on to enable mic bias */
 /*   PsuConfigure(PSU_VOLTAGE_SENSE_REMOTE, PSU_ENABLE, 0); */
    ChargerConfigure(CHARGER_SUPPRESS_LED0, 1);
    PanicFalse( ChargerConfigure (CHARGER_ENABLE,0) );               /* disable the charger */
    PanicFalse( ChargerConfigure (CHARGER_USE_EXT_TRAN,0) );         /* Use external trnasister */
    PanicFalse( ChargerConfigure (CHARGER_ATTACH_RESET_ENABLE,0) );  /* Disaable reset on charger being attached */

    /* Initialise the Bluetooth Manager */
    bluetooth_manager_init();

    audio_manager_init();

    PblockInit(CONFIG_DSP_SESSION_KEY, 0);

    /* Load the Kalimba */
    StartKalimba();

    /* Set PIO pins 1,3 and side to be inputs */
    PioSetDir32(VC_MASK, 0x0000);

    /* Schedule first message/task for volume control */
    PioTask.handler = PioControlHandler;
    MessageSend( &PioTask, 0 , 0 );
    
    /* Setup Blueack task hander and init state */
    BlueackTask.handler = blueack_handler;
    set_blueack_pio(FALSE);
    waiting_for_blueack_response = FALSE;
    
    button_release_task.handler = button_release_handler;
    handle_button_event(NO_BUTTON);

    /* Remain in MessageLoop (handles messages) */
    MessageLoop();
    return 0;
}
