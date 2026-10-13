
#define   VCP_HAS_LOGGER

//*****************************************************************
////KVCP data structure indexees
#define $VCP.buff.in_read   (0)
#define $VCP.buff.in_write  (1)
#define $VCP.buff.out_read  (2)
#define $VCP.buff.out_write (3)
#define $VCP.buff.sizeof    (4)
#define $VCP.buff.amount    (5)
#define $VCP.buff.free      (6)
#define $VCP.buff.free_out  (7)
//*****************************************************************

#define NBANDS        32

//******************************************************************************

#define VCP_NFRAME              (1)


#define FRAME_TIME		  4

#define TIME_MS(x)		  ((x + (FRAME_TIME/2))/FRAME_TIME)
#define VERSION_BEEP_TIMER  (5000/FRAME_TIME)
#define VERSION_BEEP_FRAMES ((2000/FRAME_TIME) + VERSION_BEEP_TIMER)

#define XIN_BITS                      (4)
#define XOUT_BITS                     (6)

#define REAR_MIC_INVERSE_PHASE   (1)

#define Istruc  (&$Internal_par_struc)
#define ESTS            (&$est_struc)

#define   $inpar.rx_out        (13)
#define   $inpar.rx_in         (14)
#define   $inpar.tx_out        (15)
#define   $inpar.tx_in         (16)
#define   $inpar.debug_snd     (19)
#define   $inpar.hdt           (27)
#define   $inpar.rpd           (29)
#define   $inpar.udb           (79)
#define   $inpar.tap_thr       (163)

#define $est.hd_cnt          (27)

#define ns_rMinGain 				(ns_vrMinNsGains + 2 * NS_GROUPSLEN)
#define ns_rQ                       (ns_rMinGain + 12)

#define   $inpar.flags         (0)
//flags:
#define   f.bct_eq         (1)
#define   f.ndvc_sv        (2)
#define   f.af             (4)
#define   f.tx_bypass      (8)
#define   f.adm_lim        (16)
#define   f.tx_gdnr        (32)
#define   f.rx_bypass      (64)
#define   f.sat_beeps      (128)
#define   f.vol_beeps      (256)
#define   f.adm_es_on      (512)
#define   f.adm_am_on      (1024)
#define   f.es             (2048)
#define   f.reverse        (4096)
#define   f.logger         (8192)
#define   f.rx_gdnr        (16384)
#define   f.mic_boost      (0x8000)

#define   $inpar.front_gain    (56)
#define   $inpar.admcnf        (44)

#define   $inpar.pio           (74)

#define   $inpar.flags3        (119)
  //flags3:
#define   f.log_chan_mask      (0x1F)
#define   f.log_TXout          (1)
#define   f.log_RXout          (2)
#define   f.log_TX1in          (4)
#define   f.log_TX2in          (8)
#define   f.log_RXin           (16)
#define   f.rx_ns_bypass       (32)
#define   f.lpa                (0x4000)
#define   f.ovf_beeps          (0x8000)

#define PLAY_BACK_FINISHED_MSG    0x1080
#define   f.plc                (0x2000) //use this value in shell, can't be changed

#ifdef VCP_2MIC
    #define   VCP_HAS_ADM
    #define MICS_NUM    2
#else
    #define MICS_NUM    1
#endif
