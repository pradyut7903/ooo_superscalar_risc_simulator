/*
 * Silent printf stub — CoreMark still calls ee_printf; UART not modeled.
 * Results are left in g_coremark_sink / register commits for golden.
 */
#include "core_portme.h"

int
ee_printf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}
