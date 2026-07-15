/*
 * MinNT - boot/chain/admin2.h
 * Additional administrative applets.
 */

#ifndef _BOOT_ADMIN2_H_
#define _BOOT_ADMIN2_H_

VOID NTAPI Ad2Init(VOID);
VOID NTAPI Ad2OpenApplet(ULONG AppletId);
VOID NTAPI Ad2Tick(VOID);
BOOLEAN NTAPI Ad2IsActive(VOID);
BOOLEAN NTAPI Ad2HandleMouseEvent(SHORT mx, SHORT my,
                                    BOOLEAN leftDown, BOOLEAN leftPrev);

#endif /* _BOOT_ADMIN2_H_ */
