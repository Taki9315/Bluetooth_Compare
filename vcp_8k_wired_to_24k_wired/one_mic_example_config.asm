// *****************************************************************************
// %%fullcopyright(2007)        http://www.csr.com
// %%version
//
// $Change: 1894100 $  $DateTime: 2014/05/09 10:06:38 $
// *****************************************************************************


#include "one_mic_example.h"
#include "stream_copy.h"
#include "stream_copy_passthrough.h"

#include "frame_sync_buffer.h"
#include "cbuffer.h"
#include "frame_sync_tsksched.h"
#include "VCP.h"

.MODULE $M.system_config.data;
   .DATASEGMENT DM;

// -----------------------------------------------------------------------------
// DATA OBJECTS USED WITH PROCESSING MODULES
//
// This section would be updated if more processing modules with data objects
// were to be added to the system.

// CHANNEL A

// Data object used with $stream_copy.pass_thru function
   .VAR snd_pass_thru_obj_a[$stream_copy.STRUC_SIZE] =
    &snd_stream_map_adc,
    &snd_stream_map_i2s_a_out,
    &snd_stream_map_refin,
    &$REF_struc;           // $M.STREAM_MIXER.OFFSET_PTR_CH2_MANTISSA


   .VAR rcv_pass_thru_obj_a[$stream_copy.STRUC_SIZE] =
    &rcv_stream_map_i2s_a_in,
    &rcv_stream_map_dac_l,0,0;

// CHANNEL B

// Data object used with $stream_copy.pass_thru function
   .VAR snd_pass_thru_obj_b[$stream_copy.STRUC_SIZE] =
    &snd_stream_map_sco_in,
    &snd_stream_map_i2s_b_out,
    &snd_stream_map_bt_refin,
    &$REF_Blue_struc;           // $M.STREAM_MIXER.OFFSET_PTR_CH2_MANTISSA


   .VAR rcv_pass_thru_obj_b[$stream_copy.STRUC_SIZE] =
    &rcv_stream_map_i2s_b_in,
    &rcv_stream_map_sco_out,
    0,
    0;


// CHANNEL C

// Data object used with $stream_copy.pass_thru function
   .VAR snd_pass_thru_obj_c[$stream_copy_pass.STRUC_SIZE] =
    &snd_stream_map_adc_2,
    &snd_stream_map_i2s_c_out;


   .VAR rcv_pass_thru_obj_c[$stream_copy_pass.STRUC_SIZE] =
    &rcv_stream_map_i2s_c_in,
    &rcv_stream_map_dac_r;


// -----------------------------------------------------------------------------

// STREAM MAPS
//
// A stream object is created for each stream: ADC, I2S_OUT, I2S_IN, and DAC.
// These objects are used to populate processing module data objects (such as
// aux_mix_dm1 and pass_thru_obj) with input pointers and output pointers so
// that processing modules (such as $stream_copy.pass_thru) know
// where to get and write their data.
//
// Entries would be added to these objects if more processing modules were to be
// added to the system.

// adc stream map (signal from ADC):
// The pass_thru_obj uses the adc stream to populate its ADC input fields.
   .VAR snd_stream_map_adc[$framesync_ind.ENTRY_SIZE_FIELD] =
      &$adc_in.left.cbuffer_struc,                  // $framesync_ind.CBUFFER_PTR_FIELD
      0,                                        // $framesync_ind.FRAME_PTR_FIELD
      0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
      $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
      $one_mic_example.JITTER,                  // $framesync_ind.JITTER_FIELD
      $frame_sync.distribute_input_stream_ind,  // Distribute Function
      $frame_sync.update_input_streams_ind,     // Update Function
      0 ...;
      
   .VAR snd_stream_map_adc_2[$framesync_ind.ENTRY_SIZE_FIELD] =
      &$adc_in.right.cbuffer_struc,                  // $framesync_ind.CBUFFER_PTR_FIELD
      0,                                        // $framesync_ind.FRAME_PTR_FIELD
      0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
      $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
      $one_mic_example.JITTER,                  // $framesync_ind.JITTER_FIELD
      $frame_sync.distribute_input_stream_ind,  // Distribute Function
      $frame_sync.update_input_streams_ind,     // Update Function
      0 ...;

      .VAR snd_stream_map_refin[$framesync_ind.ENTRY_SIZE_FIELD] =
      &$dac_out.ref_struc,              // CBuffer for Stream
      0,                                        // $framesync_ind.FRAME_PTR_FIELD
      0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
      $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
      $one_mic_example.JITTER,                  // $framesync_ind.JITTER_FIELD
      $frame_sync.distribute_sync_stream_ind,  // Distribute Function
      $frame_sync.update_sync_streams_ind,     // Update Function
      &snd_stream_map_adc,                     // $framesync_ind.SYNC_POINTER_FIELD
      1,                                        // $framesync_ind.SYNC_MULTIPLIER_FIELD
      0 ...;
      
    .VAR snd_stream_map_bt_refin[$framesync_ind.ENTRY_SIZE_FIELD] =
       &$USB_B.in.ref_struc,          // Bluetooth Reference Signal Buffer
       0,                              // $framesync_ind.FRAME_PTR_FIELD
       0,                              // $framesync_ind.CUR_FRAME_SIZE_FIELD
       $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
       $one_mic_example.JITTER,        // $framesync_ind.JITTER_FIELD
       $frame_sync.distribute_sync_stream_ind,  // Distribute Function
       $frame_sync.update_sync_streams_ind,     // Update Function
       &snd_stream_map_i2s_b_out,      // Sync with Bluetooth Output Stream
       1,                              // Sync Multiplier
       0 ...;



// i2s_a_out stream map (outgoing I2S signal, channel A):
   .VAR snd_stream_map_i2s_a_out[$framesync_ind.ENTRY_SIZE_FIELD] =
      &$USB_A.out.cbuffer_struc,                // CBuffer for Stream
      0,                                        // $framesync_ind.FRAME_PTR_FIELD
      0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
      $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
      0,                                        // $framesync_ind.JITTER_FIELD
      $frame_sync.distribute_output_stream_ind, // Distribute Function
      $frame_sync.update_output_streams_ind,    // Update Function
      0 ...;


// i2s_a_in stream map (incoming I2S signal, channel A):
   .VAR rcv_stream_map_i2s_a_in[$framesync_ind.ENTRY_SIZE_FIELD] =
       &$USB_A.in.cbuffer_struc,          // CBuffer for stream
       0,                                        // $framesync_ind.FRAME_PTR_FIELD
       0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
       $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
       $one_mic_example.JITTER,                  // $framesync_ind.JITTER_FIELD
       $frame_sync.distribute_input_stream_ind,  // Distribute Function
       $frame_sync.update_input_streams_ind,     // Update Function
       0 ...;

// i2s_b_out stream map (outgoing I2S signal, channel B):
   .VAR snd_stream_map_i2s_b_out[$framesync_ind.ENTRY_SIZE_FIELD] =
      &$USB_B.out.cbuffer_struc,                // CBuffer for Stream
      0,                                        // $framesync_ind.FRAME_PTR_FIELD
      0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
      $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
      0,                                        // $framesync_ind.JITTER_FIELD
      $frame_sync.distribute_output_stream_ind, // Distribute Function
      $frame_sync.update_output_streams_ind,    // Update Function
      0 ...;


// i2s_b_in stream map (incoming I2S signal, channel B):
   .VAR rcv_stream_map_i2s_b_in[$framesync_ind.ENTRY_SIZE_FIELD] =
       &$USB_B.in.cbuffer_struc,          // CBuffer for stream
       0,                                        // $framesync_ind.FRAME_PTR_FIELD
       0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
       $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
       $one_mic_example.JITTER,                  // $framesync_ind.JITTER_FIELD
       $frame_sync.distribute_input_stream_ind,  // Distribute Function
       $frame_sync.update_input_streams_ind,     // Update Function
       0 ...;

// i2s_c_out stream map (outgoing I2S signal, channel C):
   .VAR snd_stream_map_i2s_c_out[$framesync_ind.ENTRY_SIZE_FIELD] =
      &$USB_C.out.cbuffer_struc,                // CBuffer for Stream
      0,                                        // $framesync_ind.FRAME_PTR_FIELD
      0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
      $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
      0,                                        // $framesync_ind.JITTER_FIELD
      $frame_sync.distribute_output_stream_ind, // Distribute Function
      $frame_sync.update_output_streams_ind,    // Update Function
      0 ...;


// i2s_c_in stream map (incoming I2S signal, channel C):
   .VAR rcv_stream_map_i2s_c_in[$framesync_ind.ENTRY_SIZE_FIELD] =
       &$USB_C.in.cbuffer_struc,          // CBuffer for stream
       0,                                        // $framesync_ind.FRAME_PTR_FIELD
       0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
       $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
       $one_mic_example.JITTER,                  // $framesync_ind.JITTER_FIELD
       $frame_sync.distribute_input_stream_ind,  // Distribute Function
       $frame_sync.update_input_streams_ind,     // Update Function
       0 ...;


// dac stream map (signal to loudspeaker):
// The pass_thru_obj uses the dac stream to populate its DAC output fields.

   .VAR rcv_stream_map_dac_l[$framesync_ind.ENTRY_SIZE_FIELD] =
      &$dac_out.cbuffer_struc_l,                  // $framesync_ind.CBUFFER_PTR_FIELD
      0,                                        // $framesync_ind.FRAME_PTR_FIELD
      0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
      $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
      $one_mic_example.JITTER,                      // $framesync_ind.JITTER_FIELD
      $frame_sync.distribute_output_stream_ind,     // Distribute Function
      $frame_sync.update_output_streams_ind,        // Update Function
      0 ...;

   .VAR rcv_stream_map_dac_r[$framesync_ind.ENTRY_SIZE_FIELD] =
      &$dac_out.cbuffer_struc_r,                  // $framesync_ind.CBUFFER_PTR_FIELD
      0,                                        // $framesync_ind.FRAME_PTR_FIELD
      0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
      $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
      $one_mic_example.JITTER,                      // $framesync_ind.JITTER_FIELD
      $frame_sync.distribute_output_stream_ind,     // Distribute Function
      $frame_sync.update_output_streams_ind,        // Update Function
      0 ...;

// SCO in and out

   .VAR snd_stream_map_sco_in[$framesync_ind.ENTRY_SIZE_FIELD] =
      &$sco_data.sco_in.cbuffer_struc,          // $framesync_ind.CBUFFER_PTR_FIELD
      0,                                        // $framesync_ind.FRAME_PTR_FIELD
      0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
      $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
      $one_mic_example.JITTER,                  // $framesync_ind.JITTER_FIELD
      $frame_sync.distribute_input_stream_ind,  // Distribute Function
      $frame_sync.update_input_streams_ind,     // Update Function
      0 ...;

   .VAR rcv_stream_map_sco_out[$framesync_ind.ENTRY_SIZE_FIELD] =
      &$sco_data.port_out.cbuffer_struc,        // $framesync_ind.CBUFFER_PTR_FIELD
      0,                                        // $framesync_ind.FRAME_PTR_FIELD
      0,                                        // $framesync_ind.CUR_FRAME_SIZE_FIELD
      $one_mic_example.NUM_SAMPLES_PER_FRAME,   // $framesync_ind.FRAME_SIZE_FIELD
      $one_mic_example.JITTER,                      // $framesync_ind.JITTER_FIELD
      $frame_sync.distribute_output_stream_ind,     // Distribute Function
      $frame_sync.update_output_streams_ind,        // Update Function
      0 ...;


// ----------------------------------------------------------------------------
// STREAM TABLES
// These tables is used with $frame_sync.distribute_streams and $frame_sync.update_streams.
//  They identify the streams used in the processing

// Stream List for Receive Processing
.VAR    rcv_process_streams[] =
   &rcv_stream_map_i2s_a_in,
   &rcv_stream_map_dac_l,
   &rcv_stream_map_dac_r,
   &rcv_stream_map_i2s_b_in,
   &rcv_stream_map_i2s_c_in,
   &rcv_stream_map_sco_out,
   0;

// Stream List for Send Processing
.VAR    snd_process_streams[] =
   &snd_stream_map_adc,
   &snd_stream_map_adc_2,
   &snd_stream_map_i2s_a_out,
   &snd_stream_map_refin,
   &snd_stream_map_sco_in,
   &snd_stream_map_bt_refin,
   &snd_stream_map_i2s_b_out,
   &snd_stream_map_i2s_c_out,
   0;

// -----------------------------------------------------------------------------
// REINITIALIZATION FUNCTION TABLE
// Reinitialization functions and corresponding data objects can be placed
// in this table.  Functions in this table all called every time a frame of data
// is ready to be processed and the reinitialization flag is set.
   .VAR reinitialize_table[] =
    // Function                          r7                         r8
    $frame_sync.sco_initialize,          &$sco_data.object,      0,
    0;


// -----------------------------------------------------------------------------

// MODE FUNCTION TABLE
// This table contains all of the modes in this system.  The VM plugin sends a
// message that contains a mode value, which is used as a pointer into this
// table.  As shipped, this file only contains one
// mode, which corresponds to pass_thru operation.  Developers can expand this
// table if they add additional processing modes to the system.  This table must
// be null terminated.
//
// Every time a frame of data is ready to process, the functions from the
// corresponding mode table are called.
   .VAR send_mode_table[] =
    &snd_funcs, // mode 0, send function
    // more entries can be added here
    0;

    .VAR receive_mode_table[] =
    &rcv_funcs, // mode 0, receive function
    // more entries can be added here
    0;


// ----------------------------------------------------------------------------
// MODE TABLES (aka FUNCTION TABLES)
// Modes are defined as tables that contain a list of functions with
// corresponding
// data objects.  The functions are called in the order that they appear.
//
// $frame_sync.distribute_stream should always be first as it tells the processing
// modules where to get and write data.
//
// $frame_sync.update_streams should always be last as it advances cbuffer pointers
// to the correct positions after the processing modules have read and written
// data.
//
// The processing modules are called by the function $frame_sync.run_function_table,
// which is defined in the frame_sync library.
//
// Processing modules must be written to take input from r7 and r8.  A zero
// should be used if the module does not require input.
//
// Mode tables must be null terminated.
//
// Additional modes can be created by adding new tables.
//************************
#ifndef VCP_1MIC
#error PUT VCP_1MIC define into project settings
#endif

  .VAR/DMCIRC $VcpProcBufferMICin[360];
  .VAR/DMCIRC $VcpProcBufferMICout[360];

  .VAR/DMCIRC $VcpProcBufferSPKin[360];
  .VAR/DMCIRC $VcpProcBufferSPKout[360];

  .VAR/DMCIRC $VcpProcBufferREFin[360];
  
  .VAR/DMCIRC $VcpProcBufferBlueMICin[360];
  .VAR/DMCIRC $VcpProcBufferBlueMICout[360];

  .VAR/DMCIRC $VcpProcBufferBlueSPKin[360];
  .VAR/DMCIRC $VcpProcBufferBlueSPKout[360];

  .VAR/DMCIRC $VcpProcBufferBlueREFin[360];

  .VAR $MIC_struc[ 8 ] = &$VcpProcBufferMICin,                                   //in read
                         &$VcpProcBufferMICin ,    //in write
                         &$VcpProcBufferMICout,                                  //out read
                         &$VcpProcBufferMICout + LENGTH($VcpProcBufferMICout)/2,  //out write
                         LENGTH($VcpProcBufferMICout),    //sizeof in-out array
                         0,                              //amount data
                         LENGTH($VcpProcBufferMICin),
                         LENGTH($VcpProcBufferMICout)/2;


  .VAR $SPK_struc[ 8 ] = &$VcpProcBufferSPKin,                                   // in read
                         &$VcpProcBufferSPKin ,    // in write
                         &$VcpProcBufferSPKout,                                  // out read
                         &$VcpProcBufferSPKout + LENGTH($VcpProcBufferSPKout)/2,  // out write
                         LENGTH($VcpProcBufferSPKout),    //sizeof in-out array
                         0,                              //amount data
                         LENGTH($VcpProcBufferSPKin),
                         LENGTH($VcpProcBufferSPKin)/2;

  .VAR $REF_struc[ 8 ] = &$VcpProcBufferREFin,                                   // in read
                         &$VcpProcBufferREFin ,    // in write
                         0,                                  // out read
                         0,  // out write
                         LENGTH($VcpProcBufferREFin),    //sizeof in-out array
                         0,                              //amount data
                         LENGTH($VcpProcBufferREFin),
                         LENGTH($VcpProcBufferREFin)/2;
                         

  .VAR $MIC_Blue_struc[ 8 ] = &$VcpProcBufferBlueMICin,                                   //in read
                         &$VcpProcBufferBlueMICin ,    //in write
                         &$VcpProcBufferBlueMICout,                                  //out read
                         &$VcpProcBufferBlueMICout + LENGTH($VcpProcBufferBlueMICout)/2,  //out write
                         LENGTH($VcpProcBufferBlueMICout),    //sizeof in-out array
                         0,                              //amount data
                         LENGTH($VcpProcBufferBlueMICin),
                         LENGTH($VcpProcBufferBlueMICout)/2;


  .VAR $SPK_Blue_struc[ 8 ] = &$VcpProcBufferBlueSPKin,                                   // in read
                         &$VcpProcBufferBlueSPKin ,    // in write
                         &$VcpProcBufferBlueSPKout,                                  // out read
                         &$VcpProcBufferBlueSPKout + LENGTH($VcpProcBufferBlueSPKout)/2,  // out write
                         LENGTH($VcpProcBufferBlueSPKout),    //sizeof in-out array
                         0,                              //amount data
                         LENGTH($VcpProcBufferBlueSPKin),
                         LENGTH($VcpProcBufferBlueSPKin)/2;

  .VAR $REF_Blue_struc[ 8 ] = &$VcpProcBufferBlueREFin,                                   // in read
                         &$VcpProcBufferBlueREFin ,    // in write
                         0,                                  // out read
                         0,  // out write
                         LENGTH($VcpProcBufferBlueREFin),    //sizeof in-out array
                         0,                              //amount data
                         LENGTH($VcpProcBufferBlueREFin),
                         LENGTH($VcpProcBufferBlueREFin)/2;
//*************************

  .VAR snd_funcs[] =
     // Function                         r7                    r8
     $frame_sync.distribute_streams_ind, &snd_process_streams, 0,
     // Channel A
     $stream_copy,                       &snd_pass_thru_obj_a, &$MIC_struc,
     // Channel B
     $stream_copy,                       &snd_pass_thru_obj_b, &$MIC_Blue_struc,
     // $stream_copy_pass,                  &snd_pass_thru_obj_b, 0,
     // Channel C
     $stream_copy_pass,                  &snd_pass_thru_obj_c, 0,
     $frame_sync.update_streams_ind,     &snd_process_streams, 0,
     0;

  .VAR rcv_funcs[] =
    // Function                          r7                    r8
    $frame_sync.distribute_streams_ind,  &rcv_process_streams, 0,
     // Channel A
    $stream_copy,                        &rcv_pass_thru_obj_a, &$SPK_struc,
     // Channel B
    $stream_copy,                        &rcv_pass_thru_obj_b, &$SPK_Blue_struc,
    // $stream_copy_pass,                   &rcv_pass_thru_obj_b, 0,
     // Channel C
     $stream_copy_pass,                  &rcv_pass_thru_obj_c, 0,   
    $frame_sync.update_streams_ind,      &rcv_process_streams, 0,
    0;
    
.ENDMODULE;
