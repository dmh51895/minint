/*
 * MinNT - boot/chain/chain.h
 * Boot chain: SMSS → CSRSS → Winlogon → Explorer
 * All run as kernel threads simulating user-mode subsystems.
 */

#ifndef _CHAIN_H_
#define _CHAIN_H_

#include <nt/ntdef.h>

/* ---- SMSS (Session Manager) --------------------------------------------- */

VOID NTAPI SmssThread(PVOID Context);

/* ---- Winlogon ----------------------------------------------------------- */

VOID NTAPI WinlogonThread(PVOID Context);

/* ---- Explorer (Desktop Shell) ------------------------------------------- */

VOID NTAPI ExplorerThread(PVOID Context);

/* ---- Boot chain initialization ------------------------------------------ */

NTSTATUS NTAPI BootChainInit(VOID);

#endif /* _CHAIN_H_ */
