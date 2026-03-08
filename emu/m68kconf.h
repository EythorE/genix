/* Musashi configuration for Genix workbench emulator */
#ifndef M68KCONF__HEADER
#define M68KCONF__HEADER

#define OPT_OFF             0
#define OPT_ON              1
#define OPT_SPECIFY_HANDLER 2

#ifndef M68K_COMPILE_FOR_MAME
#define M68K_COMPILE_FOR_MAME      OPT_OFF
#endif

#if M68K_COMPILE_FOR_MAME == OPT_OFF

/* Only need 68000 */
#define M68K_EMULATE_010            OPT_OFF
#define M68K_EMULATE_EC020          OPT_OFF
#define M68K_EMULATE_020            OPT_OFF
#define M68K_EMULATE_030            OPT_OFF
#define M68K_EMULATE_040            OPT_OFF

#define M68K_SEPARATE_READS         OPT_OFF
#define M68K_SIMULATE_PD_WRITES     OPT_OFF
#define M68K_EMULATE_INT_ACK        OPT_OFF
#define M68K_EMULATE_BKPT_ACK       OPT_OFF
#define M68K_EMULATE_TRACE          OPT_OFF
#define M68K_EMULATE_RESET          OPT_OFF
#define M68K_CMPILD_HAS_CALLBACK    OPT_OFF
#define M68K_RTE_HAS_CALLBACK       OPT_OFF
#define M68K_TAS_HAS_CALLBACK       OPT_OFF
#define M68K_ILLG_HAS_CALLBACK      OPT_OFF
#define M68K_EMULATE_FC             OPT_OFF
#define M68K_MONITOR_PC             OPT_OFF
#define M68K_INSTRUCTION_HOOK       OPT_OFF
#define M68K_EMULATE_PREFETCH       OPT_OFF
#define M68K_EMULATE_ADDRESS_ERROR  OPT_ON
#define M68K_LOG_ENABLE             OPT_OFF
#define M68K_LOG_1010_1111          OPT_OFF
#define M68K_LOG_FILEHANDLE         stderr
#define M68K_EMULATE_PMMU           OPT_OFF
#define M68K_USE_64_BIT             OPT_ON

#endif /* M68K_COMPILE_FOR_MAME */
#endif /* M68KCONF__HEADER */
