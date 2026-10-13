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


.CONST  $SCO_IN_PORT  (($cbuffer.READ_PORT_MASK  + $CHAN_B_8KHZ_IN_PORT_NUMBER) | $cbuffer.FORCE_PCM_AUDIO);
.CONST  $SCO_OUT_PORT (($cbuffer.WRITE_PORT_MASK + $CHAN_B_8KHZ_OUT_PORT_NUMBER) | $cbuffer.FORCE_PCM_AUDIO);


.MODULE $sco_data;
    .DATASEGMENT DM;

    DeclareCBuffer(port_in.cbuffer_struc ,mem1,3*93);          // Big enough for 2 SCO packets with headers
    DeclareCBuffer(sco_in.cbuffer_struc  ,mem2,3*90);          // Big enough for 2 SCO packets
    DeclareCBuffer(port_out.cbuffer_struc,mem3,3*90);          // Big enough for 2 SCO packets

    // declare all plc memory for testing plc module
    .VAR/DM sco_decoder[$sco_decoder.STRUC_SIZE] =
            $frame_sync.sco_decoder.pcm.validate,        // VALIDATE_FUNC
            $frame_sync.sco_decoder.pcm.process,         // DECODE_FUNC
            $frame_sync.sco_decoder.pcm.initialize,      // RESET_FUNC
            0,                                // DATA_PTR
            0.3;                              // THRESHOLD (between 0 and 1) //wmsn: this field is for PLC only

#if uses_PLC
   .VAR/DM1 buffer_speech[$plc100.SP_BUF_LEN_NB];
   .VAR/DM buffer_ola[$plc100.OLA_LEN_NB];
#endif

    .VAR/DM plc_obj[$plc100.STRUC_SIZE] =
                   &buffer_speech,                    // SPEECH_BUF_START_ADDR_FIELD: 0
                   &buffer_speech,                    // SPEECH_BUF_PTR_FIELD: 1
                   &buffer_ola,                       // OLA_BUF_PTR_FIELD:     2
                   &sco_in.cbuffer_struc,             // OUTPUT_PTR_FIELD:   3
                   &$plc100.nb_consts,                // CONSTS_FIELD:  4
                   $plc100.INITIAL_ATTENUATION,       // ATTENUATION_FIELD: 5
                   0.3,                               // PER_THRESHOLD_FIELD:  6
                   0 ...;

    .VAR/DM object[$sco_pkt_handler.STRUC_SIZE ] =
                   $SCO_IN_PORT | $cbuffer.FORCE_LITTLE_ENDIAN,    // SCO PORT (header)
                   $SCO_IN_PORT | $cbuffer.FORCE_PCM_AUDIO,        // SCO port (payload)
                   &port_in.cbuffer_struc,             // INPUT_PTR_FIELD
                   &sco_in.cbuffer_struc,              // OUTPUT_PTR_FIELD:   2
#if uses_PLC
                   0x2000,                             // ENABLE_FIELD
                   0x2000,                             // CONFIG_FIELD, assigned statically in example apps, but gets assigned
#else
                   0,                             // ENABLE_FIELD
                   0,                             // CONFIG_FIELD
#endif
                                                       // by CVC app's if any processing blocks are used.
                   0,                                  // STAT_LIMIT_FIELD
                   0,                                  // PACKET_IN_LEN_FIELD
                   0,                                  // PACKET_OUT_LEN_FIELD: 6
                   &sco_decoder,                       // DECODER_PTR
#if uses_PLC
                   $plc100.process,                    // PLC_PROCESS_PTR
                   $plc100.initialize,                 // PLC_RESET_PTR
#else
                   0,
                   0,
#endif
                   0,                                  // BFI_FIELD:          7
                   0,                                  // PACKET_LOSS_FIELD
                   0,                                  // INV_STAT_LIMIT_FIELD
                   0,                                  // PACKET_COUNT_FIELD
                   0,                                  // BAD_PACKET_COUNT_FIELD
#if uses_PLC
                   &plc_obj,                           // PLC_DATA_PTR_FIELD
#else
                   0,
#endif
                   $SCO_OUT_PORT | $cbuffer.FORCE_PCM_AUDIO,           // SCO_OUT_PORT_FIELD (payload)
                   -8,                                                 // SCO_OUT_SHIFT_FIELD
                   &port_out.cbuffer_struc,                 // SCO_OUT_BUFFER_FIELD
                   30,                                      // SCO_OUT_PKTSIZE_FIELD
                   0,                                       // SCO_PARAM_TESCO_FIELD
                   0,                                       // SCO_PARAM_SLOT_LS_FIELD
                   0,                                       // SCO_NEW_PARAMS_FLAG
                   0,                                       // JITTER_PTR_FIELD
                   0,
                   120,                                     // ENCODER_INPUT_SIZE_FIELD
                   30,                                      // ENCODER_OUTPUT_SIZE_FIELD
                   0,
                   0;

.ENDMODULE;
