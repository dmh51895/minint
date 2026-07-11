/*
 * MinNT - win32k/wine/debug.h
 * Wine debug compatibility header
 */

#ifndef WINE_DEBUG_H
#define WINE_DEBUG_H

#include <nt/ke.h>

#define WINEPREFIX __FILE__, __LINE__

typedef struct _DEBUG_INFO {
    ULONG_PTR prefix;
    ULONG flags;
    ULONG layer;
} DEBUG_INFO;

#define TRACE(fmt, ...) DbgPrint("WINE: " fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) DbgPrint("WINE: " fmt, ##__VA_ARGS__)
#define ERR(fmt, ...) DbgPrint("WINE ERROR: " fmt, ##__VA_ARGS__)

#define DPRINTF TRACE

#endif /* WINE_DEBUG_H */
