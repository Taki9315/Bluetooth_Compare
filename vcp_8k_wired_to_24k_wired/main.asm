// *****************************************************************************
// %%fullcopyright(2007)        http://www.csr.com
// %%version
//
// $Change: 1894100 $  $DateTime: 2014/05/09 10:06:38 $
// *****************************************************************************


// *****************************************************************************
// DESCRIPTION
//    This is a passthrough application. Compile time flags should be
//    specified within the project's properties under "Extra defines".
//
//    Here is a list of defines that can be specified:
//
//    The application expects CVSD on the SCO input and places CVSD on
//    the SCO output.  The app runs at 8 kHz in this configuration.
//
//
//   OTHER NOTES:
//
//    The ADC/DAC gain levels are set in the plugin (csr_common_example_plugin)
//
//    To enable/disable PLC, modify the following variable :
//      M[$sco_in.decoder_obj + $sco_pkt_handler.ENABLE_FIELD]
//      (0 to disable, 0x2000 to enable)
//
// *****************************************************************************


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

#define $TONE_EXTRA_BOOST_BITS        2
#define $PROMPT_EXTRA_BOOST_BITS     (-1)
#define $PCM_END_DETECTION_TIME_OUT   40

//use fram sync instead of cbops
//#define USE_FRAMESYNC

   // System defines
   // --------------

   .CONST $WARP_PARAMS_FROM_VM_MESSAGE_ID       0x1026;

   .CONST $MESSAGE.AUDIO_CONFIG                 0x2000;
   .CONST $FAR_END.AUDIO.SCO_CVSD               0;
   .CONST $FAR_END.AUDIO.SCO_SBC                2;
   .CONST $FAR_END.AUDIO.USB_PCM                3;

   // 1ms is chosen as the interrupt rate for the audio input/output to minimize latency.
   .CONST $TIMER_PERIOD_AUDIO_COPY_MICROS       625;
   .CONST $ADCDAC_PERIOD_USEC                  0.000625;

.MODULE $cbops.scratch;
   .DATASEGMENT DM;

    .VAR BufferTable[10*$cbops_multirate.BufferTable.ENTRY_SIZE];
    // scratch 1 for resampler temp stage
    DeclareCBuffer(cbuffer_struc1,mem1,240);
    // scratch 2,3,4,5 for changing consumed/produced
    DeclareCBuffer(cbuffer_struc2,mem2,240);
    DeclareCBuffer(cbuffer_struc3,mem3,240);
    DeclareCBuffer(cbuffer_struc4,mem4,240);
   // DeclareCBuffer(cbuffer_struc5,mem5,120);

.ENDMODULE;

// VM Messages
#define VM_ADCDAC_RATE_MESSAGE_ID               0x1070
#define VM_SET_TONE_RATE_MESSAGE_ID             0x1072
#define UNSUPPORTED_SAMPLING_RATES_MSG          0x1090
#define VM_SET_LEVELS_MESSAGE_ID                0x2001

// Rate match enable masks
.CONST $RATE_MATCH_DISABLE_MASK              0x0000;
.CONST $HW_RATE_MATCH_ENABLE_MASK            0x0001;
.CONST $SW_RATE_MATCH_ENABLE_MASK            0x0002;
.CONST $SW_RATE_MATCH_MASK                   0x0003;

// Mask for interface mode
.CONST $AUDIO_IF_MASK                        (0x00ff);       // Mask to select the audio i/f info


// NOTE: When using macros ensure that no characters
// or white space follow the '\' line continuation token
// This will cause the assembler error:
//    FATAL:    Illegal character '\'




/******************************************************************************/
     // Scheduleing Task.   COUNT_FIELD counts modulous 12 (0-11) in 625 usec intervals

     // SCO Send on COUNT_FIELD=0, SCO Xmit on COUNT_FIELD=3,
     // SCO Good Receive on COUNT_FIELD=2, with Posibility of Re-Xmits
     // Need to accept 1 packet of latency due to re-Xmit
     //     The rcv_in jitter needs to be one sco packet

     // Send processing should be scheduled based on the peaks SEND_MIPS_FIELD (0-8000).
     //     Trigger equals 11 - round[11*(SEND_MIPS_FIELD/8000)]

    #define SND_PROCESS_TRIGGER     3

.MODULE $M.App.scheduler;
   .DATASEGMENT DM;

   // ** Memory Allocation for CVC config structure
    .VAR    tasks[]=
        0,                      //    COUNT_FIELD
        12,                     //    MAX_COUNT_FIELD
        (Length(tasks) - $FRM_SCHEDULER.TASKS_FIELD)/2, // NUM_TASKS_FIELD
        0,                      //    TOTAL_MIPS_FIELD
        0,                      //    SEND_MIPS_FIELD
        0,                      //    TOTALTM_FIELD
        0,                      //    TOTALSND_FIELD
        0,                      //    TIMER_FIELD
        0,                      //    TRIGGER_FIELD
        // Task List (highest to lowest priority) - Limit 23 tasks (Modulous 12)
        $main_send,              SND_PROCESS_TRIGGER,
        $main_receive,           0,
        $main_housekeeping,      1;


.ENDMODULE;



// *****************************************************************************
// MODULE:
//    $M.Main
//
// DESCRIPTION:
//    This is the main routine.  It initializes the libraries and then loops
//    "forever" in the processing loop.
//
// *****************************************************************************
.MODULE $M.main;
   .CODESEGMENT PM;
   .DATASEGMENT DM;

   // For routing config message
   .VAR  set_port_type_message_struc[$message.STRUC_SIZE];

   // These scratch "registers" are used by various libraries (e.g. SBC)
   .VAR $scratch.s0;
   .VAR $scratch.s1;
   .VAR $scratch.s2;

   // ** allocate memory for timer structures **
   .VAR $audio_copy_timer_struc[$timer.STRUC_SIZE];
   .VAR  set_adcdac_rate_from_vm_message_struc[$message.STRUC_SIZE];
   .VAR  set_tone_rate_from_vm_message_struc[$message.STRUC_SIZE]; // Message structure for VM_SET_TONE_RATE_MESSAGE_ID message
   .VAR  set_levels_from_vm_message_struc[$message.STRUC_SIZE];

$main:

   // initialise the stack library
   call $stack.initialise;
   // initialise the interrupt library
   call $interrupt.initialise;
   // initialise the message library
   call $message.initialise;
   // initialise the cbuffer library
   call $cbuffer.initialise;
   // initialise the pskey library
   call $pskey.initialise;

#ifdef PROFILER_ON
   // initialise the profiler library
   call $profiler.initialise;
#endif

   // Set up message handler for VM_ADCDAC_RATE_MESSAGE_ID message
   r1 = &set_adcdac_rate_from_vm_message_struc;
   r2 = VM_ADCDAC_RATE_MESSAGE_ID;
   r3 = &$set_front_end_from_vm;
   call $message.register_handler;

    // set up message handler for VM_SET_TONE_RATE_MESSAGE_ID message
   r1 = &set_tone_rate_from_vm_message_struc;
   r2 = VM_SET_TONE_RATE_MESSAGE_ID;
   r3 = &$set_tone_rate_from_vm;
   call $message.register_handler;

   // set up message handler for VM_SET_LEVELS_MESSAGE_ID message
   r1 = &set_levels_from_vm_message_struc;
   r2 = VM_SET_LEVELS_MESSAGE_ID;
   r3 = &$set_levels_from_vm;
   call $message.register_handler;

   // set up message handler for SCO routing message
   r1 = &set_port_type_message_struc;
   r2 = $MESSAGE.AUDIO_CONFIG;
   r3 = &$set_backend_from_vm;
   call $message.register_handler;

   // Initialize Synchronized Timing for SCO   (Requires wall clock)
   call $wall_clock.initialise;
   r8 = $sco_data.object;
   call $sco_timing.initialize;

   // initilize VCP messages handler
   call $VCP.message_handler.initialize;


   // send message saying we're up and running!
   r2 = $MESSAGE_KALIMBA_READY;
   call $message.send_short;

   // Power Up Reset needs to be called once during program execution
   call $one_mic_example.power_up_reset;



   // Setup I2S sample rate converters
   r8 = &$i2s_a_in.resample_op.param;
   r0 = $M.iir_resamplev2.Up_1_Down_3.filter;
   call $iir_resamplev2.SetFilter;

   r8 = &$i2s_a_out.resample_op.param;
   r0 = $M.iir_resamplev2.Up_3_Down_1.filter;
   call $iir_resamplev2.SetFilter;

   r8 = &$i2s_b_in.resample_op.param;
   r0 = $M.iir_resamplev2.Up_1_Down_3.filter;
   call $iir_resamplev2.SetFilter;

   r8 = &$i2s_b_out.resample_op.param;
   r0 = $M.iir_resamplev2.Up_3_Down_1.filter;
   call $iir_resamplev2.SetFilter;

   r8 = &$i2s_c_in.resample_op.param;
   r0 = $M.iir_resamplev2.Up_1_Down_3.filter;
   call $iir_resamplev2.SetFilter;

   r8 = &$i2s_c_out.resample_op.param;
   r0 = $M.iir_resamplev2.Up_3_Down_1.filter;
   call $iir_resamplev2.SetFilter;


   // start timer that copies audio samples
   r1 = &$audio_copy_timer_struc;
   r2 = $TIMER_PERIOD_AUDIO_COPY_MICROS;
   r3 = &$audio_copy_handler;
   call $timer.schedule_event_in;

   //preparing VCP for strarting
   call $VCP.startup;

   Null = M[&$usb_first_init];
   if Z jump usb.already.inited;
    nop;
wait.config:
   Null = M[&$encoding_loaded];
   if Z jump wait.config;

   call $usb.configure;
usb.already.inited:


   Null =  M[&$dac_first_init];
   if Z jump dac.already.inited;
   nop;
wait.configDAC:
   Null = M[&$M.analog.dac_param_loaded];
   if Z jump wait.configDAC;

   //configure DAC output
   call $ConfigureDACOutput;

dac.already.inited:

   // start timer that copies audio samples
   r1 = &$wbs_timer_struc;
   r2 = 300;
   r3 = &$wbs_copy_handler;
   call $timer.schedule_event_in;

//needed to start logger from 1st frame
    call $logger_begin;
    call $logger_begin;
    call $logger_begin;
    call $logger_begin;
    call $logger_begin;

main_loop:
      // go to slower clock
       r0 = $frame_sync.MAX_CLK_DIV_RATE;
       r2 = M[$UDB_frac];
       r1 = M[$UDB_curr_frac];
       Null = r2 - r1;
       if NZ r0 = 0;

       r1 = M[&$mips_c];
       Null = r1 - 0.75;
       if POS r0 = 0;

       M[$CLOCK_DIVIDE_RATE] = r0;
       
       r8 = 1;
       r0 = $SCO_IN_PORT;
       call $cbuffer.is_it_enabled;
       if Z jump route_wired_stream;
      //call VCP proc       
       r7 = &$MIC_Blue_struc;
       r8 = &$SPK_Blue_struc;
       r9 = &$REF_Blue_struc;
       call $VCP.frame_process;
       jump main_loop;
    route_wired_stream:
       r7 = &$MIC_struc;
       r8 = &$SPK_struc;
       r9 = &$REF_struc;
       call $VCP.frame_process;

    jump main_loop;


     // ******* Houskeeping Event ********************
$main_housekeeping:
     $push_rLink_macro;
     // General system processing each frame period

     jump $pop_rLink_and_rts;

     // ********  Receive Processing ************
$main_receive:
    $push_rLink_macro;

    // Check for Initialization
    NULL = M[$one_mic_example.reinit];
    if NZ call $one_mic_example_reinitialize;

    // Run PLC and Decoder
    r7 = &$sco_data.object;
    call $frame_sync.sco_decode;

    // Get Current System Mode
    r0 = M[$one_mic_example.sys_mode];
    // Call processing table that corresponds to the current mode
    r4 = M[$M.system_config.data.receive_mode_table + r0];
    call $frame_sync.run_function_table;

    jump $pop_rLink_and_rts;

      // ***************  Send Processing **********************
$main_send:
    $push_rLink_macro;

    // SP. Track time in process
    r0 = M[$TIMER_TIME];
    push r0;

    M[$ARITHMETIC_MODE] = NULL;

    // Check for Initialization
    NULL = M[$one_mic_example.reinit];
    if NZ call $one_mic_example_reinitialize;

    // Get Current System Mode
    r0 = M[$one_mic_example.sys_mode];
    // Call processing table that corresponds to the current mode
    r4 = M[$M.system_config.data.send_mode_table + r0];
    call $frame_sync.run_function_table;

    // SP.  Track Time in Process
    pop r0;
    r0 = r0 - M[$TIMER_TIME];
    r1 = M[$M.App.scheduler.tasks + $FRM_SCHEDULER.TOTALSND_FIELD];
    r1 = r1 - r0;
    M[$M.App.scheduler.tasks + $FRM_SCHEDULER.TOTALSND_FIELD]=r1;

    jump $pop_rLink_and_rts;

.ENDMODULE;



// *****************************************************************************
// MODULE:
//    $M.audio_copy_handler
//
// DESCRIPTION:
//    This routine copies data between firmware MMU buffers and dsp processing
//    buffers.
//
// *****************************************************************************
.MODULE $M.audio_copy_handler;
   .CODESEGMENT PM;
   .DATASEGMENT DM;


 $audio_copy_handler:

   // push rLink onto stack
   $push_rLink_macro;

    Null = M[&$encoding_loaded];
    if NZ call $usb.configure;

    Null = M[&$M.analog.dac_param_loaded];
    if NZ call $ConfigureDACOutput;

   // Call operators
   r8 = &$adc_in.copy_struc;
   call $cbops_multirate.copy;

   // Check if DAC Right Channel Connected
   r8 = 1;
   r0 = $DAC_PORT_R;
   call $cbuffer.is_it_enabled;
   if Z r8=NULL;
   M[$DAC_RightPortEnabled]=r8;

   r8 = &$dac_out.copy_struc;
   call $cbops_multirate.copy;

   r0 = M[&$tone_in.cbuffer_struc + $cbuffer.WRITE_ADDR_FIELD];
   M[&$M.detect_end_of_aux_stream.last_write_address] = r0;

   r8 = &$tone_in.copy_struc;
   call $cbops_multirate.copy;

    // detect end of tone/prompt stream
   call $detect_end_of_aux_stream;

   //
   // I2S A channel
   r8 = &$i2s_a_in.copy_struc;
   call $cbops_multirate.copy;

   // Empty the $far_end.in.output USB buffer if stalled
   r0 = $USB_A.in.cbuffer_struc;
   r1 = M[$USB_A.in.sw_rate_op.param + $cbops.rate_monitor_op.STALL_FIELD];
   null = r1 - $cbops.rate_monitor_op.NO_DATA_PERIODS_FOR_STALL;
   if POS call $cbuffer.empty_buffer;  // Input r0: cbuffer to prime

   r8 = &$USB_A.out.copy_struc;
   call $cbops_multirate.copy;

   //
   // I2S B channel
   r8 = &$i2s_b_in.copy_struc;
   call $cbops_multirate.copy;

   // Empty the $far_end.in.output USB buffer if stalled
   r0 = $USB_B.in.cbuffer_struc;
   r1 = M[$USB_B.in.sw_rate_op.param + $cbops.rate_monitor_op.STALL_FIELD];
   null = r1 - $cbops.rate_monitor_op.NO_DATA_PERIODS_FOR_STALL;
   if POS call $cbuffer.empty_buffer;  // Input r0: cbuffer to prime

   r8 = &$USB_B.out.copy_struc;
   call $cbops_multirate.copy;

   //
   // I2S C channel
   r8 = &$i2s_c_in.copy_struc;
   call $cbops_multirate.copy;

   // Empty the $far_end.in.output USB buffer if stalled
   r0 = $USB_C.in.cbuffer_struc;
   r1 = M[$USB_C.in.sw_rate_op.param + $cbops.rate_monitor_op.STALL_FIELD];
   null = r1 - $cbops.rate_monitor_op.NO_DATA_PERIODS_FOR_STALL;
   if POS call $cbuffer.empty_buffer;  // Input r0: cbuffer to prime

   r8 = &$USB_C.out.copy_struc;
   call $cbops_multirate.copy;

   // SP - SCO synchronization may reset frame counter
   r7 = &$M.App.scheduler.tasks + $FRM_SCHEDULER.COUNT_FIELD;
   r1 = &$audio_copy_timer_struc;
   r3 = &$audio_copy_handler;
   call $sco_timing.SyncClock;

   r8 = &$M.App.scheduler.tasks;
   call $frame_sync.task_scheduler_isr;

   jump $pop_rLink_and_rts;



// *****************************************************************************
// MODULE:
//    $audio_config_handler
//
// DESCRIPTION:
//    Message handler for receiving SCO/USB select from VM
//
// INPUTS:
//    r1 = encoding_mode
//    r2 = encoding_config
//
// OUTPUTS:
//    none
// *****************************************************************************

$set_backend_from_vm:
   // r4 == 1 means VM requests channel-B rate matcher reset on SCO
   // reconnect. Boot sends r4 == 0 (legacy no-op).
   Null = r4 - 1;
   if NZ rts;

   $push_rLink_macro;

   call $interrupt.block;

   // Reset only the 8 kHz rate monitor on the BT (channel B) path. Do not
   // call sco_timing.initialize or reinitialize_table here; those reset live
   // SCO frame sync and stall audio on reconnect.
   r0 = 8;
   r1 = r0 * 1000 (int);
   r8 = &$USB_B.in.sw_rate_op.param;
   call $cbops.rate_monitor_op.Initialize;

   call $interrupt.unblock;
   jump $pop_rLink_and_rts;

 //----------------------------------------------------------------------------
   .var $usb_first_init = 1;


// *****************************************************************************
// MODULE:
//    $set_front_end_from_vm
//
// DESCRIPTION:
//    Message handler for receiving ADCDAC rate from VM
//
// INPUTS:
//    r1 = adc/dac sampling rate/10 (e.g. 44100Hz is given by r1=4410)
//    r2 = rate match enable mask (bit1==1 enables S/W RM, bit0==1 enables H/W RM
//         (e.g.S/W RM r2=0x0002, H/W RM r2=0x0001, no RM r2=0x0000)
//    r3 = interface mode (0:ADC+DAC, 1:I2S, etc)
//    r4 = adc sampling rate for ADK 4.0
//
// OUTPUTS:
//    none
// *****************************************************************************
   // Flag to show interface type (analogue, I2S etc)


$set_front_end_from_vm:
   // We do not support changing these parameters, so we just ignore this
   // message.
   rts;


// *****************************************************************************
// MODULE:
//    $set_levels_from_vm
//
// DESCRIPTION:
//    Message handler for receiving SET_LEVELS message from VM
//
// INPUTS:
//    r1 = channel A sidetone level (0-7)
//    r2 = channel B (BT) mic level (0x0000-0x7FFF)
//    r3 = channel B (BT) speaker level (0x0000-0x7FFF)
//
// OUTPUTS:
//    none
// *****************************************************************************
$set_levels_from_vm:

    r1 = r1 AND 0x07;
    r1 = r1 LSHIFT 20;
    M[&$sidetone_gain] = r1;

    r2 = r2 LSHIFT 8;
    M[&$USB_B.out.volume.param + $cbops.volume.FINAL_VALUE_FIELD] = r2;

    r3 = r3 LSHIFT 8;
    M[&$USB_B.in.volume.param + $cbops.volume.FINAL_VALUE_FIELD] = r3;

    rts;

.ENDMODULE;

.MODULE $M.wbs_copy_handler;
   .CODESEGMENT PM_RAM;
   .DATASEGMENT DM;

.VAR $wbs_timer_struc[$timer.STRUC_SIZE];

$wbs_copy_handler:

   // push rLink onto stack
   $push_rLink_macro;

   //save clock
   r0 = M[$CLOCK_DIVIDE_RATE];
   push r0;

   //fast clock
   M[$CLOCK_DIVIDE_RATE] = NULL;

   // Call operators
    r8 = &$M.App.scheduler.tasks;
    Null = M[r8 + $FRM_SCHEDULER.TRIGGER_FIELD];
    if NZ call $frame_sync.task_scheduler_run;


   // start timer that copies audio samples
   r1 = &$wbs_timer_struc;
   r2 = 300;
   r3 = &$wbs_copy_handler;
   call $timer.schedule_event_in;

   //restore clock
   pop r0;
   M[$CLOCK_DIVIDE_RATE] = r0;

   // pop rLink from stack
   jump $pop_rLink_and_rts;

.ENDMODULE;

// *****************************************************************************
// MODULE:
//    $detect_end_of_aux_stream
//
// DESCRIPTION:
//    detects end of pcm tone/prompts and notifies vm
// *****************************************************************************

.MODULE $M.detect_end_of_aux_stream;
   .CODESEGMENT PM;
   .DATASEGMENT DM;

   .VAR last_write_address = &$tone_in.mem;
   .VAR write_move_counter = 0;

   $detect_end_of_aux_stream:

   $push_rLink_macro;

   // detect end of tone/prompt auxiliary pcm input stream
   r3 = M[$tone_in.aux_input_stream_available];
   if Z jump $pop_rLink_and_rts;
 /*  if NZ jump no_tone_data;
        r0 = 1;
        M[$tone_in.aux_input_stream_available] = r0;
    no_tone_data:*/

   // see if the input is active
   r0 = &$tone_in.cbuffer_struc;
   call $cbuffer.calc_amount_data;

   // check if input activity has been seen before
   Null = r3 AND 0x2;
   if NZ jump input_has_received;

   // input hasn't started yet, so no end check
   Null = r0;
   if Z jump $pop_rLink_and_rts;

   // input just received
   r3 = r3 OR 0x2;
   M[&$tone_in.aux_input_stream_available] = r3;
   jump $pop_rLink_and_rts;

   input_has_received:

   // Get the write address for the output buffer
   r0 = &$tone_in.cbuffer_struc;
   call $cbuffer.get_write_address_and_size;
   r4 = M[last_write_address];
   r3 = M[write_move_counter];
   r5 = 0;

   Null = r0 - r4;
   if Z r5 = r3 + 1;
   M[write_move_counter] = r5;

   Null = r5 - $PCM_END_DETECTION_TIME_OUT;
   if NEG jump $pop_rLink_and_rts;

   // inactive more than a threshold
   // notify VM about end of play back of aux input
   r2 = PLAY_BACK_FINISHED_MSG;
   r3 = 0;
   r4 = 0;
   r5 = 0;
   r6 = 0;
   call $message.send_short;
   M[$tone_in.aux_input_stream_available] = 0;
   M[write_move_counter] = 0;

   // Purge tone buffers
   r0 = M[&$tone_in.cbuffer_struc + $cbuffer.WRITE_ADDR_FIELD];
   M[&$tone_in.cbuffer_struc + $cbuffer.READ_ADDR_FIELD] = r0;

   // pop rLink from stack
   jump $pop_rLink_and_rts;

.ENDMODULE;

.MODULE $M.set_tone_rate_from_vm;
   .CODESEGMENT PM;
   .DATASEGMENT DM;
// *****************************************************************************
// MODULE:
//    $set_tone_rate_from_vm
//
// DESCRIPTION: message handler for receiving tone rate from VM
//
// INPUTS:
//  r1 = (tone rate)
//  r2 = bit 0: mono / stereo
//       bit 1: tone / prompt
//  r3 = Not used
//  r4 = Not used
// OUTPUTS:
//    none
// *****************************************************************************
   $set_tone_rate_from_vm:

   $push_rLink_macro;

   // extract  mono/stereo flag, this application
   // coverts streo voice prompts to mono
 /*  r0 = r2 AND 1;
   M[$tone_in.deinterleave_mix_op.param + $cbops.deinterleave_mix.INPUT_INTERLEAVED_FIELD] = r0;*/

   // firmware tones are boosted by 3 bits, voice promts expected to be normalised
   r0 = 8 + $PROMPT_EXTRA_BOOST_BITS;
   r3 = 8 + $TONE_EXTRA_BOOST_BITS;
   Null = r2 AND 0x2;
   if Z r0 = r3;
   M[$tone_in.copy_op.param + $iir_resamplev2.OUTPUT_SCALE_FIELD] = r0;

   // extract tone rate
   r1 = r1 AND 0xFFFF;
 //  M[current_tone_sampling_rate] = r1;

   // look up table to search
   // for matching entry
   r2 = $M.analog.cvc_sndin_resampler_lookup;
   r3 = r2;
   r4 = 0x7FFFFF;
   search_tone_rate_loop:
      // r3 = best match so far
      // r4 = best distance so far
      // r1 = tone sample rate

      // read next entry sample rate
      r0 = M[r2 + 0];
      // if no more entry, exit the loop
      if Z jump seach_done;

      // if exact match found exit
      r0 = r0 - r1;
      if Z jump exact_tone_found;

      // else look for nearest one
      if NEG r0 = -r0;
      Null = r4 - r0;
      if NEG jump  continue_search;

      // update nearest one
      r4 = r0;
      r3 = r2;
      continue_search:
      // go to next entry
      r2 = r2 + 3;
   jump  search_tone_rate_loop;

   exact_tone_found:
   r3 = r2;
   seach_done:
   // r3 = exact or nearest entry
   r0 = M[r3 + 1];
   r1 = M[r3 + 2];
   // r0 = rate -> 8khz coeffs
   // r1 = rate -> 16khz coeffs
   r2 = 8000;//M[frame_dac_sampling_rate];
   Null = r2 - 8000;
   if NZ r0 = r1;
   // set operator filter coeffs
   r8 = &$tone_in.copy_op.param;
   call $iir_resamplev2.SetFilter;

   // clear tone buffer
   r0 = $tone_in.cbuffer_struc;
   call $cbuffer.empty_buffer;

   // auxiliary input expected now
   r0 = 1;
   M[$tone_in.aux_input_stream_available] = r0;

   jump $pop_rLink_and_rts;
.ENDMODULE;
