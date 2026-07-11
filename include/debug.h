/* MinNT debug.h stub - ReactOS compat */
#ifndef _MINNT_DEBUG_H_
#define _MINNT_DEBUG_H_

#define DPRINT(...) DbgPrint(__VA_ARGS__)
#define DPRINT1(...) DbgPrint(__VA_ARGS__)
#define DPRINT2(...) DbgPrint(__VA_ARGS__)

#define ASSERTMSG(msg, expr) ((void)(expr))
#define UNIMPLEMENTED_DBGBREAK(msg) do { DbgPrint("UNIMPLEMENTED: %s - %s\n", __FUNCTION__, msg); } while(0)

#define NDEBUG 1

#endif
