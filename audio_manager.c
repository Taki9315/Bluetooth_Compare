#include "audio_manager.h"

#include <hfp.h>
#include <panic.h>
#include <micbias.h>
#include <pio.h>
#include <sink.h>
#include <source.h>
#include <kalimba.h>
#include <stdlib.h>
#include <psu.h>

#include <stream.h>

#include "kalimba_constants.h"
#include "bluetooth_manager.h"
#include "config.h"

/* Uncomment this to enable logging in this file */
/* #define DEBUG_ENABLED */
#include "log.h"

/*
 * Used instead of PanicFalse() on the Bluetooth/audio path (codec gains -
 * applied every 20 ms by the PIO loop - Kalimba messages and stream
 * routing on SCO connect/disconnect). With PanicFalse() one failed call,
 * e.g. while the headset disconnects, reset the chip. Now the failure is
 * printed (always, also without DEBUG_ENABLED) with its line number and
 * the device keeps running. The boot-time hardware set-up in
 * audio_manager_init() still uses PanicFalse().
 */
#define AUDIO_CHECK(ok) \
    ((ok) ? (void)0 : log_print("AUDIO FAIL: audio_manager.c line %d\n", \
                                __LINE__))

/* Alango Logger PIO */
#define LOG_PIO		2
#if (LOG_PIO < 16)
#define LOG_MASK	((1 << LOG_PIO) & 0xFFFF)
#else
#define LOG_MASK (0x00010000 << (LOG_PIO - 16)) & 0xFFFFFFFF
#endif



/* I2S Master / Slave switch. 1 = Master, 0 = Slave */
#define I2S_MASTER                0
#define I2S_SAMPLE_RATE           24150
#define ADC_DAC_A_SAMPLE_RATE     8050
#define ADC_DAC_B_SAMPLE_RATE     8050

#define POWER_AMP_PIO 4


/** Audio manager global data structure. */
typedef struct audio_data_t
{
    /** Current AG route setting. */
    audio_manager_bt_ag_audio_route_t ag_route;
} audio_data_t;


/** Audio manager global data. */
static audio_data_t audio_data;


/* Wired HS1 mic gain while SCO is up. */
#define WIRED_MIC_GAIN_MUTED  0


/* mic1/spk1 saved on SCO connect and restored on drop (codec B / I2S0). */
static struct
{
    uint16 mic1;
    uint16 spk1;
} saved_wired_gains;


static bool wired_gains_muted = FALSE;
static bool wired_gains_restore_pending = FALSE;


/* codec B, headset 1 */
static void
apply_headset1_codec_gains(uint16 mic1, uint16 spk1)
{
    Source adc_b = StreamAudioSource(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0,
                                     AUDIO_CHANNEL_B);
    Sink dac_b = StreamAudioSink(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0,
                                 AUDIO_CHANNEL_B);

    AUDIO_CHECK(SourceConfigure(adc_b, STREAM_CODEC_INPUT_GAIN, mic1));
    AUDIO_CHECK(SinkConfigure(dac_b, STREAM_CODEC_OUTPUT_GAIN, spk1));
}


/* codec A, headset 2 (I2S1 -- not switched to BT in HS1 mode) */
static void
apply_headset2_codec_gains_from_cfg(void)
{
    Source adc_a = StreamAudioSource(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0,
                                     AUDIO_CHANNEL_A);
    Sink dac_a = StreamAudioSink(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0,
                                 AUDIO_CHANNEL_A);

    AUDIO_CHECK(SourceConfigure(adc_a, STREAM_CODEC_INPUT_GAIN,
                               cfg->app.audio.pio_involumeb));
    AUDIO_CHECK(SinkConfigure(dac_a, STREAM_CODEC_OUTPUT_GAIN,
                             cfg->app.audio.pio_volumeb));
}


static void
update_wired_gains_for_bt(Sink hfp_sink)
{
    if (hfp_sink != NULL && cfg != NULL)
    {
        saved_wired_gains.mic1 = cfg->app.audio.pio_involume;
        saved_wired_gains.spk1 = cfg->app.audio.pio_volume;

        if (!wired_gains_muted)
        {
            LOG_DEBUG(("Muting HS1 wired gains (mic1=%u spk1=%u)\n",
                       saved_wired_gains.mic1, saved_wired_gains.spk1));
        }
        else
        {
            LOG_DEBUG(("SCO back: saved HS1 gains (mic1=%u spk1=%u)\n",
                       saved_wired_gains.mic1, saved_wired_gains.spk1));
        }
        wired_gains_restore_pending = FALSE;
        wired_gains_muted = TRUE;
    }
    else if (wired_gains_muted)
    {
        LOG_DEBUG(("SCO down: restore HS1 wired gains (mic1=%u spk1=%u)\n",
                   saved_wired_gains.mic1, saved_wired_gains.spk1));
        wired_gains_muted = FALSE;
        wired_gains_restore_pending = TRUE;
    }
}


void
audio_manager_apply_wired_codec_gains(void)
{
    if (cfg == NULL)
    {
        return;
    }

    apply_headset2_codec_gains_from_cfg();

    if (wired_gains_muted)
    {
        apply_headset1_codec_gains(WIRED_MIC_GAIN_MUTED,
                                   cfg->app.audio.pio_volume_min);
    }
    else if (wired_gains_restore_pending)
    {
        apply_headset1_codec_gains(saved_wired_gains.mic1,
                                   saved_wired_gains.spk1);
        wired_gains_restore_pending = FALSE;
    }
    else
    {
        apply_headset1_codec_gains(cfg->app.audio.pio_involume,
                                   cfg->app.audio.pio_volume);
    }
}


static void
PioSetPio(uint16 pPIO, bool pOnOrOff)
{
    uint16 lPinVals = 0;
    uint16 lWhichPin  = (1<< pPIO);

    if (pOnOrOff)
    {
        lPinVals = lWhichPin;
    }
    else
    {
        lPinVals = 0x0000; /* clear the corresponding bit */
    }

    /* (mask,bits) setting bit to a '1' sets the corresponding port as
     * an output */
    PioSetDir32(lWhichPin, lWhichPin);
    /* Set the value of the pin */
    PioSet32(lWhichPin, lPinVals);
}


/* Configure all audio streams, and connect them */
void
audio_manager_init(void)
{
    /* Audio Interfaces */
    Source I2S_IN_0;
    Source I2S_IN_1;
    Source audio_source_a;
    Source audio_source_b;

    Sink I2S_OUT_0;
    Sink I2S_OUT_1;
    Sink audio_sink_a;
    Sink audio_sink_b;

    audio_data.ag_route = BT_AG_AUDIO_OVERRIDES_HS1;

    /* I2S Setup */

    /* Source StreamAudioSource (audio_hardware hardware, audio_instance
     * instance, audio_channel channel); */
    I2S_IN_0 = StreamAudioSource(AUDIO_HARDWARE_I2S, AUDIO_INSTANCE_0,
                                 AUDIO_CHANNEL_SLOT_0);
    I2S_IN_1 = StreamAudioSource(AUDIO_HARDWARE_I2S, AUDIO_INSTANCE_0,
                                 AUDIO_CHANNEL_SLOT_1);
    audio_source_a = StreamAudioSource(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0,
                                       AUDIO_CHANNEL_A);
    audio_source_b = StreamAudioSource(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0,
                                       AUDIO_CHANNEL_B);

    /* Sink StreamAudioSink (audio_hardware hardware, audio_instance
     * instance, audio_channel channel); */
    I2S_OUT_0 = StreamAudioSink(AUDIO_HARDWARE_I2S, AUDIO_INSTANCE_0,
                                AUDIO_CHANNEL_SLOT_0);
    I2S_OUT_1 = StreamAudioSink(AUDIO_HARDWARE_I2S, AUDIO_INSTANCE_0,
                                AUDIO_CHANNEL_SLOT_1);
    audio_sink_a = StreamAudioSink(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0,
                                    AUDIO_CHANNEL_A);
    audio_sink_b = StreamAudioSink(AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0,
                                   AUDIO_CHANNEL_B);

    /* SinkConfigure (Sink sink, stream_config_key key, uint32 value) */
    PanicFalse(SinkConfigure(I2S_OUT_0, STREAM_I2S_MASTER_MODE, I2S_MASTER));
    PanicFalse(SinkConfigure(I2S_OUT_1, STREAM_I2S_MASTER_MODE, I2S_MASTER));
    PanicFalse(SinkConfigure(I2S_OUT_0, STREAM_I2S_SYNC_RATE, I2S_SAMPLE_RATE));
    PanicFalse(SinkConfigure(I2S_OUT_1, STREAM_I2S_SYNC_RATE, I2S_SAMPLE_RATE));
    PanicFalse(SinkConfigure(I2S_OUT_0, STREAM_I2S_BITS_PER_SAMPLE, 32));
    PanicFalse(SinkConfigure(I2S_OUT_1, STREAM_I2S_BITS_PER_SAMPLE, 32));
    PanicFalse(SinkConfigure(I2S_OUT_0, STREAM_I2S_MASTER_CLOCK_RATE, 0));
    PanicFalse(SinkConfigure(I2S_OUT_1, STREAM_I2S_MASTER_CLOCK_RATE, 0));
    PanicFalse(SinkConfigure(I2S_OUT_0, STREAM_I2S_JSTFY_FORMAT, 0));
    PanicFalse(SinkConfigure(I2S_OUT_1, STREAM_I2S_JSTFY_FORMAT, 0));
    PanicFalse(SinkConfigure(I2S_OUT_0, STREAM_I2S_LFT_JSTFY_DLY, 1));
    PanicFalse(SinkConfigure(I2S_OUT_1, STREAM_I2S_LFT_JSTFY_DLY, 1));
    PanicFalse(SinkConfigure(I2S_OUT_0, STREAM_I2S_CHNL_PLRTY, 0));
    PanicFalse(SinkConfigure(I2S_OUT_1, STREAM_I2S_CHNL_PLRTY, 0));
    PanicFalse(SinkSynchronise(I2S_OUT_0, I2S_OUT_1));


    /* Source Configure (Sink sink, stream_config_key key, uint32 value) */
    PanicFalse(SourceConfigure(I2S_IN_0, STREAM_I2S_MASTER_MODE, I2S_MASTER));
    PanicFalse(SourceConfigure(I2S_IN_1, STREAM_I2S_MASTER_MODE, I2S_MASTER));
    PanicFalse(SourceConfigure(I2S_IN_0, STREAM_I2S_SYNC_RATE,
                               I2S_SAMPLE_RATE));
    PanicFalse(SourceConfigure(I2S_IN_1, STREAM_I2S_SYNC_RATE,
                               I2S_SAMPLE_RATE));
    PanicFalse(SourceConfigure(I2S_IN_0, STREAM_I2S_BITS_PER_SAMPLE, 32));
    PanicFalse(SourceConfigure(I2S_IN_1, STREAM_I2S_BITS_PER_SAMPLE, 32));
    PanicFalse(SourceConfigure(I2S_IN_0, STREAM_I2S_MASTER_CLOCK_RATE, 0));
    PanicFalse(SourceConfigure(I2S_IN_1, STREAM_I2S_MASTER_CLOCK_RATE, 0));
    PanicFalse(SourceConfigure(I2S_IN_0, STREAM_I2S_JSTFY_FORMAT, 0));
    PanicFalse(SourceConfigure(I2S_IN_1, STREAM_I2S_JSTFY_FORMAT, 0));
    PanicFalse(SourceConfigure(I2S_IN_0, STREAM_I2S_LFT_JSTFY_DLY, 1));
    PanicFalse(SourceConfigure(I2S_IN_1, STREAM_I2S_LFT_JSTFY_DLY, 1));
    PanicFalse(SourceConfigure(I2S_IN_0, STREAM_I2S_CHNL_PLRTY, 0));
    PanicFalse(SourceConfigure(I2S_IN_1, STREAM_I2S_CHNL_PLRTY, 0));
    PanicFalse(SourceSynchronise(I2S_IN_0, I2S_IN_1));

    PanicFalse( SourceSynchronise(audio_source_a, audio_source_b) );
    PanicFalse( SinkSynchronise(audio_sink_a, audio_sink_b) );
    PanicFalse( SinkSynchronise(audio_sink_a, I2S_OUT_0) ) ;



    /* Configure sampling rate for both channels and synchronise left
     * and right channels */
    PanicFalse(SourceConfigure(audio_source_a, STREAM_CODEC_INPUT_RATE,
                               ADC_DAC_A_SAMPLE_RATE));
    PanicFalse(SourceConfigure(audio_source_a, STREAM_AUDIO_SAMPLE_SIZE, 16));
    PanicFalse(SourceConfigure(audio_source_b, STREAM_CODEC_INPUT_RATE,
                               ADC_DAC_B_SAMPLE_RATE));
    PanicFalse(SourceConfigure(audio_source_b, STREAM_AUDIO_SAMPLE_SIZE, 16));

    PanicFalse(SinkConfigure(audio_sink_a, STREAM_CODEC_OUTPUT_RATE,
                             ADC_DAC_A_SAMPLE_RATE));
    PanicFalse(SinkConfigure(audio_sink_a, STREAM_AUDIO_SAMPLE_SIZE, 16));
    PanicFalse(SinkConfigure(audio_sink_b, STREAM_CODEC_OUTPUT_RATE,
                             ADC_DAC_B_SAMPLE_RATE));
    PanicFalse(SinkConfigure(audio_sink_b, STREAM_AUDIO_SAMPLE_SIZE, 16));

    /* Set up codec gains */
    PanicFalse(MicbiasConfigure(MIC_BIAS_0, MIC_BIAS_VOLTAGE, 0));
    PanicFalse(MicbiasConfigure(MIC_BIAS_1, MIC_BIAS_VOLTAGE, 0));

    PanicFalse(SourceConfigure(audio_source_a,
                               STREAM_CODEC_MIC_INPUT_GAIN_ENABLE, 1));
    PanicFalse(SourceConfigure(audio_source_b,
                               STREAM_CODEC_MIC_INPUT_GAIN_ENABLE, 1));

    PanicFalse(MicbiasConfigure(MIC_BIAS_0, MIC_BIAS_ENABLE,
                                MIC_BIAS_FORCE_ON));
    PanicFalse(MicbiasConfigure(MIC_BIAS_1, MIC_BIAS_ENABLE,
                                MIC_BIAS_FORCE_ON));
    /*PanicFalse(PsuConfigure(PSU_LINEAR2, PSU_ENABLE, 0));
    PanicFalse(PsuConfigure(PSU_SMPS1, PSU_ENABLE, 0));
    PanicFalse(PsuConfigure(PSU_SMPS0, PSU_ENABLE, 0));*/
    /*PanicFalse(PsuConfigure(PSU_LINEAR1, PSU_ENABLE, 0));*/

    PioSetPio(POWER_AMP_PIO, TRUE);


    /*9 =0 see Firmware configuration keys for CSR8670 and 8675
    PanicFalse(SourceConfigure(audio_source_a, STREAM_CODEC_INPUT_GAIN, 12));
    PanicFalse(SourceConfigure(audio_source_b, STREAM_CODEC_INPUT_GAIN, 12));
    PanicFalse(SinkConfigure(audio_sink_a, STREAM_CODEC_OUTPUT_GAIN, 9));
    PanicFalse(SinkConfigure(audio_sink_b, STREAM_CODEC_OUTPUT_GAIN, 9)); */

}


void
audio_manager_connect_kalimba(void)
{
    Source audio_source_b;
    Sink audio_sink_b;

    audio_source_b = StreamAudioSource(AUDIO_HARDWARE_CODEC,
                                       AUDIO_INSTANCE_0, AUDIO_CHANNEL_B);
    audio_sink_b = StreamAudioSink(AUDIO_HARDWARE_CODEC,
                                   AUDIO_INSTANCE_0, AUDIO_CHANNEL_B);

    AUDIO_CHECK(KalimbaSendMessage(CVC_LOADPARAMS_MSG, CVC_1MIC_PS_BASE,
                                  CVC_SR_NB, 0, 0));
    AUDIO_CHECK(KalimbaSendMessage(CVC_AUDIO_CONFIG_MSG, LINK_ENCODING_USB,
                                  AUDIO_CODEC_CVSD,
                                  AUDIO_CONFIG_SCO_RATE, 0));

    LOG_DEBUG(("Connecting up Kalimba block\n"));
    /* Connect streams:
     *          ADC_B ---> Kalimba (VCP + upsample to 24k) ---> I2S_0
     * @8kHz    DAC_B <--- Kalimba (VCP + dnsample to 8k ) <--- I2S_0 @24kHz
     */
    AUDIO_CHECK(StreamConnect(
            audio_source_b,
            StreamKalimbaSink(KALIMBA_PORT_CHAN_A_8KHZ_IN)));
    AUDIO_CHECK(StreamConnect(
            StreamKalimbaSource(KALIMBA_PORT_CHAN_A_8KHZ_OUT),
            audio_sink_b));
    /* Always printed: the DSP writes plain samples to this port (no SCO
     * packet headers), so the firmware should refuse this setting. If it
     * is accepted, the audio going to the headset would be read as packet
     * headers. Same call as before; only the result is now reported. */
    if (SourceConfigure(StreamKalimbaSource(KALIMBA_PORT_CHAN_B_8KHZ_OUT),
                        VM_SOURCE_SCO_METADATA_ENABLE, 1))
        log_print("AUDIO: DSP->headset port ACCEPTED SCO metadata\n");
    else
        log_print("AUDIO: DSP->headset port refused SCO metadata\n");

    audio_manager_update_routing();

    AUDIO_CHECK(KalimbaSendMessage(CVC_SETMODE_MSG, SYSMODE_HFK, 0,
                                  CALLST_DISCONNECTED, 0));
}


void
audio_manager_set_bt_ag_audio_route(
        audio_manager_bt_ag_audio_route_t bt_ag_audio_route)
{
    audio_data.ag_route = bt_ag_audio_route;
    audio_manager_update_routing();
}


/** Enumeration of mode composite of Bluetooth mode and AG route mode. */
typedef enum composite_mode_t
{
    /** Bluetooth source mode (headset 1). */
    CMODE_SOURCE_HS1,
    /** Bluetooth source mode (headset 2). */
    CMODE_SOURCE_HS2,
    /** Bluetooth sink mode. */
    CMODE_SINK
} composite_mode_t;


static composite_mode_t
get_current_composite_mode(void)
{
    bluetooth_mode_t bt_mode = bluetooth_manager_get_mode();
    if (bt_mode == BLUETOOTH_MODE_SINK)
        return CMODE_SINK;
    else if (audio_data.ag_route == BT_AG_AUDIO_OVERRIDES_HS1)
        return CMODE_SOURCE_HS1;
    else
        return CMODE_SOURCE_HS2;

}


/**
 * Look up table mapping the volume level (range 0-19) to a scaling factor
 * that is applied to each sample by the DSP for Bluetooth communications
 * (i.e., sample * scalar / 0x7FFF).
 *
 * These values are somewhat arbitrarily chosen, and can be tweaked to
 * adjust the perceived change in volume for each step.
 */
static const uint16 bt_volume_lut[] = {
    1036,
    1212,
    1418,
    1659,
    1941,
    2271,
    2657,
    3109,
    3638,
    4256,
    4980,
    5826,
    6817,
    7976,
    9331,
    10918,
    12774,
    14945,
    17486,
    20459,
    23937,
    28006,
    32767
};


/**
 * Looks up an entry for the given volume in \ref bt_volume_lut.
 *
 * \param volume    The PIO volume level to look up the corresponding LUT
 *                  entry for. This will be clamped to the size of the LUT.
 *
 * \returns the corresponding LUT value.
 */
static uint16
lookup_bt_volume_lut(uint16 volume)
{
    uint16 lut_size = sizeof(bt_volume_lut) / sizeof(bt_volume_lut[0]);
    if (volume >= lut_size)
        volume = lut_size - 1;
    return bt_volume_lut[volume];
}


#define BT_SPK_BOOST_LUT_SIZE  5


static const uint16 bt_spk_boost_db_lut[BT_SPK_BOOST_LUT_SIZE] =
    {0, 6, 12, 18, 26};


/* Kalimba USB_B.in gain per boost step (SET_LEVELS r3, before LSHIFT 8). */
static const uint16 bt_spk_boost_level_lut[BT_SPK_BOOST_LUT_SIZE] =
    {1024, 2048, 4096, 16384, 32767};


static uint16
bt_spk_boost_lut_index(uint16 db)
{
    uint16 i;

    for (i = 0; i < BT_SPK_BOOST_LUT_SIZE; i++)
    {
        if (db == bt_spk_boost_db_lut[i])
            return i;
    }
    return BT_SPK_BOOST_LUT_SIZE - 1;
}


uint16
audio_manager_normalize_bt_spk_boost_db(uint16 db)
{
    uint16 i;

    for (i = 0; i < BT_SPK_BOOST_LUT_SIZE; i++)
    {
        if (db == bt_spk_boost_db_lut[i])
            return db;
    }
    return BT_SPK_BOOST_DB_DEFAULT;
}


uint16
audio_manager_step_bt_spk_boost_db(uint16 current, bool increase)
{
    uint16 i = bt_spk_boost_lut_index(
            audio_manager_normalize_bt_spk_boost_db(current));

    if (increase && i < BT_SPK_BOOST_LUT_SIZE - 1)
        i++;
    else if (!increase && i > 0)
        i--;

    return bt_spk_boost_db_lut[i];
}


static uint16
lookup_bt_spk_boost_level(uint16 boost_db)
{
    uint16 i = bt_spk_boost_lut_index(
            audio_manager_normalize_bt_spk_boost_db(boost_db));

    return bt_spk_boost_level_lut[i];
}


void
audio_manager_set_hfk_call_state(bool connected)
{
    uint16 call_state = connected ? CALLST_CONNECTED : CALLST_DISCONNECTED;

    LOG_DEBUG(("Kalimba HFK call state -> %s\n",
               connected ? "CONNECTED" : "DISCONNECTED"));

    AUDIO_CHECK(KalimbaSendMessage(CVC_SETMODE_MSG, SYSMODE_HFK, 0,
                                  call_state, 0));
}


/**
 * Send a SET_LEVELS message to Kalimba.
 *
 * This will use the values set via the PIO interface.
 */
void
audio_manager_update_kalimba_levels(void)
{
    uint16 bt_mic_level;
    uint16 bt_spk_level;
    uint16 boost_db;
    Sink hfp_sink = bluetooth_manager_get_hfp_sink();

    /* If the Bluetooth headset is "Headset 1" then use mic/spk volume 1,
     * else use mic/spk volume 2. */
    if (get_current_composite_mode() == CMODE_SOURCE_HS1)
        bt_mic_level = lookup_bt_volume_lut(cfg->app.audio.pio_involume);
    else
        bt_mic_level = lookup_bt_volume_lut(cfg->app.audio.pio_involumeb);

    /* BT speaker boost only while SCO is active; wired paths stay at 0 dB. */
    if (hfp_sink != NULL)
        boost_db = cfg->app.audio.bt_spk_boost_db;
    else
        boost_db = 0;

    bt_spk_level = lookup_bt_spk_boost_level(boost_db);

    LOG_DEBUG(("Sending levels to Kalimba: sidetone=%u, bt_mic=0x%04x, "
               "bt_spk_boost=%u dB (0x%04x, SCO %s)\n",
               cfg->app.audio.pio_side, bt_mic_level,
               boost_db, bt_spk_level,
               (hfp_sink != NULL) ? "up" : "down"));

    AUDIO_CHECK(KalimbaSendMessage(SET_LEVELS_MSG, cfg->app.audio.pio_side,
                                  bt_mic_level, bt_spk_level, 0));
}


void
audio_manager_update_routing(void)
{
    Source audio_source_a = StreamAudioSource(
            AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0, AUDIO_CHANNEL_A);
    Sink audio_sink_a = StreamAudioSink(
            AUDIO_HARDWARE_CODEC, AUDIO_INSTANCE_0, AUDIO_CHANNEL_A);

    Source kalimba_source_a_24khz =
            StreamKalimbaSource(KALIMBA_PORT_CHAN_A_24KHZ_OUT);
    Sink kalimba_sink_a_24khz =
            StreamKalimbaSink(KALIMBA_PORT_CHAN_A_24KHZ_IN);

    Source kalimba_source_b_8khz =
            StreamKalimbaSource(KALIMBA_PORT_CHAN_B_8KHZ_OUT);
    Sink kalimba_sink_b_8khz =
            StreamKalimbaSink(KALIMBA_PORT_CHAN_B_8KHZ_IN);
    Source kalimba_source_b_24khz =
            StreamKalimbaSource(KALIMBA_PORT_CHAN_B_24KHZ_OUT);
    Sink kalimba_sink_b_24khz =
            StreamKalimbaSink(KALIMBA_PORT_CHAN_B_24KHZ_IN);

    Source kalimba_source_c_8khz =
            StreamKalimbaSource(KALIMBA_PORT_CHAN_C_8KHZ_OUT);
    Sink kalimba_sink_c_8khz =
            StreamKalimbaSink(KALIMBA_PORT_CHAN_C_8KHZ_IN);
    Source kalimba_source_c_24khz =
            StreamKalimbaSource(KALIMBA_PORT_CHAN_C_24KHZ_OUT);
    Sink kalimba_sink_c_24khz =
            StreamKalimbaSink(KALIMBA_PORT_CHAN_C_24KHZ_IN);


    Source i2s_source_0 = StreamAudioSource(
            AUDIO_HARDWARE_I2S, AUDIO_INSTANCE_0, AUDIO_CHANNEL_SLOT_0);
    Source i2s_source_1 = StreamAudioSource(
            AUDIO_HARDWARE_I2S, AUDIO_INSTANCE_0, AUDIO_CHANNEL_SLOT_1);
    Sink i2s_sink_0 = StreamAudioSink(
            AUDIO_HARDWARE_I2S, AUDIO_INSTANCE_0, AUDIO_CHANNEL_SLOT_0);
    Sink i2s_sink_1 = StreamAudioSink(
            AUDIO_HARDWARE_I2S, AUDIO_INSTANCE_0, AUDIO_CHANNEL_SLOT_1);

    Sink hfp_sink = bluetooth_manager_get_hfp_sink();
    Source hfp_source = NULL;

    composite_mode_t cmode = get_current_composite_mode();

    /* Attenuate wired HS1 when SCO replaces it. */
    update_wired_gains_for_bt(hfp_sink);
    audio_manager_apply_wired_codec_gains();

    StreamDisconnect(audio_source_a, audio_sink_a);
    StreamDisconnect(kalimba_source_a_24khz, kalimba_sink_a_24khz);
    StreamDisconnect(kalimba_source_b_24khz, kalimba_sink_b_24khz);
    StreamDisconnect(kalimba_source_b_8khz, kalimba_sink_b_8khz);
    StreamDisconnect(kalimba_source_c_24khz, kalimba_sink_c_24khz);
    StreamDisconnect(kalimba_source_c_8khz, kalimba_sink_c_8khz);

    LOG_DEBUG(("Update audio routing (BT %s, %s mode)\n",
               (hfp_sink == NULL) ? "disconnected" : "connected",
               (cmode == CMODE_SINK) ? "sink" :
                 ((cmode == CMODE_SOURCE_HS1) ? "source (HS1)" :
                        "source (HS2)")));


    if (hfp_sink != NULL)
    {
        hfp_source = StreamSourceFromSink(hfp_sink);
        StreamDisconnect(hfp_source, hfp_sink);
        SourceConfigure(hfp_source, VM_SOURCE_SCO_METADATA_ENABLE, 1);

        LOG_DEBUG(("KAL B 8kHz <> BT\n"));
        AUDIO_CHECK(StreamConnect(kalimba_source_b_8khz, hfp_sink));
        AUDIO_CHECK(StreamConnect(hfp_source, kalimba_sink_b_8khz));
    }

    if (cmode != CMODE_SOURCE_HS1 || hfp_sink == NULL)
    {
        LOG_DEBUG(("I2S 0 <> KAL_A 24kHz (headset 1)\n"));
        AUDIO_CHECK(StreamConnect(kalimba_source_a_24khz, i2s_sink_0));
        AUDIO_CHECK(StreamConnect(i2s_source_0, kalimba_sink_a_24khz));
    }
    else
    {
        LOG_DEBUG(("I2S 0 <> KAL_B 24kHz (BT)\n"));
        AUDIO_CHECK(StreamConnect(kalimba_source_b_24khz, i2s_sink_0));
        AUDIO_CHECK(StreamConnect(i2s_source_0, kalimba_sink_b_24khz));
    }

    if (cmode == CMODE_SOURCE_HS1 || hfp_sink == NULL)
    {
        LOG_DEBUG(("I2S 1 <> ANA_A (headset 2)\n"));
        AUDIO_CHECK(StreamConnect(audio_source_a, kalimba_sink_c_8khz));
        AUDIO_CHECK(StreamConnect(kalimba_source_c_24khz, i2s_sink_1));
        AUDIO_CHECK(StreamConnect(i2s_source_1, kalimba_sink_c_24khz));
        AUDIO_CHECK(StreamConnect(kalimba_source_c_8khz, audio_sink_a));
    }
    else
    {
        LOG_DEBUG(("I2S 1 <> KAL B 24kHz (BT)\n"));
        AUDIO_CHECK(StreamConnect(kalimba_source_b_24khz, i2s_sink_1));
        AUDIO_CHECK(StreamConnect(i2s_source_1, kalimba_sink_b_24khz));
    }
/* Enable Alango Logger */
#if defined(LOG_MASK)
    PioSetKalimbaControl32(LOG_MASK, LOG_MASK);
#endif
}
