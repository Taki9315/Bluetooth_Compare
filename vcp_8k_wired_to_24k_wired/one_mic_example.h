// *****************************************************************************
// %%fullcopyright(2007)        http://www.csr.com
// %%version
//
// $Change: 1882869 $  $DateTime: 2014/04/23 13:37:24 $
// *****************************************************************************

#ifndef one_mic_example_LIB_H
#define one_mic_example_LIB_H


.CONST $one_mic_example.REINITIALIZE           1;
.CONST $one_mic_example.VMMSG.SETMODE          4;
.CONST $one_mic_example.VMMSG.READY            5;
.CONST $one_mic_example.MESSAGE_REM_BT_ADDRESS 0x2001;

// System Modes
.CONST $one_mic_example.SYSMODE.PASSTHRU       0;

// Data block size
.CONST $one_mic_example.NUM_SAMPLES_PER_FRAME  60;
.CONST $one_mic_example.JITTER                 8;

.CONST $SAMPLE_RATE           8000;
.CONST $BLOCK_SIZE            60;

.CONST $BUFFER_SCALING                       4;
.CONST $TONE_BUFFER_SIZE                     ($BLOCK_SIZE * 2);
.CONST $TWO_MIC_EXAMPLE_BUFFER_SIZE          $BLOCK_SIZE;

#define uses_PLC 1

#endif
