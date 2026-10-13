#include "core_library.h"
#include "cbops_multirate_library.h"
#include "kalimba_messages.h"
#include "frame_sync_library.h"
#include "frame_codec.h"
#include "usbio.h"
#include "plc100_library.h"
#include "one_mic_example.h"
#include "frame_sync_stream_macros.h"
#include "flash.h"
#include "stream_copy.h"
#include "sbc_library.h"
#include "codec_library.h"

#include "frame_sync_tsksched.h"

#include "VCP.h"
#include "ports.h"


   .CONST  $ADC_PORT_L           (($cbuffer.READ_PORT_MASK  + $CHAN_A_8KHZ_IN_PORT_NUMBER) | $cbuffer.FORCE_PCM_AUDIO);
   .CONST  $ADC_PORT_R           (($cbuffer.READ_PORT_MASK  + $CHAN_C_8KHZ_IN_PORT_NUMBER) | $cbuffer.FORCE_PCM_AUDIO);
   //--1
   .CONST  $DAC_PORT_L           (($cbuffer.WRITE_PORT_MASK + $CHAN_A_8KHZ_OUT_PORT_NUMBER) | $cbuffer.FORCE_PCM_AUDIO);
   .CONST  $DAC_PORT_R           (($cbuffer.WRITE_PORT_MASK + $CHAN_C_8KHZ_OUT_PORT_NUMBER) | $cbuffer.FORCE_PCM_AUDIO);
   .CONST  $TONE_PORT            (($cbuffer.READ_PORT_MASK  + 7) | $cbuffer.FORCE_PCM_AUDIO); // Unused


// INPUT/OUTPUT streams
// --------------------------------------------------------------------------

#define ADCINDEX_PORT_L          0
#define ADCINDEX_PORT_R          1
#define ADCINDEX_CBUFFER_L       2
#define ADCINDEX_CBUFFER_R       3
#define ADCINDEX_SIDETONE        4
#define ADCINDEX_INTERNAL_L      5
#define ADCINDEX_INTERNAL_R      6
#define ADCINDEX_NONE            -1

.MODULE $adc_in;
   .DATASEGMENT DM;

   DeclareCBuffer (left.cbuffer_struc,mem_l,$BLOCK_SIZE * $BUFFER_SCALING);
   DeclareCBuffer (right.cbuffer_struc,mem_r,$BLOCK_SIZE * $BUFFER_SCALING);
   DeclareCBuffer (sidetone_cbuffer_struc,sidetone_mem,4*($SAMPLE_RATE / 1000));
   .VAR $sw_ratematching_adc = 0;

   .VAR/DM1CIRC    sr_hist_l[$cbops.rate_adjustment_and_shift.SRA_COEFFS_SIZE];
   .VAR/DM1CIRC    sr_hist_r[$cbops.rate_adjustment_and_shift.SRA_COEFFS_SIZE];

   .VAR copy_struc[] =
      $cbops.scratch.BufferTable,     // BUFFER_TABLE_FIELD
      &copy_op_l,                       // MAIN_FIRST_OPERATOR_FIELD
      &sidetone_copy_op,              // MTU_FIRST_OPERATOR_FIELD
      2,                              // NUM_INPUTS_FIELD
      $ADC_PORT_L,
      $ADC_PORT_R,
      3,                              // NUM_OUTPUTS_FIELD
      &left.cbuffer_struc,
      &right.cbuffer_struc,
      &sidetone_cbuffer_struc,
      2,                              // NUM_INTERNAL_FIELD
      $cbops.scratch.cbuffer_struc2,
      $cbops.scratch.cbuffer_struc3;

    .BLOCK copy_op_l;
        .VAR copy_op_l.mtu_next  = $cbops.NO_MORE_OPERATORS;
        .VAR copy_op_l.main_next = &copy_op_r;
        .VAR copy_op_l.func = $cbops_iir_resamplev2;
        .VAR copy_op_l.param[$iir_resamplev2.OBJECT_SIZE] =
      ADCINDEX_PORT_L,       /*INPUT_INDEX_FIELD*/
      ADCINDEX_INTERNAL_L,    /*OUTPUT_INDEX_FIELD*/
            0,                                  // FILTER_DEFINITION_PTR_FIELD  [---CONFIG---]
            0,                                  // INPUT_SCALE_FIELD   (input Q15)
            8,                                  // OUTPUT_SCALE_FIELD  (output Q23)
            &$cbops.scratch.mem1,               // INTERMEDIATE_CBUF_PTR_FIELD
            LENGTH($cbops.scratch.mem1),        // INTERMEDIATE_CBUF_LEN_FIELD
            0 ...;
    .ENDBLOCK;

    .BLOCK copy_op_r;
        .VAR copy_op_r.mtu_next  = &copy_op_l;
        .VAR copy_op_r.main_next = &sw_rate_op;//hw_rate_op;
        .VAR copy_op_r.func = $cbops_iir_resamplev2;//&$cbops.shift;
        .VAR copy_op_r.param[$iir_resamplev2.OBJECT_SIZE] =
      ADCINDEX_PORT_R,       /*INPUT_INDEX_FIELD*/
      ADCINDEX_INTERNAL_R,    /*OUTPUT_INDEX_FIELD*/                                                                                                // INPUT_SCALE_FIELD   (input Q15)
            0,                                  // FILTER_DEFINITION_PTR_FIELD  [---CONFIG---]
            0,                                  // INPUT_SCALE_FIELD   (input Q15)
            8,                                  // OUTPUT_SCALE_FIELD  (output Q23)
            &$cbops.scratch.mem1,               // INTERMEDIATE_CBUF_PTR_FIELD
            LENGTH($cbops.scratch.mem1),        // INTERMEDIATE_CBUF_LEN_FIELD
            0 ...;
            //ADCINDEX_PORT,        /*INPUT_INDEX_FIELD*/
            //ADCINDEX_CBUFFER,     /*OUTPUT_INDEX_FIELD*/
            //8;                    /*SHIFT_AMOUNT_FIELD*/
    .ENDBLOCK;

    // Software rate matching uses internal buffers
    .BLOCK sw_rate_op;
        .VAR sw_rate_op.mtu_next  = &copy_op_r;
        .VAR sw_rate_op.main_next = &sw_copy_op_l;
        .VAR sw_rate_op.func = &$cbops.rate_monitor_op;
        .VAR sw_rate_op.param[$cbops.rate_monitor_op.STRUC_SIZE] =
            ADCINDEX_INTERNAL_L ,     // MONITOR_INDEX_FIELD
            1600,                       // PERIODS_PER_SECOND_FEILD
            10,                         // SECONDS_TRACKED_FIELD
            0,                          // TARGET_RATE_FIELD    [---CONFIG---]
            10,                         // ALPHA_LIMIT_FIELD (controls the size of the averaging window)
            0.5,                        // AVERAGE_IO_RATIO_FIELD - initialize to 1.0 in q.22
            11,                         // WARP_MSG_LIMIT_FIELD
            120,                        // IDLE_PERIODS_AFTER_STALL_FIELD (empirical)
            0 ...;
    .ENDBLOCK;

    .BLOCK sw_copy_op_l;
        .VAR sw_copy_op_l.mtu_next  = &sw_rate_op;
        .VAR sw_copy_op_l.main_next = &sw_copy_op_r;//sw_copy_right_op;
        .VAR sw_copy_op_l.func = &$cbops.rate_adjustment_and_shift;
        .VAR sw_copy_op_l.param[$cbops.rate_adjustment_and_shift.STRUC_SIZE] =
            ADCINDEX_INTERNAL_L,     // INPUT1_START_INDEX_FIELD
            ADCINDEX_CBUFFER_L,      // OUTPUT1_START_INDEX_FIELD
            0,                          // SHIFT_AMOUNT_FIELD
            0,                          // MASTER_OP_FIELD
            &$sra_coeffs,               // FILTER_COEFFS_FIELD
            &sr_hist_l,              // HIST1_BUF_FIELD
            &sr_hist_l,              // HIST1_BUF_START_FIELD
            &sw_rate_op.param + $cbops.rate_monitor_op.WARP_VALUE_FIELD, // SRA_TARGET_RATE_ADDR_FIELD
            0,                          // ENABLE_COMPRESSOR_FIELD
            0 ...;
    .ENDBLOCK;

    .BLOCK sw_copy_op_r;
        .VAR sw_copy_op_r.mtu_next  = &sw_copy_op_l;
        .VAR sw_copy_op_r.main_next = &sidetone_copy_op;
        .VAR sw_copy_op_r.func = &$cbops.rate_adjustment_and_shift;
        .VAR sw_copy_op_r.param[$cbops.rate_adjustment_and_shift.STRUC_SIZE] =
            ADCINDEX_INTERNAL_R,     // INPUT1_START_INDEX_FIELD
            ADCINDEX_CBUFFER_R,      // OUTPUT1_START_INDEX_FIELD
            0,                          // SHIFT_AMOUNT_FIELD
            0,                          // MASTER_OP_FIELD
            &$sra_coeffs,               // FILTER_COEFFS_FIELD
            &sr_hist_r,              // HIST1_BUF_FIELD
            &sr_hist_r,              // HIST1_BUF_START_FIELD
            &sw_rate_op.param + $cbops.rate_monitor_op.WARP_VALUE_FIELD, // SRA_TARGET_RATE_ADDR_FIELD
            0,                          // ENABLE_COMPRESSOR_FIELD
            0 ...;
    .ENDBLOCK;

    .BLOCK sidetone_copy_op;
       .VAR sidetone_copy_op.mtu_next  = &sw_copy_op_r;
       .VAR sidetone_copy_op.main_next = $cbops.NO_MORE_OPERATORS;
       .VAR sidetone_copy_op.func = &$cbops.shift;
       .VAR sidetone_copy_op.param[$cbops.shift.STRUC_SIZE] =
          ADCINDEX_CBUFFER_L,         /* Input index field (Input cbuffer) */
          ADCINDEX_SIDETONE,    /* Output index field (SIDE TONE cbuffer) */
          0;                    /* Shift parameter field */
   .ENDBLOCK;

.ENDMODULE;

#define DACINDEX_CBUFFER_L        0
#define DACINDEX_CBUFFER_R        1
#define DACINDEX_SIDETONE         2
#define DACINDEX_PORT_LEFT        3
#define DACINDEX_PORT_RIGHT       4
#define DACINDEX_REFERENCE        5
#define DACINDEX_INTERNAL_1       6
#define DACINDEX_INTERNAL_2       7
#define DACINDEX_NONE             -1

.MODULE $dac_out;
   .DATASEGMENT DM;

   .VAR/DM1CIRC    sr_hist_lo[$cbops.rate_adjustment_and_shift.SRA_COEFFS_SIZE];
   .VAR/DM1CIRC    sr_hist_ro[$cbops.rate_adjustment_and_shift.SRA_COEFFS_SIZE];
   .VAR spkr_out_pk_dtct = 0;
   .VAR $sw_ratematching_dac = 0;
   DeclareCBuffer (cbuffer_struc_l,mem_l,$BLOCK_SIZE * $BUFFER_SCALING);
   DeclareCBuffer (cbuffer_struc_r,mem_r,$BLOCK_SIZE * $BUFFER_SCALING);
   DeclareCBuffer (ref_struc, reference_mem,$BLOCK_SIZE * $BUFFER_SCALING * 4);

   .VAR $DAC_RightPortEnabled=1;

   .VAR copy_struc[] =
        $cbops.scratch.BufferTable,     // BUFFER_TABLE_FIELD
        &insert_op_l,                     // MAIN_FIRST_OPERATOR_FIELD
        &insert_op_l,                     // MTU_FIRST_OPERATOR_FIELD
        3,                              // NUM_INPUTS_FIELD
        &cbuffer_struc_l,
        &cbuffer_struc_r,
        &$adc_in.sidetone_cbuffer_struc,
        3,                              // NUM_OUTPUTS_FIELD
        $DAC_PORT_L,
        $DAC_PORT_R,
        &ref_struc,
        2,                              // NUM_INTERNAL_FIELD
        $cbops.scratch.cbuffer_struc2,  // buffer 1 -> scratch 1 (resample)
        $cbops.scratch.cbuffer_struc3;  // scratch 1 (resample) -> scratch 2 (RM)


    .VAR $sidetone_gain = 0.0;

    // cBuffer Insertion Operator (Keep DAC chain Fed)
    .BLOCK insert_op_l;
    .VAR insert_op_l.mtu_next  = &insert_op_r;
    .VAR insert_op_l.main_next = &insert_op_r;
    .VAR insert_op_l.func = &$cbops.insert_op;
    .VAR insert_op_l.param[$cbops.insert_op.STRUC_SIZE] =
        DACINDEX_CBUFFER_L,           // BUFFER_INDEX_FIELD
        6,                  // MAX_ADVANCE_FIELD
        0 ...;
    .ENDBLOCK;

    // cBuffer Insertion Operator (Keep DAC chain Fed)
    .BLOCK insert_op_r;
    .VAR insert_op_r.mtu_next  = &dac_wrap_op;
    .VAR insert_op_r.main_next = &audio_out_volume;
    .VAR insert_op_r.func = &$cbops.insert_op;
    .VAR insert_op_r.param[$cbops.insert_op.STRUC_SIZE] =
        DACINDEX_CBUFFER_R,           // BUFFER_INDEX_FIELD
        6,                  // MAX_ADVANCE_FIELD
        0 ...;
    .ENDBLOCK;

    .BLOCK audio_out_volume;
        .VAR audio_out_volume.mtu_next  = $cbops.NO_MORE_OPERATORS;
        .VAR audio_out_volume.main_next = &auxillary_mix_op;
        .VAR audio_out_volume.func = &$cbops.volume;
        .VAR audio_out_volume.param[$cbops.volume.STRUC_SIZE] =
            DACINDEX_CBUFFER_L,                // INPUT_START_INDEX_FIELD
            DACINDEX_CBUFFER_L,                // OUTPUT_START_INDEX_FIELD
            1.0,                             // Final Gain Value
            0,                               // Current Gain Value
            0,                               // samples per step,
            -5,                              // step shift field
            0.01,                            // delta field
            0;                               // current step field
      .ENDBLOCK;

    .BLOCK auxillary_mix_op;
   .VAR auxillary_mix_op.mtu_next  = &audio_out_volume;//$cbops.NO_MORE_OPERATORS;
   .VAR auxillary_mix_op.main_next = &reference_copy_op;
   .VAR auxillary_mix_op.func = &$cbops.aux_audio_mix_op;
   .VAR auxillary_mix_op.param[$cbops.aux_audio_mix_op.STRUC_SIZE] =
      DACINDEX_CBUFFER_L,                /* Input index (Output cbuffer) */
      DACINDEX_CBUFFER_L,                /* Output index (Output cbuffer) */
      $TONE_PORT,                      /* Auxillary Audio Port */
      $tone_in.cbuffer_struc,          /* Auxillary Audio CBuffer */
      0,                               /* Hold Timer */
      -154,                            /* Hold count.  0.625 msec (ISR rate) * 154 = ~ 96 msec */
      0x80000,   /*(0db) */            /* Auxillary Gain  (Durring Tone Mix)  (Q5.18) */
      0x80000,   /*(0db) */            /* Main Gain       (Durring Tone Mix)  (Q5.18) */
      0x008000,  /*(0db) */            /* OFFSET_INV_DAC_GAIN  (Q8.15) */
      1.0,                             /* Volume Independent Clip Point (Q23)*/
      1.0,                             /* Absolute Clip Point  (Q23)*/
      0x40000,                         /* Global Gain i.e. Boost (Q4.19)*/
      0,                               /* Auxillary Audio Peak Statistic */
      1.0,                             /* Inverse gain difference between Main & Tone Volume (Q23) */
      0;                               /* Internal Data */
   .ENDBLOCK;

    // Tap for AEC reference  (Use $cbops.shift to share code)
    .BLOCK reference_copy_op;
        .VAR reference_copy_op.mtu_next = &auxillary_mix_op;
        .VAR reference_copy_op.main_next = &sidetone_mix_op;
        .VAR reference_copy_op.func = &$cbops.shift;
        .VAR reference_copy_op.param[$cbops.shift.STRUC_SIZE] =
             DACINDEX_CBUFFER_L,     // Input index
             DACINDEX_REFERENCE,   // Output index
             0;                    // SHIFT_AMOUNT_FIELD
    .ENDBLOCK;

    .BLOCK sidetone_mix_op;
        .VAR sidetone_mix_op.mtu_next  = &reference_copy_op;
        .VAR sidetone_mix_op.main_next = &pk_out_dac;
        .VAR sidetone_mix_op.func = &$cbops.sidetone_mix_op;
        .VAR sidetone_mix_op.param[$cbops.sidetone_mix_op.STRUC_SIZE] =
            DACINDEX_CBUFFER_L,       // INPUT_START_INDEX_FIELD
            DACINDEX_INTERNAL_1,       // OUTPUT_START_INDEX_FIELD
            DACINDEX_SIDETONE,      // SIDETONE_START_INDEX_FIELD
            0,                      // SIDETONE_MAX_SAMPLES_FIELD
            &$sidetone_gain;        // ATTENUATION_PTR_FIELD
    .ENDBLOCK;

    .BLOCK pk_out_dac;
      .VAR pk_out_dac.mtu_next = &sidetone_mix_op;
        .VAR pk_out_dac.main_next= &rate_match_mux;
        .VAR pk_out_dac.func    = &$cbops.peak_monitor_op;
        .VAR pk_out_dac.param[$cbops.peak_monitor_op.STRUC_SIZE] =
            DACINDEX_INTERNAL_1,  // PTR_INPUT_BUFFER_FIELD
            &spkr_out_pk_dtct;    // PEAK_LEVEL_PTR - UFE stat
    .ENDBLOCK;

    // Mux has only main part
    .BLOCK rate_match_mux;
        .VAR rate_match_mux.mtu_next = &pk_out_dac;
        .VAR rate_match_mux.main_next = 0; // Set by mux
        .VAR rate_match_mux.func    = &$cbops.mux_1to2_op;
        .VAR rate_match_mux.param[$cbops.mux_1to2_op.STRUC_SIZE] =
            &$sw_ratematching_dac,    // PTR_STATE_FIELD
            &left_sw_rate_adjust_op,  // MAIN_TRUE_FIELD
            &hw_copy_op;              // MAIN_FALSE_FIELD
    .ENDBLOCK;

    // Apply rate adjustment in software
    .BLOCK left_sw_rate_adjust_op;
        .VAR left_sw_rate_adjust_op.mtu_next  = &rate_match_mux;
        .VAR left_sw_rate_adjust_op.main_next = &left_sw_rate_monitor_op;
        .VAR left_sw_rate_adjust_op.func = &$cbops.rate_adjustment_and_shift;
        .VAR left_sw_rate_adjust_op.param[$cbops.rate_adjustment_and_shift.STRUC_SIZE] =
            DACINDEX_INTERNAL_1,        // INPUT1_START_INDEX_FIELD
            DACINDEX_INTERNAL_2,        // OUTPUT1_START_INDEX_FIELD
            0,                          // SHIFT_AMOUNT_FIELD
            0,                          // MASTER_OP_FIELD
            &$sra_coeffs,               // FILTER_COEFFS_FIELD
            &sr_hist_lo,                // HIST1_BUF_FIELD
            &sr_hist_lo,                // HIST1_BUF_START_FIELD
            &left_sw_rate_monitor_op.param + $cbops.rate_monitor_op.INVERSE_WARP_VALUE_FIELD, // SRA_TARGET_RATE_ADDR_FIELD
            0,                          // ENABLE_COMPRESSOR_FIELD
            0 ...;
    .ENDBLOCK;

    // Software rate monitor dac chain independently
    .BLOCK left_sw_rate_monitor_op;
        .VAR left_sw_rate_monitor_op.mtu_next  = &left_sw_rate_adjust_op;
        .VAR left_sw_rate_monitor_op.main_next = &rate_match_demux;
        .VAR left_sw_rate_monitor_op.func = &$cbops.rate_monitor_op;
        .VAR left_sw_rate_monitor_op.param[$cbops.rate_monitor_op.STRUC_SIZE] =
            DACINDEX_INTERNAL_2,        // MONITOR_INDEX_FIELD
            1600,                       // PERIODS_PER_SECOND_FIELD
            10,                         // SECONDS_TRACKED_FIELD
            0,                          // TARGET_RATE_FIELD    [---CONFIG---]
            10,                         // ALPHA_LIMIT_FIELD (controls the size of the averaging window)
            0.5,                        // AVERAGE_IO_RATIO_FIELD - initialize to 1.0 in q.22
            11,                         // WARP_MSG_LIMIT_FIELD
            120,                        // IDLE_PERIODS_AFTER_STALL_FIELD (empirical)
            0 ...;
    .ENDBLOCK;

    // Keep mtu and main paths of mux/demux in symmetry
    .BLOCK hw_copy_op;
        .VAR hw_copy_op.mtu_next  = &rate_match_mux;
        .VAR hw_copy_op.main_next = &left_hw_rate_adjust_op;
        .VAR hw_copy_op.func = &$cbops.copy_op;
        .VAR hw_copy_op.param[$cbops.copy_op.STRUC_SIZE] =
            DACINDEX_INTERNAL_1,  // INPUT_START_INDEX_FIELD
            DACINDEX_INTERNAL_2;  // OUTPUT_START_INDEX_FIELD
    .ENDBLOCK;

    // Hardware rate monitor and send message dac chain independently
    .BLOCK left_hw_rate_adjust_op;
        .VAR left_hw_rate_adjust_op.mtu_next  = &hw_copy_op;
        .VAR left_hw_rate_adjust_op.main_next = &rate_match_demux;
#ifndef USE_FRAMESYNC
        .VAR left_hw_rate_adjust_op.func = &$cbops.hw_warp_op;
        .VAR left_hw_rate_adjust_op.param[$cbops.hw_warp_op.STRUC_SIZE] =
#else
        .VAR left_hw_rate_adjust_op.func = &$frame_sync.hw_warp_op;
        .VAR left_hw_rate_adjust_op.param[$frame_sync.hw_warp_op.STRUC_SIZE] =
#endif
            $DAC_PORT_L,        // PORT_OFFSET - Tracks Connectivity
            DACINDEX_INTERNAL_2,// MONITOR_INDEX_OFFSET - Monitors throughput
            0x30,               // WHICH_PORTS_OFFSET
            0,                  // TARGET_RATE_OFFSET    [---CONFIG---]
            1600,               // PERIODS_PER_SECOND_OFFSET
            3,                  // COLLECT_SECONDS_OFFSET
            1,                  // ENABLE_DITHER_OFFSET  [---CONFIG---]
            0 ...;
    .ENDBLOCK;

    // Demux has only mtu part
    .BLOCK rate_match_demux;
        .VAR rate_match_demux.mtu_next = 0; // Set by demux
        .VAR rate_match_demux.main_next = &left_copy_op;
        .VAR rate_match_demux.func    = &$cbops.demux_2to1_op;
        .VAR rate_match_demux.param[$cbops.demux_2to1_op.STRUC_SIZE] =
            &$sw_ratematching_dac,     // PTR_STATE_FIELD
            &left_sw_rate_monitor_op,  // MTU_TRUE_FIELD
            &left_hw_rate_adjust_op;   // MTU_FALSE_FIELD
    .ENDBLOCK;

    // Using shift here instead of copy. Not shifting in rate adjust and shift
    .BLOCK left_copy_op;
        .VAR left_copy_op.mtu_next  = &rate_match_demux;
        .VAR left_copy_op.main_next = &right_sw_rate_monitor_op;
        .VAR left_copy_op.func = &$cbops.shift;
        .VAR left_copy_op.param[$cbops.shift.STRUC_SIZE] =
            DACINDEX_INTERNAL_2,  // INPUT_START_INDEX_FIELD
            DACINDEX_PORT_LEFT,   // OUTPUT_START_INDEX_FIELD
            -8;
    .ENDBLOCK;

     // Software rate monitor dac chain independently
    .BLOCK right_sw_rate_monitor_op;
        .VAR right_sw_rate_monitor_op.mtu_next  = &left_copy_op;
        .VAR right_sw_rate_monitor_op.main_next = &right_sw_rate_adjust_op;
        .VAR right_sw_rate_monitor_op.func = &$cbops.rate_monitor_op;
        .VAR right_sw_rate_monitor_op.param[$cbops.rate_monitor_op.STRUC_SIZE] =
            DACINDEX_CBUFFER_R,        // MONITOR_INDEX_FIELD
            1600,                       // PERIODS_PER_SECOND_FIELD
            10,                         // SECONDS_TRACKED_FIELD
            0,                          // TARGET_RATE_FIELD    [---CONFIG---]
            10,                         // ALPHA_LIMIT_FIELD (controls the size of the averaging window)
            0.5,                        // AVERAGE_IO_RATIO_FIELD - initialize to 1.0 in q.22
            11,                         // WARP_MSG_LIMIT_FIELD
            120,                        // IDLE_PERIODS_AFTER_STALL_FIELD (empirical)
            0 ...;
    .ENDBLOCK;

    // Apply rate adjustment in software
    .BLOCK right_sw_rate_adjust_op;
        .VAR right_sw_rate_adjust_op.mtu_next  = &right_sw_rate_monitor_op;
        .VAR right_sw_rate_adjust_op.main_next = &dac_wrap_op;
        .VAR right_sw_rate_adjust_op.func = &$cbops.rate_adjustment_and_shift;
        .VAR right_sw_rate_adjust_op.param[$cbops.rate_adjustment_and_shift.STRUC_SIZE] =
            DACINDEX_CBUFFER_R,         // INPUT1_START_INDEX_FIELD
            DACINDEX_PORT_RIGHT,        // OUTPUT1_START_INDEX_FIELD
            -8,                          // SHIFT_AMOUNT_FIELD
            0,                          // MASTER_OP_FIELD
            &$sra_coeffs,               // FILTER_COEFFS_FIELD
            &sr_hist_ro,                 // HIST1_BUF_FIELD
            &sr_hist_ro,                 // HIST1_BUF_START_FIELD
            &right_sw_rate_monitor_op.param + $cbops.rate_monitor_op.WARP_VALUE_FIELD, // SRA_TARGET_RATE_ADDR_FIELD
            0,                          // ENABLE_COMPRESSOR_FIELD
            0 ...;
    .ENDBLOCK;

    // Check DAC for wrap.  Always last operator
    .BLOCK dac_wrap_op;
        .VAR dac_wrap_op.mtu_next  = &right_sw_rate_adjust_op;
        .VAR dac_wrap_op.main_next = $cbops.NO_MORE_OPERATORS;
        .VAR dac_wrap_op.func = &$cbops.port_wrap_op;
        .VAR dac_wrap_op.param[$cbops.port_wrap_op.STRUC_SIZE] =
            DACINDEX_PORT_LEFT, // LEFT_PORT_FIELD
            DACINDEX_PORT_RIGHT,    // RIGHT_PORT_FIELD
            3,                 // BUFFER_ADJUST_FIELD
            6,                 // MAX_ADVANCE_FIELD
            1,                 // SYNC_INDEX_FIELD
            0;                 // internal : WRAP_COUNT_FIELD
    .ENDBLOCK;

   .ENDMODULE;

.MODULE $tone_in;
    .DATASEGMENT DM;

    // SP.  Need to add a little headroom above a frame to
    //      handle conversion ratio plus maximum fill is size-1
    DeclareCBuffer(cbuffer_struc,mem, ($BLOCK_SIZE * 2)+3 );

    .VAR aux_input_stream_available;    // local pcm file is being mixed

#ifdef KAL_ARCH2
   .VAR/DM1CIRC  fir_buf[$IIR_RESAMPLEV2_FIR_BUFFER_SIZE];
   .VAR/DM1CIRC  iir_buf[$IIR_RESAMPLEV2_IIR_BUFFER_SIZE];
#endif

   .VAR copy_struc[] =
      $cbops.scratch.BufferTable,      // BUFFER_TABLE_FIELD
      &copy_op,                       // MAIN_FIRST_OPERATOR_FIELD
      &copy_op,                       // MTU_FIRST_OPERATOR_FIELD
      1,                               // NUM_INPUTS_FIELD
      $TONE_PORT,
      1,                               // NUM_OUTPUTS_FIELD
      &cbuffer_struc,
      0;                              // NUM_INTERNAL_FIELD

  .BLOCK copy_op;
      .VAR copy_op.mtu_next  = $cbops.NO_MORE_OPERATORS;
      .VAR copy_op.main_next = $cbops.NO_MORE_OPERATORS;
      .VAR copy_op.func = $cbops_iir_resamplev2;
      .VAR copy_op.param[$iir_resamplev2.OBJECT_SIZE] =
         0,                                  // Input index
         1,                                  // Output index
         0,
         0,                                  // INPUT_SCALE_FIELD
         8,                                  // OUTPUT_SCALE_FIELD,
         &$cbops.scratch.mem1,         // INTERMEDIATE_CBUF_PTR_FIELD
         LENGTH($cbops.scratch.mem1),
         0 ...;
   .ENDBLOCK;

.ENDMODULE;

.MODULE $M.analog;
   .CODESEGMENT PM;
   .DATASEGMENT DM;


   .VAR audio_if_mode = 0; // 0=ADC+DAC, 1=I2S, etc.
   .VAR audio_rate_matching = $SW_RATE_MATCH_ENABLE_MASK;// ICINNOVATE was $HW_RATE_MATCH_ENABLE_MASK;//$HW_RATE_MATCH_MASK;

   // Sampling Rate
   .VAR dac_sampling_rate=8000;
   .VAR adc_sampling_rate=8000;

   .VAR dac_param_loaded = 1; // ICINNOVATE was 0

   .VAR frame_dac_sampling_rate=8000;
   .VAR frame_adc_sampling_rate=8000;
   .var $dac_first_init = 1;

$ConfigureDACOutput:
    $push_rLink_macro;

//wait.configDAC:
//   Null = M[&dac_param_loaded];
//   if Z jump wait.configDAC;
    M[&$dac_first_init] = Null;

    call $interrupt.block;
    // Get CVC variant
    r9 = 8000;
    // Setup sample rates based on variant
    r1 = 8000;      // 8  kHz

    M[frame_dac_sampling_rate]=r1;
    M[frame_adc_sampling_rate]=r1;

    // Setup Aux Tone sample rate 8kHz or 16kHz
    r0 = &$M.iir_resamplev2.Up_2_Down_1.filter;
    Null = r9 - 8000;
    if Z r0=NULL;
    r8 = &$tone_in.copy_op.param;
    call $iir_resamplev2.SetFilter;

    // Set Front end Frame Size
    r0 = 60;
    NULL = r9 - 8000;
    if NZ r0 = r0 + r0;
    M[&$M.system_config.data.rcv_stream_map_dac_l + $framesync_ind.FRAME_SIZE_FIELD] = r0;
    M[&$M.system_config.data.rcv_stream_map_dac_r + $framesync_ind.FRAME_SIZE_FIELD] = r0;
    M[&$M.system_config.data.snd_stream_map_refin  + $framesync_ind.FRAME_SIZE_FIELD] = r0;

    r0 = 60;
    NULL = r9 - 16000;
    if Z r0 = r0 + r0;
    M[&$M.system_config.data.snd_stream_map_adc  + $framesync_ind.FRAME_SIZE_FIELD] = r0;

    // Setup Rate Matching
    r0 = M[audio_rate_matching];
    r0 = r0 AND $SW_RATE_MATCH_ENABLE_MASK;
    M[$sw_ratematching_dac] = r0;
    M[$sw_ratematching_adc] = r0;

    // ---- Setup ADC -----
    // Setup ADC sampling Rate
    r6 = M[frame_adc_sampling_rate];
    r7 = M[adc_sampling_rate];
    r8 = &cvc_sndin_resampler_lookup;
    call IdentifyFilter;
    // r0 = filter descrptor or NULL (passthrough)
    push r0;
    r8 = &$adc_in.copy_op_l.param;
    call $iir_resamplev2.SetFilter;
    pop r0;
    
    push r0;
    r8 = &$adc_in.copy_op_r.param;
    call $iir_resamplev2.SetFilter;
    pop r0;

    r1 = M[frame_adc_sampling_rate];
    r8 = &$adc_in.sw_rate_op.param;
    call $cbops.rate_monitor_op.Initialize;

    r6 = M[frame_adc_sampling_rate];
    r2 = r6 * $ADCDAC_PERIOD_USEC (frac);
    r2 = r2 + 1;
    // Jitter for send in.   The smaller the jitter the more glitches but
    //  of smaller size, or fewer glitches of a larger magnitude.  Two interupt
    //  periods seem to work well.
    r2 = r2 + r2;
    r2 = r2 + r2;
    M[&$M.system_config.data.snd_stream_map_adc  + $framesync_ind.JITTER_FIELD] = r2;
    // clear frame counter to reset frame threshold calculation
    M[&$M.system_config.data.snd_stream_map_adc + $framesync_ind.COUNTER_FIELD] = NULL;
    // ---- Setup DAC -----
    //  MAX_ADVANCE_FIELD for DAC cBuffer and sidetone (8kHz or 16kHz time 625usec)
    //      NB:  DAC cBuffer & Sidetone = 8kHz
    //      FE:  DAC cBuffer & Sidetone = 16kHz
    //      WB:  DAC cBuffer & Sidetone = 16kHz
    r6 = M[frame_dac_sampling_rate];
    r2 = r6 * $ADCDAC_PERIOD_USEC (frac);
    r2 = r2 + 1;
  //  r2 = r2 + r2;
    M[$dac_out.sidetone_mix_op.param + $cbops.sidetone_mix_op.SIDETONE_MAX_SAMPLES_FIELD]=r2;
    M[$dac_out.insert_op_l.param + $cbops.insert_op.MAX_ADVANCE_FIELD]  = r2;
    M[$dac_out.insert_op_r.param + $cbops.insert_op.MAX_ADVANCE_FIELD]  = r2;
    M[&$M.system_config.data.rcv_stream_map_dac_l + $framesync_ind.JITTER_FIELD]   = r2;
    M[&$M.system_config.data.rcv_stream_map_dac_r + $framesync_ind.JITTER_FIELD]   = r2;
    //dbg
    r3 = r2*3 (int);
    r3 = r3 + r3;
    M[&$M.system_config.data.snd_stream_map_refin  + $framesync_ind.JITTER_FIELD]   = r3;
    // clear frame counter to reset frame threshold calculation
    M[$M.system_config.data.rcv_stream_map_dac_l + $framesync_ind.COUNTER_FIELD] = NULL;
    M[$M.system_config.data.rcv_stream_map_dac_r + $framesync_ind.COUNTER_FIELD] = NULL;

    r7 = M[&dac_sampling_rate];
    r8 = &cvc_rcvout_resampler_lookup;
    call IdentifyFilter;
    // r0 is resampler Descriptor or NULL (passthrough)
    // Set DAC port MAX_ADVANCE_FIELD (r2)
    r3 = r7 * $ADCDAC_PERIOD_USEC (frac);
    r3 = r3 + 1;
    NULL = r0;
    if NZ r2=r3;
    M[$dac_out.dac_wrap_op.param + $cbops.port_wrap_op.MAX_ADVANCE_FIELD] = r2;

    // Set rate for left sample rate monitor
    r1 = M[&dac_sampling_rate];
    r8 = &$dac_out.left_sw_rate_monitor_op.param;
    push r0;
    call $cbops.rate_monitor_op.Initialize;
    pop r0;
    
    // Set rate for right sample rate monitor
    r1 = M[&dac_sampling_rate];
    r8 = &$dac_out.right_sw_rate_monitor_op.param;
    push r0;
    call $cbops.rate_monitor_op.Initialize;
    pop r0;

    call $interrupt.unblock;

    M[&dac_param_loaded] = Null;

    jump $pop_rLink_and_rts;

$DAC_CheckConnectivity:
    $push_rLink_macro;
    r8 = 1;
    r0 = $DAC_PORT_R;
    call $cbuffer.is_it_enabled;
    if Z r8=NULL;
    M[$DAC_RightPortEnabled] = r8;

    jump $pop_rLink_and_rts;


// *****************************************************************************
// MODULE:
//    IdentifyFilter:
//
// DESCRIPTION:
//    Search resampler list for filter descriptor.
//    List is NULL terminated with the descriptors for targets of 8kHz and 16kHz
//
// INPUTS:
//    r8 = Search Table (cvc_rcvout_resampler_lookup or cvc_sndin_resampler_lookup)
//    r7 = Sample Rate
//    r6 = Target Sample Rate
//
// OUTPUTS:
//    r0 = Descriptor, or NULL
// *****************************************************************************
    .VAR cvc_rcvout_resampler_lookup[] =
        // Rate     ,     8kHz-->Rate,                          16kHz-->Rate
        32000,        $M.iir_resamplev2.Up_4_Down_1.filter,     $M.iir_resamplev2.Up_2_Down_1.filter,
        44100,        $M.iir_resamplev2.Up_441_Down_80.filter,  $M.iir_resamplev2.Up_441_Down_160.filter,
        48000,        $M.iir_resamplev2.Up_6_Down_1.filter,     $M.iir_resamplev2.Up_3_Down_1.filter,
        96000,        $M.iir_resamplev2.Up_12_Down_1.filter,    $M.iir_resamplev2.Up_6_Down_1.filter,
#if !defined(ADK35)
        192000,       $M.iir_resamplev2.Up_24_Down_1.filter,    $M.iir_resamplev2.Up_12_Down_1.filter,
#endif
        0;

    .VAR cvc_sndin_resampler_lookup[] =
        // Rate     ,     Rate-->8Kz,                            Rate-->16kHz
        32000,        $M.iir_resamplev2.Up_1_Down_4.filter,     $M.iir_resamplev2.Up_1_Down_2.filter,
        44100,        $M.iir_resamplev2.Up_80_Down_441.filter,  $M.iir_resamplev2.Up_160_Down_441_low_mips.filter,
        48000,        $M.iir_resamplev2.Up_1_Down_6.filter,     $M.iir_resamplev2.Up_1_Down_3.filter,
        96000,        $M.iir_resamplev2.Up_1_Down_12.filter,    $M.iir_resamplev2.Up_1_Down_6.filter,
        8000,         0,                                        $M.iir_resamplev2.Up_2_Down_1.filter,
        16000,        $M.iir_resamplev2.Up_1_Down_2.filter,     0,
        3200,        $M.iir_resamplev2.Up_1_Down_4.filter,     $M.iir_resamplev2.Up_1_Down_2.filter,
        4410,        $M.iir_resamplev2.Up_80_Down_441.filter,  $M.iir_resamplev2.Up_160_Down_441_low_mips.filter,
        4800,        $M.iir_resamplev2.Up_1_Down_6.filter,     $M.iir_resamplev2.Up_1_Down_3.filter,
        9600,        $M.iir_resamplev2.Up_1_Down_12.filter,    $M.iir_resamplev2.Up_1_Down_6.filter,
        800,         0,                                        $M.iir_resamplev2.Up_2_Down_1.filter,
        1600,        $M.iir_resamplev2.Up_1_Down_2.filter,     0,
        0;

IdentifyFilter:
    r6 = r6 - 15999;
    if NEG r6=NULL;
    // Target Sample Rate (r5)  : (0) 8kHz or (1) 16kHz
jp_identify:
    r0 = M[r8];
    if Z rts;
    r8 = r8 + 3;
 /*   Null = r0 - 16000;
    if LE jump jp_identify;*/
    NULL = r0 - r7;
    if NZ jump jp_identify;
    r8 = r8 - 2;
    r0 = M[r8 + r6];
    rts;
.ENDMODULE;
