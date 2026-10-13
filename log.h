#ifndef LOG_H__
#define LOG_H__

/** If defined then debug printing is enabled. */
#define GLOBAL_DEBUG_ENABLED
#define DEBUG_ENABLED
/*
 * If local and global debug is enabled then define the print macros.
 *
 * Note: To get debug to print first GLOBAL_DEBUG_ENABLED must be defined,
 * if it is not defined then no debug will be printed. Debug must also be
 * enabled on a file by file basis. To do this define DEBUG_ENABLED in the
 * c file before including log.h. If DEBUG_ENABLED is not defined then no
 * debug will be printed from that file.
 */
#if defined(DEBUG_ENABLED) && defined(GLOBAL_DEBUG_ENABLED)

/** Macro used to print log messages. */
#define LOG_DEBUG(x)                (log_print x)

/** Macro used to print a bluetooth address. */
#define LOG_BLUETOOTH_ADDR(addr) \
    (LOG_DEBUG((" bdaddr [0x%x:0x%x:0x%lx]\n", \
    addr.nap, addr.uap, addr.lap)))

/** Macro used to print a bluetooth address from a pointer. */
#define LOG_BLUETOOTH_ADDRP(addr) \
    (LOG_DEBUG((" bdaddr [0x%x:0x%x:0x%lx]\n", \
    addr->nap, addr->uap, addr->lap)))

#else

/** None modifying macro for if debug is disabled. */
#define LOG_DEBUG(x)                    do {} while(0)

/** None modifying macro for if debug is disabled. */
#define LOG_BLUETOOTH_ADDR(addr)        do {} while(0)

/** None modifying macro for if debug is disabled. */
#define LOG_BLUETOOTH_ADDRP(addr)        do {} while(0)

#endif


/**
 * Formated printout to the standard output.
 *
 * @param format   Format as per printf.
 */
extern void
log_print(const char *format, ...);


#endif
