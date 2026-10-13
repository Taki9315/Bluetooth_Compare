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


.CONST  $I2S_A_IN_PORT          (($cbuffer.READ_PORT_MASK  + $CHAN_A_24KHZ_IN_PORT_NUMBER) | $cbuffer.FORCE_PCM_AUDIO);
.CONST  $I2S_A_OUT_PORT         ($cbuffer.WRITE_PORT_MASK + $CHAN_A_24KHZ_OUT_PORT_NUMBER);

.CONST  $I2S_B_IN_PORT          (($cbuffer.READ_PORT_MASK  + $CHAN_B_24KHZ_IN_PORT_NUMBER) | $cbuffer.FORCE_PCM_AUDIO);
.CONST  $I2S_B_OUT_PORT         ($cbuffer.WRITE_PORT_MASK + $CHAN_B_24KHZ_OUT_PORT_NUMBER);

.CONST  $I2S_C_IN_PORT          (($cbuffer.READ_PORT_MASK  + $CHAN_C_24KHZ_IN_PORT_NUMBER) | $cbuffer.FORCE_PCM_AUDIO);
.CONST  $I2S_C_OUT_PORT         ($cbuffer.WRITE_PORT_MASK + $CHAN_C_24KHZ_OUT_PORT_NUMBER);

/*
 * ---------------------------------------------------------------------------
 *                                  Channel A
 * ---------------------------------------------------------------------------
 */

.MODULE $USB_A.out;
   .DATASEGMENT DM;
   // The scaling has been reduced to 3 from 4 to reduce the usage of data memory
   DeclareCBuffer(cbuffer_struc,mem,$BLOCK_SIZE * 3);

  .VAR/DM1CIRC    sr_hist[$cbops.rate_adjustment_and_shift.SRA_COEFFS_SIZE];

    .VAR copy_struc[] =
        $cbops.scratch.BufferTable,     // BUFFER_TABLE_FIELD
        &sw_copy_op,                    // MAIN_FIRST_OPERATOR_FIELD
        &$i2s_a_out.resample_op,        // MTU_FIRST_OPERATOR_FIELD
        1,                              // NUM_INPUTS_FIELD
        &cbuffer_struc,
        1,                              // NUM_OUTPUTS_FIELD
        $I2S_A_OUT_PORT,
        1,                              // NUM_INTERNAL_FIELD
        $cbops.scratch.cbuffer_struc2;

    .BLOCK sw_copy_op;
        .VAR sw_copy_op.mtu_next  = $cbops.NO_MORE_OPERATORS;
        .VAR sw_copy_op.main_next = &$i2s_a_out.resample_op;
        .VAR sw_copy_op.func = &$cbops.rate_adjustment_and_shift;
        .VAR sw_copy_op.param[$cbops.rate_adjustment_and_shift.STRUC_SIZE] =
            0,                          // INPUT1_START_INDEX_FIELD
            2,                          // OUTPUT1_START_INDEX_FIELD
            0,                          // SHIFT_AMOUNT_FIELD
            0,                          // MASTER_OP_FIELD
            &$sra_coeffs,               // FILTER_COEFFS_FIELD
            &sr_hist,                   // HIST1_BUF_FIELD
            &sr_hist,                   // HIST1_BUF_START_FIELD
            &$USB_A.in.sw_rate_op.param + $cbops.rate_monitor_op.INVERSE_WARP_VALUE_FIELD, // SRA_TARGET_RATE_ADDR_FIELD
            0,                          // ENABLE_COMPRESSOR_FIELD
            0 ...;
    .ENDBLOCK;
    
    .BLOCK $i2s_a_out.resample_op;
        .VAR $i2s_a_out.resample_op.mtu_next  = &sw_copy_op;
        .VAR $i2s_a_out.resample_op.main_next = $cbops.NO_MORE_OPERATORS;
        .VAR $i2s_a_out.resample_op.func =  $cbops_iir_resamplev2;
        .VAR $i2s_a_out.resample_op.param[$iir_resamplev2.OBJECT_SIZE] =
            2,    /*INPUT_INDEX_FIELD*/
            1,    /*OUTPUT_INDEX_FIELD*/
            0,                                  // FILTER_DEFINITION_PTR_FIELD  [---CONFIG---]
            -8,                                  // INPUT_SCALE_FIELD   (input Q15)
            0,                                  // OUTPUT_SCALE_FIELD  (output Q23)
            &$cbops.scratch.mem1,               // INTERMEDIATE_CBUF_PTR_FIELD
            LENGTH($cbops.scratch.mem1),        // INTERMEDIATE_CBUF_LEN_FIELD
            0 ...;
    .ENDBLOCK;

.ENDMODULE;




// far end receive input structures
.MODULE $USB_A.in;
    .DATASEGMENT DM;

    DeclareCBuffer(cbuffer_struc,mem_output,3*90);          // Big enough for 2 SCO packets

    .VAR/DM1CIRC    sr_hist[$cbops.rate_adjustment_and_shift.SRA_COEFFS_SIZE];


    .VAR $i2s_a_in.copy_struc[] =
        $cbops.scratch.BufferTable,     // BUFFER_TABLE_FIELD
        &$i2s_a_in.resample_op,         // MAIN_FIRST_OPERATOR_FIELD
        &sw_copy_op,                    // MTU_FIRST_OPERATOR_FIELD
        1,                              // NUM_INPUTS_FIELD
        $I2S_A_IN_PORT,
        1,                              // NUM_OUTPUTS_FIELD
        &cbuffer_struc,
        1,                              // NUM_INTERNAL_FIELD
        $cbops.scratch.cbuffer_struc2;

    .BLOCK $i2s_a_in.resample_op;
        .VAR $i2s_a_in.resample_op.mtu_next  = $cbops.NO_MORE_OPERATORS;
        .VAR $i2s_a_in.resample_op.main_next = &sw_rate_op;
        .VAR $i2s_a_in.resample_op.func =  $cbops_iir_resamplev2;
        .VAR $i2s_a_in.resample_op.param[$iir_resamplev2.OBJECT_SIZE] =
            0,    /*INPUT_INDEX_FIELD*/
            2,    /*OUTPUT_INDEX_FIELD*/
            0,                                  // FILTER_DEFINITION_PTR_FIELD  [---CONFIG---]
            0,                                  // INPUT_SCALE_FIELD   (input Q15)
            8,                                  // OUTPUT_SCALE_FIELD  (output Q23)
            &$cbops.scratch.mem1,               // INTERMEDIATE_CBUF_PTR_FIELD
            LENGTH($cbops.scratch.mem1),        // INTERMEDIATE_CBUF_LEN_FIELD
            0 ...;
    .ENDBLOCK;

    .BLOCK sw_rate_op;
        .VAR sw_rate_op.mtu_next  = &$i2s_a_in.resample_op;
        .VAR sw_rate_op.main_next = &sw_copy_op;
        .VAR sw_rate_op.func = &$cbops.rate_monitor_op;
        .VAR sw_rate_op.param[$cbops.rate_monitor_op.STRUC_SIZE] =
            2,                          // MONITOR_INDEX_FIELD
            1600,                       // PERIODS_PER_SECOND_FIELD
            10,                         // SECONDS_TRACKED_FIELD
            0,                          // TARGET_RATE_FIELD    [---CONFIG---]
            10,                         // ALPHA_LIMIT_FIELD (controls the size of the averaging window)
            0.5,                        // AVERAGE_IO_RATIO_FIELD - initialize to 1.0 in q.22
            11,                         // WARP_MSG_LIMIT_FIELD
            2400,                       // IDLE_PERIODS_AFTER_STALL_FIELD
            0 ...;
    .ENDBLOCK;

    .BLOCK sw_copy_op;
        .VAR sw_copy_op.mtu_next  = &sw_rate_op;
        .VAR sw_copy_op.main_next = $cbops.NO_MORE_OPERATORS;
        .VAR sw_copy_op.func = &$cbops.rate_adjustment_and_shift;
        .VAR sw_copy_op.param[$cbops.rate_adjustment_and_shift.STRUC_SIZE] =
            2,                          // INPUT1_START_INDEX_FIELD
            1,                          // OUTPUT1_START_INDEX_FIELD
            0,                          // SHIFT_AMOUNT_FIELD
            0,                          // MASTER_OP_FIELD
            &$sra_coeffs,               // FILTER_COEFFS_FIELD
            &sr_hist,                   // HIST1_BUF_FIELD
            &sr_hist,                   // HIST1_BUF_START_FIELD
            &sw_rate_op.param + $cbops.rate_monitor_op.WARP_VALUE_FIELD, // SRA_TARGET_RATE_ADDR_FIELD
            0,                          // ENABLE_COMPRESSOR_FIELD
            0 ...;
    .ENDBLOCK;

.ENDMODULE;



/*
 * ---------------------------------------------------------------------------
 *                                  Channel B
 * ---------------------------------------------------------------------------
 */

.MODULE $USB_B.out;
   .DATASEGMENT DM;
   // The scaling has been reduced to 3 from 4 to reduce the usage of data memory
   DeclareCBuffer(cbuffer_struc,mem,$BLOCK_SIZE * 3);

  .VAR/DM1CIRC    sr_hist[$cbops.rate_adjustment_and_shift.SRA_COEFFS_SIZE];

    .VAR copy_struc[] = // second
        $cbops.scratch.BufferTable,     // BUFFER_TABLE_FIELD
        &volume,                        // MAIN_FIRST_OPERATOR_FIELD
        &$i2s_b_out.resample_op,        // MTU_FIRST_OPERATOR_FIELD
        1,                              // NUM_INPUTS_FIELD
        &cbuffer_struc,
        2,                              // NUM_OUTPUTS_FIELD
        $I2S_B_OUT_PORT,
        &$USB_B.in.ref_struc,          // Reference Signal Output Buffer
        1,                              // NUM_INTERNAL_FIELD
        $cbops.scratch.cbuffer_struc2;

    .BLOCK volume;
        .VAR volume.mtu_next  = $cbops.NO_MORE_OPERATORS;
        .VAR volume.main_next = &sw_copy_op;
        .VAR volume.func = &$cbops.volume;
        .VAR volume.param[$cbops.volume.STRUC_SIZE] =
            0,                               // INPUT_START_INDEX_FIELD
            0,                               // OUTPUT_START_INDEX_FIELD
            1.0,                             // Final Gain Value
            0,                               // Current Gain Value
            0,                               // samples per step,
            -5,                              // step shift field
            0.01,                            // delta field
            0;                               // current step field
    .ENDBLOCK;

    .BLOCK sw_copy_op;
        .VAR sw_copy_op.mtu_next  = &volume;
        .VAR sw_copy_op.main_next = &$i2s_b_out.resample_op;
        .VAR sw_copy_op.func = &$cbops.rate_adjustment_and_shift;
        .VAR sw_copy_op.param[$cbops.rate_adjustment_and_shift.STRUC_SIZE] =
            0,                          // INPUT1_START_INDEX_FIELD
            2,                          // OUTPUT1_START_INDEX_FIELD
            0,                          // SHIFT_AMOUNT_FIELD
            0,                          // MASTER_OP_FIELD
            &$sra_coeffs,               // FILTER_COEFFS_FIELD
            &sr_hist,                   // HIST1_BUF_FIELD
            &sr_hist,                   // HIST1_BUF_START_FIELD
            &$USB_B.in.sw_rate_op.param + $cbops.rate_monitor_op.INVERSE_WARP_VALUE_FIELD, // SRA_TARGET_RATE_ADDR_FIELD
            0,                          // ENABLE_COMPRESSOR_FIELD
            0 ...;
    .ENDBLOCK;
    
    .BLOCK $i2s_b_out.resample_op;
        .VAR $i2s_b_out.resample_op.mtu_next  = &sw_copy_op;
        .VAR $i2s_b_out.resample_op.main_next = $cbops.NO_MORE_OPERATORS;
        .VAR $i2s_b_out.resample_op.func =  $cbops_iir_resamplev2;
        .VAR $i2s_b_out.resample_op.param[$iir_resamplev2.OBJECT_SIZE] =
            2,    /*INPUT_INDEX_FIELD*/
            1,    /*OUTPUT_INDEX_FIELD*/
            0,                                  // FILTER_DEFINITION_PTR_FIELD  [---CONFIG---]
            -8,                                  // INPUT_SCALE_FIELD   (input Q15)
            0,                                  // OUTPUT_SCALE_FIELD  (output Q23)
            &$cbops.scratch.mem1,               // INTERMEDIATE_CBUF_PTR_FIELD
            LENGTH($cbops.scratch.mem1),        // INTERMEDIATE_CBUF_LEN_FIELD
            0 ...;
    .ENDBLOCK;

.ENDMODULE;


// far end receive input structures
.MODULE $USB_B.in;
    .DATASEGMENT DM;

    DeclareCBuffer(cbuffer_struc,mem_output,3*90);          // Big enough for 2 SCO packets
    // Declare a circular buffer for Bluetooth reference signal
    DeclareCBuffer(ref_struc, mem, $BLOCK_SIZE * $BUFFER_SCALING);
    
    .VAR/DM1CIRC    sr_hist[$cbops.rate_adjustment_and_shift.SRA_COEFFS_SIZE];


    .VAR $i2s_b_in.copy_struc[] =
        $cbops.scratch.BufferTable,     // BUFFER_TABLE_FIELD
        &$i2s_b_in.resample_op,         // MAIN_FIRST_OPERATOR_FIELD
        &volume,                        // MTU_FIRST_OPERATOR_FIELD
        1,                              // NUM_INPUTS_FIELD
        $I2S_B_IN_PORT,
        1,                              // NUM_OUTPUTS_FIELD
        &cbuffer_struc,
        1,                              // NUM_INTERNAL_FIELD
        $cbops.scratch.cbuffer_struc2;

    .BLOCK $i2s_b_in.resample_op;
        .VAR $i2s_b_in.resample_op.mtu_next  = $cbops.NO_MORE_OPERATORS;
        .VAR $i2s_b_in.resample_op.main_next = &sw_rate_op;
        .VAR $i2s_b_in.resample_op.func =  $cbops_iir_resamplev2;
        .VAR $i2s_b_in.resample_op.param[$iir_resamplev2.OBJECT_SIZE] =
            0,    /*INPUT_INDEX_FIELD*/
            2,    /*OUTPUT_INDEX_FIELD*/
            0,                                  // FILTER_DEFINITION_PTR_FIELD  [---CONFIG---]
            0,                                  // INPUT_SCALE_FIELD   (input Q15)
            8,                                  // OUTPUT_SCALE_FIELD  (output Q23)
            &$cbops.scratch.mem1,               // INTERMEDIATE_CBUF_PTR_FIELD
            LENGTH($cbops.scratch.mem1),        // INTERMEDIATE_CBUF_LEN_FIELD
            0 ...;
    .ENDBLOCK;

    .BLOCK sw_rate_op;
        .VAR sw_rate_op.mtu_next  = &$i2s_b_in.resample_op;
        .VAR sw_rate_op.main_next = &sw_copy_op;
        .VAR sw_rate_op.func = &$cbops.rate_monitor_op;
        .VAR sw_rate_op.param[$cbops.rate_monitor_op.STRUC_SIZE] =
            2,                          // MONITOR_INDEX_FIELD
            1600,                       // PERIODS_PER_SECOND_FIELD
            10,                         // SECONDS_TRACKED_FIELD
            0,                          // TARGET_RATE_FIELD    [---CONFIG---]
            10,                         // ALPHA_LIMIT_FIELD (controls the size of the averaging window)
            0.5,                        // AVERAGE_IO_RATIO_FIELD - initialize to 1.0 in q.22
            11,                         // WARP_MSG_LIMIT_FIELD
            2400,                       // IDLE_PERIODS_AFTER_STALL_FIELD
            0 ...;
    .ENDBLOCK;

    .BLOCK sw_copy_op;
        .VAR sw_copy_op.mtu_next  = &sw_rate_op;
        .VAR sw_copy_op.main_next = &volume;
        .VAR sw_copy_op.func = &$cbops.rate_adjustment_and_shift;
        .VAR sw_copy_op.param[$cbops.rate_adjustment_and_shift.STRUC_SIZE] =
            2,                          // INPUT1_START_INDEX_FIELD
            1,                          // OUTPUT1_START_INDEX_FIELD
            0,                          // SHIFT_AMOUNT_FIELD
            0,                          // MASTER_OP_FIELD
            &$sra_coeffs,               // FILTER_COEFFS_FIELD
            &sr_hist,                   // HIST1_BUF_FIELD
            &sr_hist,                   // HIST1_BUF_START_FIELD
            &sw_rate_op.param + $cbops.rate_monitor_op.WARP_VALUE_FIELD, // SRA_TARGET_RATE_ADDR_FIELD
            0,                          // ENABLE_COMPRESSOR_FIELD
            0 ...;
    .ENDBLOCK;

    .BLOCK volume;
        .VAR volume.mtu_next  = &sw_copy_op;
        .VAR volume.main_next = $cbops.NO_MORE_OPERATORS;
        .VAR volume.func = &$cbops.volume;
        .VAR volume.param[$cbops.volume.STRUC_SIZE] =
            1,                               // INPUT_START_INDEX_FIELD
            1,                               // OUTPUT_START_INDEX_FIELD
            1.0,                             // Final Gain Value
            0,                               // Current Gain Value
            0,                               // samples per step,
            -5,                              // step shift field
            0.01,                            // delta field
            0;                               // current step field
    .ENDBLOCK;
.ENDMODULE;


/*
 * ---------------------------------------------------------------------------
 *                                  Channel C
 * ---------------------------------------------------------------------------
 */

.MODULE $USB_C.out;
   .DATASEGMENT DM;
   // The scaling has been reduced to 3 from 4 to reduce the usage of data memory
   DeclareCBuffer(cbuffer_struc,mem,$BLOCK_SIZE * 3);

  .VAR/DM1CIRC    sr_hist[$cbops.rate_adjustment_and_shift.SRA_COEFFS_SIZE];

    .VAR copy_struc[] =
        $cbops.scratch.BufferTable,     // BUFFER_TABLE_FIELD
        &sw_copy_op,                    // MAIN_FIRST_OPERATOR_FIELD
        &$i2s_c_out.resample_op,        // MTU_FIRST_OPERATOR_FIELD
        1,                              // NUM_INPUTS_FIELD
        &cbuffer_struc,
        1,                              // NUM_OUTPUTS_FIELD
        $I2S_C_OUT_PORT,
        1,                              // NUM_INTERNAL_FIELD
        $cbops.scratch.cbuffer_struc2;

    .BLOCK sw_copy_op;
        .VAR sw_copy_op.mtu_next  = $cbops.NO_MORE_OPERATORS;
        .VAR sw_copy_op.main_next = &$i2s_c_out.resample_op;
        .VAR sw_copy_op.func = &$cbops.rate_adjustment_and_shift;
        .VAR sw_copy_op.param[$cbops.rate_adjustment_and_shift.STRUC_SIZE] =
            0,                          // INPUT1_START_INDEX_FIELD
            2,                          // OUTPUT1_START_INDEX_FIELD
            0,                          // SHIFT_AMOUNT_FIELD
            0,                          // MASTER_OP_FIELD
            &$sra_coeffs,               // FILTER_COEFFS_FIELD
            &sr_hist,                   // HIST1_BUF_FIELD
            &sr_hist,                   // HIST1_BUF_START_FIELD
            &$USB_C.in.sw_rate_op.param + $cbops.rate_monitor_op.INVERSE_WARP_VALUE_FIELD, // SRA_TARGET_RATE_ADDR_FIELD
            0,                          // ENABLE_COMPRESSOR_FIELD
            0 ...;
    .ENDBLOCK;
    
    .BLOCK $i2s_c_out.resample_op;
        .VAR $i2s_c_out.resample_op.mtu_next  = &sw_copy_op;
        .VAR $i2s_c_out.resample_op.main_next = $cbops.NO_MORE_OPERATORS;
        .VAR $i2s_c_out.resample_op.func =  $cbops_iir_resamplev2;
        .VAR $i2s_c_out.resample_op.param[$iir_resamplev2.OBJECT_SIZE] =
            2,    /*INPUT_INDEX_FIELD*/
            1,    /*OUTPUT_INDEX_FIELD*/
            0,                                  // FILTER_DEFINITION_PTR_FIELD  [---CONFIG---]
            -8,                                  // INPUT_SCALE_FIELD   (input Q15)
            0,                                  // OUTPUT_SCALE_FIELD  (output Q23)
            &$cbops.scratch.mem1,               // INTERMEDIATE_CBUF_PTR_FIELD
            LENGTH($cbops.scratch.mem1),        // INTERMEDIATE_CBUF_LEN_FIELD
            0 ...;
    .ENDBLOCK;

.ENDMODULE;


// far end receive input structures
.MODULE $USB_C.in;
    .DATASEGMENT DM;

    DeclareCBuffer(cbuffer_struc,mem_output,3*90);          // Big enough for 2 SCO packets

    .VAR/DM1CIRC    sr_hist[$cbops.rate_adjustment_and_shift.SRA_COEFFS_SIZE];


    .VAR $i2s_c_in.copy_struc[] =
        $cbops.scratch.BufferTable,     // BUFFER_TABLE_FIELD
        &$i2s_c_in.resample_op,         // MAIN_FIRST_OPERATOR_FIELD
        &sw_copy_op,                        // MTU_FIRST_OPERATOR_FIELD
        1,                              // NUM_INPUTS_FIELD
        $I2S_C_IN_PORT,
        1,                              // NUM_OUTPUTS_FIELD
        &cbuffer_struc,
        1,                              // NUM_INTERNAL_FIELD
        $cbops.scratch.cbuffer_struc2;

    .BLOCK $i2s_c_in.resample_op;
        .VAR $i2s_c_in.resample_op.mtu_next  = $cbops.NO_MORE_OPERATORS;
        .VAR $i2s_c_in.resample_op.main_next = &sw_rate_op;
        .VAR $i2s_c_in.resample_op.func =  $cbops_iir_resamplev2;
        .VAR $i2s_c_in.resample_op.param[$iir_resamplev2.OBJECT_SIZE] =
            0,    /*INPUT_INDEX_FIELD*/
            2,    /*OUTPUT_INDEX_FIELD*/
            0,                                  // FILTER_DEFINITION_PTR_FIELD  [---CONFIG---]
            0,                                  // INPUT_SCALE_FIELD   (input Q15)
            8,                                  // OUTPUT_SCALE_FIELD  (output Q23)
            &$cbops.scratch.mem1,               // INTERMEDIATE_CBUF_PTR_FIELD
            LENGTH($cbops.scratch.mem1),        // INTERMEDIATE_CBUF_LEN_FIELD
            0 ...;
    .ENDBLOCK;

    .BLOCK sw_rate_op;
        .VAR sw_rate_op.mtu_next  = &$i2s_c_in.resample_op;
        .VAR sw_rate_op.main_next = &sw_copy_op;
        .VAR sw_rate_op.func = &$cbops.rate_monitor_op;
        .VAR sw_rate_op.param[$cbops.rate_monitor_op.STRUC_SIZE] =
            2,                          // MONITOR_INDEX_FIELD
            1600,                       // PERIODS_PER_SECOND_FIELD
            10,                         // SECONDS_TRACKED_FIELD
            0,                          // TARGET_RATE_FIELD    [---CONFIG---]
            10,                         // ALPHA_LIMIT_FIELD (controls the size of the averaging window)
            0.5,                        // AVERAGE_IO_RATIO_FIELD - initialize to 1.0 in q.22
            11,                         // WARP_MSG_LIMIT_FIELD
            2400,                       // IDLE_PERIODS_AFTER_STALL_FIELD
            0 ...;
    .ENDBLOCK;

    .BLOCK sw_copy_op;
        .VAR sw_copy_op.mtu_next  = &sw_rate_op;
        .VAR sw_copy_op.main_next = $cbops.NO_MORE_OPERATORS;
        .VAR sw_copy_op.func = &$cbops.rate_adjustment_and_shift;
        .VAR sw_copy_op.param[$cbops.rate_adjustment_and_shift.STRUC_SIZE] =
            2,                          // INPUT1_START_INDEX_FIELD
            1,                          // OUTPUT1_START_INDEX_FIELD
            0,                          // SHIFT_AMOUNT_FIELD
            0,                          // MASTER_OP_FIELD
            &$sra_coeffs,               // FILTER_COEFFS_FIELD
            &sr_hist,                   // HIST1_BUF_FIELD
            &sr_hist,                   // HIST1_BUF_START_FIELD
            &sw_rate_op.param + $cbops.rate_monitor_op.WARP_VALUE_FIELD, // SRA_TARGET_RATE_ADDR_FIELD
            0,                          // ENABLE_COMPRESSOR_FIELD
            0 ...;
    .ENDBLOCK;

.ENDMODULE;


.MODULE $M.i2s;
   .CODESEGMENT PM;
   .DATASEGMENT DM;

   .VAR  $encoding_loaded = 1;

$usb.configure:
nop;
   M[&$usb_first_init] = Null;

   //Configure USB
   push rLink;

   call $interrupt.block;

    // for USB mode, set jitter to 2ms (@16ms),
    r2 = (16*2);
    M[&$M.system_config.data.rcv_stream_map_i2s_a_in + $framesync_ind.JITTER_FIELD] = r2;
    M[&$M.system_config.data.rcv_stream_map_i2s_b_in + $framesync_ind.JITTER_FIELD] = r2;
    M[&$M.system_config.data.rcv_stream_map_i2s_c_in + $framesync_ind.JITTER_FIELD] = r2;

    r0 = 8;

    // Set USB Rate (number of words per 1msec period * 1000)
    push r0;    
    r1 = r0 * 1000 (int);
    r8 = &$USB_A.in.sw_rate_op.param;
    call $cbops.rate_monitor_op.Initialize;
    pop r0;

    push r0;
    r1 = r0 * 1000 (int);
    r8 = &$USB_B.in.sw_rate_op.param;
    call $cbops.rate_monitor_op.Initialize;
    pop r0;

    push r0;
    r1 = r0 * 1000 (int);
    r8 = &$USB_C.in.sw_rate_op.param;
    call $cbops.rate_monitor_op.Initialize;
    pop r0;

    call $interrupt.unblock;

    M[&$encoding_loaded] = Null;

    jump $pop_rLink_and_rts;


.ENDMODULE;
