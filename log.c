#include "log.h"


#include <stdio.h>


void
log_print(const char *format, ...)
{
    va_list(args);
    va_start(args, format);

    vprintf(format, args);

    va_end(args);
}

