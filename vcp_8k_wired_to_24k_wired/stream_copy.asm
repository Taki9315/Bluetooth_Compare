// *****************************************************************************
// %%fullcopyright(2007)        http://www.csr.com
// %%version
//
// $Change: 1882869 $  $DateTime: 2014/04/23 13:37:24 $
// *****************************************************************************


// *****************************************************************************
// MODULE:
//    $M.stream_copy
//
// DESCRIPTION:
//   Function to copy data from one buffer into another.
//
// INPUTS:
//   r7 -  data object containing pointers to input and output data.  Also
//         contains the number of samples to process.
//
// OUTPUTS:
//   - none
//
// *****************************************************************************
#include "core_library.h"
#include "stream_copy.h"
#include "VCP.h"

.MODULE $M.stream_copy;
 .codesegment PM;
 .datasegment DM;

 .var loop_cnt = 0;

$stream_copy:

   push r8;
   push rLink;
   // Get Input Buffer
   r0  = M[r7 + $stream_copy.INPUT_PTR_BUFFER_FIELD];
#ifdef BASE_REGISTER_MODE
   call $frmbuffer.get_buffer_with_start_address;
   push r2;
   pop  B0;
#else
   call $frmbuffer.get_buffer;
#endif
   // r0 = buf ptr
   // r1 = circ buf length
   // r2 = buffer base address <base variant only>
   // r3 = frame size
   I0  = r0;
   L0  = r1;

   // Use input frame size
   r10 = r3;
   M[&loop_cnt] = r10;
   // Update output frame size from input
   r0 = M[r7 + $stream_copy.OUTPUT_PTR_BUFFER_FIELD];
   call $frmbuffer.set_frame_size;

   // Get output buffer
#ifdef BASE_REGISTER_MODE
   call $frmbuffer.get_buffer_with_start_address;
   push r2;
   pop  B4;
#else
   call $frmbuffer.get_buffer;
#endif
   I4 = r0;
   L4 = r1;
   pop rLink;
   pop r8;

// intermediate copy
   r0 = M[r8 + $VCP.buff.amount];
   r0 = r0 + r10;
   M[r8 + $VCP.buff.amount] = r0;


   //r8 - struc address
   r0 = M[r8 + $VCP.buff.in_write];
   I1 = r0;   r0 = M[r8 + $VCP.buff.sizeof];
   L1 = r0;

   r0 = M[r8 + $VCP.buff.out_read];
   I5 = r0;   r0 = M[r8 + $VCP.buff.sizeof];
   L5 = r0;

   //-------------check free space----------------------------
   r2 =  M[r8 + $VCP.buff.sizeof];
   r1 =  M[r8 + $VCP.buff.in_write];//write address
   r0 =  M[r8 + $VCP.buff.in_read];//read address

   r0 = r0 - r1;
   if LE r0 = r0 + r2;
   r0 = r0 - 1;
   M[r8 + $VCP.buff.free] = r0;

//--------------------------check out free space----------
   r2 =  M[r8 + $VCP.buff.sizeof];
   r1 =  M[r8 + $VCP.buff.out_write];//write address
   r0 =  M[r8 + $VCP.buff.out_read];//read address

   r0 = r1 - r0;
   if LE r0 = r0 + r2;
   r0 = r0 - 1;
   M[r8 + $VCP.buff.free_out] = r0;

//-----------------------------------------------------------

   //I1 - buffer for VCP
   //I5 - buffer with VCP out


// INPUT->OUTPUT
   r0=M[I0,1]; // first input
   do loop_passthru;
      M[I1,1] = r0, r0 = M[I5,1];
      M[I4,1] = r0, r0=M[I0,1]; // copy
   loop_passthru:



  //update intermediate
   r0 = I1;
   M[r8 + $VCP.buff.in_write] = r0;

   r0 = I5;
   M[r8 + $VCP.buff.out_read] = r0;

    r0 = M[r7 + $stream_copy.OFFSET_INPUT_REF_PTR];
    if Z jump no_ref;
   push rLink;
   #ifdef BASE_REGISTER_MODE
        call $frmbuffer.get_buffer_with_start_address;
        push r2;
        pop  B0;
   #else
        call $frmbuffer.get_buffer;
   #endif
   pop rLink;

   I0  = r0;
   L0  = r1;
   r4 = M[r7 + $stream_copy.OFFSET_PTR_REF_STRUC];       // OFFSET_PTR_CH1_REFstruc

  //r4 - struc address
   r0 = M[r4 + $VCP.buff.in_write];
   I5 = r0;   r0 = M[r4 + $VCP.buff.sizeof];
   L5 = r0;

   r0 = M[r4 + $VCP.buff.amount];
   r0 = r0 + M[&loop_cnt];
   M[r4 + $VCP.buff.amount] = r0;

   //-------------check free space----------------------------
   r2 =  M[r4 + $VCP.buff.sizeof];
   r1 =  M[r4 + $VCP.buff.in_write];//write address
   r0 =  M[r4 + $VCP.buff.in_read];//read address

   r0 = r0 - r1;
   if LE r0 = r0 + r2;
   r0 = r0 - 1;
   M[r4 + $VCP.buff.free] = r0;
 //----------------------------------------------
   r10 = M[&loop_cnt];
   do copy.ref.in;
   r0 = M[I0,1];
   M[I5,1] = r0;  //
copy.ref.in:
   r0 = I5;
   M[r4 + $VCP.buff.in_write] = r0;
no_ref:





// Clear L registers
   L0 = 0;
   L4 = 0;
   L1 = 0;
   L5 = 0;

#ifdef BASE_REGISTER_MODE
   push Null;
   B4 = M[SP-1];
   pop  B0;
#endif
   rts;
.ENDMODULE;
