/*
 * MinNT - boot/chain/admin.h
 * Public interface for the administrative applets.
 */

#ifndef _BOOT_ADMIN_H_
#define _BOOT_ADMIN_H_

VOID NTAPI AdminInit(VOID);
VOID NTAPI AdminOpenApplet(ULONG AppletId);
VOID NTAPI AdminTick(VOID);
BOOLEAN NTAPI AdminIsActive(VOID);
BOOLEAN NTAPI AdminHandleMouseEvent(SHORT mx, SHORT my,
                                      BOOLEAN leftDown, BOOLEAN leftPrev);

#endif /* _BOOT_ADMIN_H_ */
