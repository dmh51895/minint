/*
 * PROJECT:         ReactOS Windows-Compatible Session Manager
 * LICENSE:         BSD 2-Clause License
 * FILE:            base/system/smss/smsbapi.c
 * PURPOSE:         Main SMSS Code
 * PROGRAMMERS:     Alex Ionescu
 */

/* INCLUDES *******************************************************************/


/* MinNT includes */
#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ps.h>
#include <nt/cm.h>
#include <nt/se.h>
#include <nt/lpc.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/dispatcher.h>
#include <ndk/obfuncs.h>
#include <ndk/cmfuncs.h>
#include <ndk/lpcfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/setypes.h>
#include <ndk/rtlfuncs.h>

#include "smss.h"

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#if DBG
const PCSTR SmpSubSystemNames[] =
{
    "Unknown",
    "Native",
    "Windows GUI",
    "Windows CUI",
    NULL,
    "OS/2 CUI",
    NULL,
    "Posix CUI"
};
#endif

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
SmpSbCreateSession(IN PVOID Reserved,
                   IN PSMP_SUBSYSTEM OtherSubsystem,
                   IN PRTL_USER_PROCESS_INFORMATION ProcessInformation,
                   IN ULONG DbgSessionId,
                   IN PCLIENT_ID DbgUiClientId)
{
    NTSTATUS Status;
    ULONG SubSystemType = ProcessInformation->ImageInformation.ImageSubSystemType;
    ULONG MuSessionId;
    ULONG SessionId;
    PSMP_SUBSYSTEM KnownSubsys;
    SB_API_MSG SbApiMsg = {0};
    PSB_CREATE_SESSION_MSG CreateSessionMsg = &SbApiMsg.u.CreateSession;

    /* Write out the create session message including its initial process */
    CreateSessionMsg->ProcessInfo = *ProcessInformation;
    CreateSessionMsg->DbgSessionId = DbgSessionId;
    if (DbgUiClientId)
    {
        CreateSessionMsg->DbgUiClientId = *DbgUiClientId;
    }
    else
    {
        CreateSessionMsg->DbgUiClientId.UniqueThread = NULL;
        CreateSessionMsg->DbgUiClientId.UniqueProcess = NULL;
    }

    /* Find a subsystem responsible for this session */
    SmpGetProcessMuSessionId(ProcessInformation->ProcessHandle, &MuSessionId);
    if (!SmpCheckDuplicateMuSessionId(MuSessionId))
    {
        NtClose(ProcessInformation->ProcessHandle);
        NtClose(ProcessInformation->ThreadHandle);
        DbgPrint("SMSS: CreateSession status=%x\n", STATUS_OBJECT_NAME_NOT_FOUND);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    /* Find the subsystem suitable for this initial process */
    KnownSubsys = SmpLocateKnownSubSysByType(MuSessionId, SubSystemType);
    if (KnownSubsys)
    {
        /* Duplicate the process handle into the message */
        Status = NtDuplicateObject(((HANDLE)(LONG_PTR)-1),
                                   ProcessInformation->ProcessHandle,
                                   KnownSubsys->ProcessHandle,
                                   &CreateSessionMsg->ProcessInfo.ProcessHandle,
                                   0x001FFFFF,
                                   0,
                                   0);
        if (NT_SUCCESS(Status))
        {
            /* Duplicate the thread handle into the message */
            Status = NtDuplicateObject(((HANDLE)(LONG_PTR)-1),
                                       ProcessInformation->ThreadHandle,
                                       KnownSubsys->ProcessHandle,
                                       &CreateSessionMsg->ProcessInfo.ThreadHandle,
                                       0x001FFFFF,
                                       0,
                                       0);
            if (!NT_SUCCESS(Status))
            {
                /* Close everything on failure */
                NtClose(ProcessInformation->ProcessHandle);
                NtClose(ProcessInformation->ThreadHandle);
                SmpDereferenceSubsystem(KnownSubsys);
                DbgPrint("SmpSbCreateSession: NtDuplicateObject (Thread) Failed %lx\n", Status);
                return Status;
            }

            /* Close the original handles as they are no longer needed */
            NtClose(ProcessInformation->ProcessHandle);
            NtClose(ProcessInformation->ThreadHandle);

            /* Finally, allocate a new SMSS session ID for this session */
            SessionId = SmpAllocateSessionId(KnownSubsys, OtherSubsystem);
            CreateSessionMsg->SessionId = SessionId;

            /* Fill out the LPC message header and send it to the client! */
            SbApiMsg.ApiNumber = SbpCreateSession;
            SbApiMsg.h.u2.ZeroInit = 0;
            SbApiMsg.h.u1.s1.DataLength = sizeof(SB_CREATE_SESSION_MSG) + 8;
            SbApiMsg.h.u1.s1.TotalLength = sizeof(SbApiMsg);
            Status = NtRequestWaitReplyPort(KnownSubsys->SbApiPort,
                                            &SbApiMsg.h,
                                            &SbApiMsg.h);
            if (!NT_SUCCESS(Status))
            {
                /* Bail out */
                DbgPrint("SmpSbCreateSession: NtRequestWaitReply Failed %lx\n", Status);
            }
            else
            {
                /* If the API succeeded, get the result value from the LPC */
                Status = SbApiMsg.ReturnValue;
            }

            /* Delete the session on any kind of failure */
            if (!NT_SUCCESS(Status)) SmpDeleteSession(SessionId);
        }
        else
        {
            /* Close the handles on failure */
            DbgPrint("SmpSbCreateSession: NtDuplicateObject (Process) Failed %lx\n", Status);
            NtClose(ProcessInformation->ProcessHandle);
            NtClose(ProcessInformation->ThreadHandle);
        }

        /* Dereference the subsystem and return the status of the LPC call */
        SmpDereferenceSubsystem(KnownSubsys);
        return Status;
    }

    /* If we don't yet have a subsystem, only native images can be launched */
    if (SubSystemType != IMAGE_SUBSYSTEM_NATIVE)
    {
        /* Fail */
#if DBG
        PCSTR SubSysName = NULL;
        CHAR SubSysTypeName[sizeof("Type 0x")+8];

        if (SubSystemType < RTL_NUMBER_OF(SmpSubSystemNames))
            SubSysName = SmpSubSystemNames[SubSystemType];
        if (!SubSysName)
        {
            SubSysName = SubSysTypeName;
            sprintf(SubSysTypeName, "Type 0x%08lx", SubSystemType);
        }
        DbgPrint("SMSS: %s SubSystem not found (either not started or destroyed).\n", SubSysName);
#endif
        Status = STATUS_UNSUCCESSFUL;
        NtClose(ProcessInformation->ProcessHandle);
        NtClose(ProcessInformation->ThreadHandle);
        return Status;
    }

#if 0
    /*
     * This code is part of the LPC-based legacy debugging support for native
     * applications, implemented with the debug client interface (DbgUi) and
     * debug subsystem (DbgSs). It is now vestigial since WinXP+ and is here
     * for informational purposes only.
     */
    if ((*(ULONGLONG)&CreateSessionMsg.DbgUiClientId) && SmpDbgSsLoaded)
    {
        Process = RtlAllocateHeap(SmpHeap, SmBaseTag, sizeof(SMP_PROCESS));
        if (!Process)
        {
            DbgPrint("Unable to initialize debugging for Native App %lx.%lx -- out of memory\n",
                    ProcessInformation->ClientId.UniqueProcess,
                    ProcessInformation->ClientId.UniqueThread);
            NtClose(ProcessInformation->ProcessHandle);
            NtClose(ProcessInformation->ThreadHandle);
            return STATUS_NO_MEMORY;
        }

        Process->DbgUiClientId = CreateSessionMsg->DbgUiClientId;
        Process->ClientId = ProcessInformation->ClientId;
        InsertHeadList(&NativeProcessList, &Process->Entry);
        DbgPrint("Native Debug App %lx.%lx\n",
                Process->ClientId.UniqueProcess,
                Process->ClientId.UniqueThread);

        Status = NtSetInformationProcess(ProcessInformation->ProcessHandle,
                                         7,
                                         &SmpDebugPort,
                                         sizeof(SmpDebugPort));
            }
#endif

    /* This is a native application being started as the initial command */
    DbgPrint("Subsystem active, starting thread\n");
    NtClose(ProcessInformation->ProcessHandle);
    NtResumeThread(ProcessInformation->ThreadHandle, NULL);
    NtClose(ProcessInformation->ThreadHandle);
    return STATUS_SUCCESS;
}

