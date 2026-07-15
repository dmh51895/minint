/*
 * MinNT - boot/chain/cpl.h
 * Public interface for the Control Panel applets.
 */

#ifndef _BOOT_CPL_H_
#define _BOOT_CPL_H_

/* Applet IDs (mirror the CPL_* defines in cpl.c) */
#define CPL_DISPLAY         0
#define CPL_SOUND           1
#define CPL_KEYBOARD        2
#define CPL_MOUSE           3
#define CPL_POWER           4
#define CPL_REGIONAL        5
#define CPL_ACCESSIBILITY   6
#define CPL_NOTIFICATIONS   7
#define CPL_PRIVACY         8
#define CPL_SYSTEM          9
#define CPL_TASKMAN         10
#define CPL_EVENTVIEW      11
#define CPL_DISKMGMT       12
#define CPL_DEVICEMGR      13
#define CPL_SERVICES       14
#define CPL_PERFMON        15
#define CPL_BACKUP         16
#define CPL_PERSONALIZE    17
#define CPL_SCREENSAVER    18
#define CPL_PRIVACY2       19
#define CPL_STARTMENU      20
#define CPL_TERMINAL       21
#define CPL_TASKBAR        22
#define CPL_THEMES         23
#define CPL_CONTROLLER     24
#define CPL_STEAMINPUT     25
#define CPL_WINE           26
#define CPL_PROPERTIES     27
#define CPL_APPS           17
#define CPL_NETWORK        18
#define CPL_PRINTERS       19
#define CPL_DESKTOP_ICONS  20
#define MAX_CPL_APPLETS    21

VOID NTAPI CplInit(VOID);
VOID NTAPI CplOpenApplet(ULONG AppletId);
VOID NTAPI CplTick(VOID);
VOID NTAPI CplMarkDirty(VOID);
BOOLEAN NTAPI CplIsActive(VOID);
BOOLEAN NTAPI CplHandleMouseEvent(SHORT mx, SHORT my,
                                    BOOLEAN leftDown, BOOLEAN leftPrev);

#endif /* _BOOT_CPL_H_ */
