/*
 * MinNT - win32k/settings.c
 * System settings subsystem - reads/writes user preferences from
 * the configuration manager (registry). Provides a unified interface
 * for all OS settings across the personalization, display, audio,
 * input, network, power, accessibility, and system categories.
 *
 * Storage layout (Win32-style):
 *   HKCU\Software\Microsoft\Windows\CurrentVersion\...
 *     \Desktop\Wallpaper, Colors, Metrics
 *     \Explorer\Advanced\Hidden, ShowExt
 *     \Input\Keyboard\RepeatRate, RepeatDelay
 *     \Power\SleepTimeout, LidCloseAction
 *     \Notifications\Enabled, DND
 *     \Privacy\Telemetry, AdId
 *     \SystemInfo\ComputerName, RegisteredOwner
 *
 *   HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\...
 *     \NetworkList\Profiles
 *     \Print\Environments
 *
 * Each setting accessor reads from the registry on first access
 * and caches the value for fast subsequent reads. Writes update
 * both the cache and the registry.
 */

#include "precomp.h"
#include <nt/cm.h>
#include <nt/rtl.h>

/* ---- Value types ------------------------------------------------------- */

#define REG_DWORD_LITTLE_ENDIAN  4
#define REG_SZ                   1
#define REG_BINARY               3

/* ---- Per-category settings -------------------------------------------- */

typedef struct _BOOL_SETTING {
    const char *Path;        /* registry path */
    const char *Name;        /* value name */
    BOOLEAN     DefaultValue;
    BOOLEAN     CachedValue;
    BOOLEAN     Loaded;
} BOOL_SETTING;

typedef struct _DWORD_SETTING {
    const char *Path;
    const char *Name;
    ULONG       DefaultValue;
    ULONG       CachedValue;
    ULONG       MinValue;
    ULONG       MaxValue;
    BOOLEAN     Loaded;
} DWORD_SETTING;

typedef struct _STRING_SETTING {
    const char *Path;
    const char *Name;
    const WCHAR *DefaultValue;
    WCHAR       CachedValue[256];
    BOOLEAN     Loaded;
} STRING_SETTING;

/* ---- Display settings -------------------------------------------------- */

static STRING_SETTING g_WallpaperPath = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Desktop",
    "Wallpaper", L"", {0}, FALSE
};

static STRING_SETTING g_DesktopColor = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Colors",
    "Background", L"0 0 0", {0}, FALSE
};

static DWORD_SETTING g_ResolutionWidth = {
    L"\\Registry\\User\\.DEFAULT\\System\\CurrentControlSet\\Control\\GraphicsDrivers",
    "ResolutionX", 1920, 1920, 320, 7680, FALSE
};

static DWORD_SETTING g_ResolutionHeight = {
    L"\\Registry\\User\\.DEFAULT\\System\\CurrentControlSet\\Control\\GraphicsDrivers",
    "ResolutionY", 1080, 1080, 240, 4320, FALSE
};

static DWORD_SETTING g_RefreshRate = {
    L"\\Registry\\User\\.DEFAULT\\System\\CurrentControlSet\\Control\\GraphicsDrivers",
    "RefreshRate", 60, 60, 30, 240, FALSE
};

static DWORD_SETTING g_DpiScale = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Desktop",
    "LogPixels", 96, 96, 50, 500, FALSE
};

static BOOL_SETTING g_NightLight = {
    L"\\Registry\\User\\.DEFAULT\\System\\CurrentControlSet\\Control\\NightLight",
    "Enabled", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_HdrEnabled = {
    L"\\Registry\\User\\.DEFAULT\\System\\CurrentControlSet\\Control\\Display",
    "HDREnabled", FALSE, FALSE, FALSE
};

/* ---- Audio settings ---------------------------------------------------- */

static DWORD_SETTING g_MasterVolume = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Audio",
    "MasterVolume", 80, 80, 0, 100, FALSE
};

static BOOL_SETTING g_Mute = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Audio",
    "Mute", FALSE, FALSE, FALSE
};

/* ---- Mouse & keyboard -------------------------------------------------- */

static DWORD_SETTING g_KeyboardRepeatRate = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Keyboard",
    "KeyboardSpeed", 31, 31, 0, 31, FALSE
};

static DWORD_SETTING g_KeyboardRepeatDelay = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Keyboard",
    "KeyboardDelay", 1, 1, 0, 3, FALSE
};

static DWORD_SETTING g_MouseSpeed = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Mouse",
    "MouseSpeed", 10, 10, 1, 20, FALSE
};

static DWORD_SETTING g_DoubleClickTime = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Mouse",
    "DoubleClickSpeed", 500, 500, 100, 1000, FALSE
};

static BOOL_SETTING g_SwapMouseButtons = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Mouse",
    "SwapMouseButtons", FALSE, FALSE, FALSE
};

/* ---- Power settings ---------------------------------------------------- */

static DWORD_SETTING g_SleepTimeoutAC = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Power",
    "SleepTimeoutAC", 900, 900, 0, 3600, FALSE
};

static DWORD_SETTING g_SleepTimeoutDC = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Power",
    "SleepTimeoutDC", 300, 300, 0, 3600, FALSE
};

static DWORD_SETTING g_ScreenTimeoutAC = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Power",
    "VideoTimeoutAC", 600, 600, 0, 3600, FALSE
};

static DWORD_SETTING g_ScreenTimeoutDC = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Power",
    "VideoTimeoutDC", 180, 180, 0, 3600, FALSE
};

static DWORD_SETTING g_PowerButtonAction = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Power",
    "PowerButtonAction", 1, 1, 0, 5, FALSE  /* 0=nothing,1=sleep,2=hibernate,3=shutdown,4=ask */
};

static BOOL_SETTING g_FastStartup = {
    L"\\Registry\\User\\.DEFAULT\\System\\CurrentControlSet\\Control\\Session Manager\\Power",
    "HiberbootEnabled", TRUE, TRUE, FALSE
};

/* ---- Region & language ------------------------------------------------- */

static DWORD_SETTING g_TimeZoneBias = {
    L"\\Registry\\User\\.DEFAULT\\System\\CurrentControlSet\\Control\\TimeZoneInformation",
    "Bias", 0, 0, -720, 720, FALSE
};

static DWORD_SETTING g_TimeFormat = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\International",
    "iTime", 0, 0, 0, 1, FALSE  /* 0=12h, 1=24h */
};

static DWORD_SETTING g_DateFormat = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\International",
    "iDate", 0, 0, 0, 2, FALSE
};

static BOOL_SETTING g_AutoTimeSync = {
    L"\\Registry\\User\\.DEFAULT\\System\\CurrentControlSet\\Services\\W32Time\\Parameters",
    "Type", TRUE, TRUE, FALSE  /* NoType=NT5DS=auto */
};

/* ---- Accessibility ----------------------------------------------------- */

static BOOL_SETTING g_HighContrast = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Accessibility\\HighContrast",
    "Flags", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_StickyKeys = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Accessibility\\StickyKeys",
    "Flags", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_FilterKeys = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Accessibility\\Keyboard Response",
    "Flags", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_ToggleKeys = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Accessibility\\ToggleKeys",
    "Flags", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_MouseKeys = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Accessibility\\MouseKeys",
    "Flags", FALSE, FALSE, FALSE
};

/* ---- Privacy ----------------------------------------------------------- */

static BOOL_SETTING g_TelemetryEnabled = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\DataCollection",
    "AllowTelemetry", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_AdvertisingId = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo",
    "Enabled", TRUE, TRUE, FALSE
};

static BOOL_SETTING g_LocationService = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\LocationGlobal",
    "Enabled", TRUE, TRUE, FALSE
};

/* ---- Notifications ----------------------------------------------------- */

static BOOL_SETTING g_NotificationsEnabled = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\PushNotifications",
    "Enabled", TRUE, TRUE, FALSE
};

static BOOL_SETTING g_DndMode = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\QuietHours",
    "Enabled", FALSE, FALSE, FALSE
};

/* ---- System Info ------------------------------------------------------- */

static STRING_SETTING g_ComputerName = {
    L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\ComputerName\\ComputerName",
    "ComputerName", L"MINNT-PC", {0}, FALSE
};

static STRING_SETTING g_RegisteredOwner = {
    L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion",
    "RegisteredOwner", L"User", {0}, FALSE
};

static STRING_SETTING g_RegisteredOrg = {
    L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion",
    "RegisteredOrganization", L"MinNT", {0}, FALSE
};

static STRING_SETTING g_ProductId = {
    L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion",
    "ProductId", L"12345-OEM-67890-ABCDE", {0}, FALSE
};

/* ---- Screensaver -------------------------------------------------------- */

static STRING_SETTING g_ScreensaverPath = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Desktop",
    "SCRNSAVE.EXE", L"", {0}, FALSE
};

static DWORD_SETTING g_ScreensaverTimeout = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Desktop",
    "ScreenSaveTimeOut", 600, 600, 0, 3600, FALSE
};

static BOOL_SETTING g_ScreensaverSecure = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Desktop",
    "ScreenSaverIsSecure", TRUE, TRUE, FALSE
};

static BOOL_SETTING g_ScreensaverEnabled = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Desktop",
    "ScreenSaveActive", FALSE, FALSE, FALSE
};

/* ---- User profile / avatar --------------------------------------------- */

static STRING_SETTING g_UserProfilePicture = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Desktop",
    "UserPicture", L"", {0}, FALSE
};

static STRING_SETTING g_UserDisplayName = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\International",
    "UserDisplayName", L"User", {0}, FALSE
};

/* ---- Taskbar / Start menu --------------------------------------------- */

static DWORD_SETTING g_TaskbarPosition = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "TaskbarPos", 1, 1, 0, 3, FALSE
};

static DWORD_SETTING g_TaskbarAutoHide = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "AutoHideTaskbar", 0, 0, 0, 1, FALSE
};

static DWORD_SETTING g_TaskbarSmallIcons = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "TaskbarSmallIcons", 0, 0, 0, 1, FALSE
};

static DWORD_SETTING g_TaskbarCombine = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "TaskbarGlomLevel", 0, 0, 0, 2, FALSE
};

static BOOL_SETTING g_ShowStartButton = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "ShowStartButton", TRUE, TRUE, FALSE
};

static STRING_SETTING g_StartButtonLabel = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "StartButtonLabel", L"Start", {0}, FALSE
};

static STRING_SETTING g_StartMenuStyle = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "StartMenuStyle", L"ClassicXP", {0}, FALSE
};

static BOOL_SETTING g_ShowRunCommand = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "ShowRunCommand", TRUE, TRUE, FALSE
};

static BOOL_SETTING g_ShowSearchBox = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "ShowSearchBox", TRUE, TRUE, FALSE
};

static BOOL_SETTING g_ShowMyComputer = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "ShowMyComputer", TRUE, TRUE, FALSE
};

static BOOL_SETTING g_ShowMyDocuments = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "ShowMyDocuments", TRUE, TRUE, FALSE
};

static BOOL_SETTING g_ShowControlPanel = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "ShowControlPanel", TRUE, TRUE, FALSE
};

static BOOL_SETTING g_ShowRecycleBin = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "ShowRecycleBin", TRUE, TRUE, FALSE
};

/* ---- File Explorer ---------------------------------------------------- */

static BOOL_SETTING g_ShowHiddenFiles = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "Hidden", 0, 0, FALSE
};

static BOOL_SETTING g_ShowFileExtensions = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "HideFileExt", 0, 0, FALSE
};

static BOOL_SETTING g_ShowProtectedOSFiles = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "ShowSuperHidden", 0, 0, FALSE
};

static BOOL_SETTING g_LaunchFolderWindows = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "SeparateProcess", 0, 0, FALSE
};

static DWORD_SETTING g_DefaultFolderView = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "FolderViewMode", 0, 0, 0, 8, FALSE
};

/* ---- Terminal / shell defaults ---------------------------------------- */

static STRING_SETTING g_DefaultTerminal = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "DefaultTerminal", L"C:\\Windows\\System32\\minnt-cmd.exe", {0}, FALSE
};

static STRING_SETTING g_DefaultShell = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "DefaultShell", L"C:\\Windows\\System32\\explorer.exe", {0}, FALSE
};

static STRING_SETTING g_PowerShellPath = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "PowerShellPath", L"C:\\Windows\\System32\\minnt-powershell.exe", {0}, FALSE
};

/* ---- Theme ------------------------------------------------------------- */

static STRING_SETTING g_ThemeName = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes",
    "ThemeName", L"Luna", {0}, FALSE
};

static STRING_SETTING g_AccentColor = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes",
    "AccentColor", L"#3A6EA5", {0}, FALSE
};

static BOOL_SETTING g_EnableTransparency = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes",
    "EnableTransparency", TRUE, TRUE, FALSE
};

/* ---- Customization (PowerToys-style) ----------------------------------- */

static BOOL_SETTING g_FancyZonesEnabled = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\PowerToys\\FancyZones",
    "Enabled", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_AlwaysOnTopEnabled = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\PowerToys\\AlwaysOnTop",
    "Enabled", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_ColorPickerEnabled = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\PowerToys\\ColorPicker",
    "Enabled", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_PowerRenameEnabled = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\PowerToys\\PowerRename",
    "Enabled", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_KeyboardManagerEnabled = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\PowerToys\\KeyboardManager",
    "Enabled", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_LightSwitchEnabled = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\PowerToys\\LightSwitch",
    "Enabled", FALSE, FALSE, FALSE
};

/* ---- Security ---------------------------------------------------------- */

static BOOL_SETTING g_TpmRequired = {
    L"\\Registry\\Machine\\Software\\MinNT\\Security",
    "TpmRequired", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_SecureBootRequired = {
    L"\\Registry\\Machine\\Software\\MinNT\\Security",
    "SecureBootRequired", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_RequireOnlineAccount = {
    L"\\Registry\\Machine\\Software\\MinNT\\Security",
    "RequireOnlineAccount", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_CortanaEnabled = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Search",
    "CortanaEnabled", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_ShowSyncNotifications = {
    L"\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Shell",
    "ShowSyncNotifications", FALSE, FALSE, FALSE
};

/* ---- Cosmetic: title bars, window borders ------------------------------ */

static DWORD_SETTING g_WindowBorderWidth = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Desktop\\WindowMetrics",
    "BorderWidth", 1, 1, 0, 16, FALSE
};

static DWORD_SETTING g_WindowCaptionHeight = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Desktop\\WindowMetrics",
    "CaptionHeight", 22, 22, 8, 64, FALSE
};

static DWORD_SETTING g_MenuHeight = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Desktop\\WindowMetrics",
    "MenuHeight", 20, 20, 8, 64, FALSE
};

static STRING_SETTING g_CursorScheme = {
    L"\\Registry\\User\\.DEFAULT\\Control Panel\\Cursors",
    "Scheme", L"Windows Default", {0}, FALSE
};

static STRING_SETTING g_SoundScheme = {
    L"\\Registry\\User\\.DEFAULT\\AppEvents\\Schemes",
    "Name", L"Windows Default", {0}, FALSE
};

/* ---- Diagnostic & feedback (all disabled by default) ------------------ */

static BOOL_SETTING g_ErrorReporting = {
    L"\\Registry\\Machine\\Software\\Microsoft\\Windows\\Windows Error Reporting",
    "Disabled", TRUE, TRUE, FALSE
};

static BOOL_SETTING g_FeedbackFrequency = {
    L"\\Registry\\Machine\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\DataCollection",
    "DoNotShowFeedbackNotifications", TRUE, TRUE, FALSE
};

static BOOL_SETTING g_ActivityHistory = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Privacy",
    "ActivityHistory", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_DiagnosticData = {
    L"\\Registry\\Machine\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\DataCollection",
    "DiagnosticData", FALSE, FALSE, FALSE
};

static BOOL_SETTING g_TailoredExperiences = {
    L"\\Registry\\User\\.DEFAULT\\Software\\Microsoft\\Windows\\CurrentVersion\\Privacy",
    "TailoredExperiences", FALSE, FALSE, FALSE
};

/* ---- Helper: parse a unicode path into segments ------------------------- */

static NTSTATUS OpenKeyPath(const char *Path, PCM_KEY_NODE *OutKey)
{
    UNICODE_STRING keyName;
    NTSTATUS status;

    /* Convert ASCII path to UNICODE_STRING */
    {
        SIZE_T len = 0;
        while (Path[len]) len++;
        keyName.Buffer = ExAllocatePool(NonPagedPool, (len + 1) * sizeof(WCHAR));
        if (!keyName.Buffer) return STATUS_NO_MEMORY;
        for (SIZE_T i = 0; i < len; i++) {
            keyName.Buffer[i] = (WCHAR)(UCHAR)Path[i];
        }
        keyName.Buffer[len] = 0;
        keyName.Length = (USHORT)(len * sizeof(WCHAR));
        keyName.MaximumLength = (USHORT)((len + 1) * sizeof(WCHAR));
    }

    status = CmCreateKey(&keyName, 0, OutKey);
    ExFreePool(keyName.Buffer);
    return status;
}

static NTSTATUS ReadStringSetting(STRING_SETTING *s, PWCHAR Out, ULONG OutLen)
{
    PCM_KEY_NODE key;
    UNICODE_STRING valueName;
    NTSTATUS status;
    ULONG nameLen;

    if (s->Loaded) {
        RtlCopyMemory(Out, s->CachedValue, sizeof(WCHAR) * 256);
        return STATUS_SUCCESS;
    }

    status = OpenKeyPath(s->Path, &key);
    if (!NT_SUCCESS(status)) {
        /* Use default */
        RtlCopyMemory(Out, s->DefaultValue, sizeof(WCHAR) * 256);
        return STATUS_SUCCESS;
    }

    nameLen = 0;
    while (s->Name[nameLen]) nameLen++;
    valueName.Buffer = ExAllocatePool(NonPagedPool, (nameLen + 1) * sizeof(WCHAR));
    if (!valueName.Buffer) return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < nameLen; i++) {
        valueName.Buffer[i] = (WCHAR)(UCHAR)s->Name[i];
    }
    valueName.Buffer[nameLen] = 0;
    valueName.Length = (USHORT)(nameLen * sizeof(WCHAR));
    valueName.MaximumLength = (USHORT)((nameLen + 1) * sizeof(WCHAR));

    {
        WCHAR buf[256];
        ULONG actualLen = 0;
        ULONG dataType = 0;
        status = CmQueryValue(key, &valueName, &dataType, buf, sizeof(buf), &actualLen);
        if (NT_SUCCESS(status) && (dataType == REG_SZ || dataType == REG_BINARY)) {
            ULONG toCopy = actualLen < sizeof(s->CachedValue) ? actualLen : sizeof(s->CachedValue);
            RtlCopyMemory(s->CachedValue, buf, toCopy);
            s->Loaded = TRUE;
            RtlCopyMemory(Out, s->CachedValue, sizeof(WCHAR) * 256);
        } else {
            /* Use default */
            RtlCopyMemory(Out, s->DefaultValue, sizeof(WCHAR) * 256);
        }
    }

    ExFreePool(valueName.Buffer);
    return STATUS_SUCCESS;
}

static NTSTATUS WriteStringSetting(STRING_SETTING *s, PCWSTR NewValue)
{
    PCM_KEY_NODE key;
    UNICODE_STRING valueName;
    NTSTATUS status;
    ULONG nameLen;

    status = OpenKeyPath(s->Path, &key);
    if (!NT_SUCCESS(status)) return status;

    nameLen = 0;
    while (s->Name[nameLen]) nameLen++;
    valueName.Buffer = ExAllocatePool(NonPagedPool, (nameLen + 1) * sizeof(WCHAR));
    if (!valueName.Buffer) return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < nameLen; i++) {
        valueName.Buffer[i] = (WCHAR)(UCHAR)s->Name[i];
    }
    valueName.Buffer[nameLen] = 0;
    valueName.Length = (USHORT)(nameLen * sizeof(WCHAR));
    valueName.MaximumLength = (USHORT)((nameLen + 1) * sizeof(WCHAR));

    {
        ULONG dataLen = 0;
        while (NewValue[dataLen]) dataLen++;
        dataLen *= sizeof(WCHAR);

        status = CmSetValue(key, &valueName, REG_SZ, (PVOID)NewValue, dataLen);
        if (NT_SUCCESS(status)) {
            RtlCopyMemory(s->CachedValue, NewValue, sizeof(WCHAR) * 256);
            s->Loaded = TRUE;
        }
    }

    ExFreePool(valueName.Buffer);
    return status;
}

static NTSTATUS ReadDwordSetting(DWORD_SETTING *s, PULONG OutValue)
{
    PCM_KEY_NODE key;
    UNICODE_STRING valueName;
    NTSTATUS status;
    ULONG nameLen;

    if (s->Loaded) {
        *OutValue = s->CachedValue;
        return STATUS_SUCCESS;
    }

    status = OpenKeyPath(s->Path, &key);
    if (!NT_SUCCESS(status)) {
        *OutValue = s->DefaultValue;
        return STATUS_SUCCESS;
    }

    nameLen = 0;
    while (s->Name[nameLen]) nameLen++;
    valueName.Buffer = ExAllocatePool(NonPagedPool, (nameLen + 1) * sizeof(WCHAR));
    if (!valueName.Buffer) return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < nameLen; i++) {
        valueName.Buffer[i] = (WCHAR)(UCHAR)s->Name[i];
    }
    valueName.Buffer[nameLen] = 0;
    valueName.Length = (USHORT)(nameLen * sizeof(WCHAR));
    valueName.MaximumLength = (USHORT)((nameLen + 1) * sizeof(WCHAR));

    {
        ULONG data = 0;
        ULONG actualLen = 0;
        ULONG dataType = 0;
        status = CmQueryValue(key, &valueName, &dataType, &data, sizeof(data), &actualLen);
        if (NT_SUCCESS(status) && actualLen >= sizeof(ULONG)) {
            s->CachedValue = data;
            s->Loaded = TRUE;
            *OutValue = data;
        } else {
            *OutValue = s->DefaultValue;
        }
    }

    ExFreePool(valueName.Buffer);
    return STATUS_SUCCESS;
}

static NTSTATUS WriteDwordSetting(DWORD_SETTING *s, ULONG NewValue)
{
    PCM_KEY_NODE key;
    UNICODE_STRING valueName;
    NTSTATUS status;
    ULONG nameLen;

    if (NewValue < s->MinValue) NewValue = s->MinValue;
    if (NewValue > s->MaxValue) NewValue = s->MaxValue;

    status = OpenKeyPath(s->Path, &key);
    if (!NT_SUCCESS(status)) return status;

    nameLen = 0;
    while (s->Name[nameLen]) nameLen++;
    valueName.Buffer = ExAllocatePool(NonPagedPool, (nameLen + 1) * sizeof(WCHAR));
    if (!valueName.Buffer) return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < nameLen; i++) {
        valueName.Buffer[i] = (WCHAR)(UCHAR)s->Name[i];
    }
    valueName.Buffer[nameLen] = 0;
    valueName.Length = (USHORT)(nameLen * sizeof(WCHAR));
    valueName.MaximumLength = (USHORT)((nameLen + 1) * sizeof(WCHAR));

    status = CmSetValue(key, &valueName, REG_DWORD_LITTLE_ENDIAN, &NewValue, sizeof(NewValue));
    if (NT_SUCCESS(status)) {
        s->CachedValue = NewValue;
        s->Loaded = TRUE;
    }

    ExFreePool(valueName.Buffer);
    return status;
}

static NTSTATUS ReadBoolSetting(BOOL_SETTING *s, PBOOL OutValue)
{
    PCM_KEY_NODE key;
    UNICODE_STRING valueName;
    NTSTATUS status;
    ULONG nameLen;

    if (s->Loaded) {
        *OutValue = s->CachedValue;
        return STATUS_SUCCESS;
    }

    status = OpenKeyPath(s->Path, &key);
    if (!NT_SUCCESS(status)) {
        *OutValue = s->DefaultValue;
        return STATUS_SUCCESS;
    }

    nameLen = 0;
    while (s->Name[nameLen]) nameLen++;
    valueName.Buffer = ExAllocatePool(NonPagedPool, (nameLen + 1) * sizeof(WCHAR));
    if (!valueName.Buffer) return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < nameLen; i++) {
        valueName.Buffer[i] = (WCHAR)(UCHAR)s->Name[i];
    }
    valueName.Buffer[nameLen] = 0;
    valueName.Length = (USHORT)(nameLen * sizeof(WCHAR));
    valueName.MaximumLength = (USHORT)((nameLen + 1) * sizeof(WCHAR));

    {
        ULONG data = 0;
        ULONG actualLen = 0;
        ULONG dataType = 0;
        status = CmQueryValue(key, &valueName, &dataType, &data, sizeof(data), &actualLen);
        if (NT_SUCCESS(status) && actualLen >= sizeof(ULONG)) {
            s->CachedValue = data ? TRUE : FALSE;
            s->Loaded = TRUE;
            *OutValue = s->CachedValue;
        } else {
            *OutValue = s->DefaultValue;
        }
    }

    ExFreePool(valueName.Buffer);
    return STATUS_SUCCESS;
}

static NTSTATUS WriteBoolSetting(BOOL_SETTING *s, BOOL NewValue)
{
    PCM_KEY_NODE key;
    UNICODE_STRING valueName;
    NTSTATUS status;
    ULONG nameLen;
    ULONG data = NewValue ? 1 : 0;

    status = OpenKeyPath(s->Path, &key);
    if (!NT_SUCCESS(status)) return status;

    nameLen = 0;
    while (s->Name[nameLen]) nameLen++;
    valueName.Buffer = ExAllocatePool(NonPagedPool, (nameLen + 1) * sizeof(WCHAR));
    if (!valueName.Buffer) return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < nameLen; i++) {
        valueName.Buffer[i] = (WCHAR)(UCHAR)s->Name[i];
    }
    valueName.Buffer[nameLen] = 0;
    valueName.Length = (USHORT)(nameLen * sizeof(WCHAR));
    valueName.MaximumLength = (USHORT)((nameLen + 1) * sizeof(WCHAR));

    status = CmSetValue(key, &valueName, REG_DWORD_LITTLE_ENDIAN, &data, sizeof(data));
    if (NT_SUCCESS(status)) {
        s->CachedValue = NewValue;
        s->Loaded = TRUE;
    }

    ExFreePool(valueName.Buffer);
    return status;
}

/* ---- SettingsInit: load all defaults, persist registry --------------- */

NTSTATUS NTAPI SettingsInit(VOID)
{
    BOOLEAN b;
    ULONG d;
    WCHAR buf[256];

    DbgPrint("SETTINGS: initializing from registry\n");

    /* Load each setting (uses defaults if not in registry) */
    ReadBoolSetting(&g_NightLight, &b);
    ReadBoolSetting(&g_HdrEnabled, &b);
    ReadBoolSetting(&g_Mute, &b);
    ReadDwordSetting(&g_ResolutionWidth, &d);
    ReadDwordSetting(&g_ResolutionHeight, &d);
    ReadDwordSetting(&g_RefreshRate, &d);
    ReadDwordSetting(&g_DpiScale, &d);
    ReadDwordSetting(&g_MasterVolume, &d);
    ReadDwordSetting(&g_KeyboardRepeatRate, &d);
    ReadDwordSetting(&g_KeyboardRepeatDelay, &d);
    ReadDwordSetting(&g_MouseSpeed, &d);
    ReadDwordSetting(&g_DoubleClickTime, &d);
    ReadBoolSetting(&g_SwapMouseButtons, &b);
    ReadDwordSetting(&g_SleepTimeoutAC, &d);
    ReadDwordSetting(&g_SleepTimeoutDC, &d);
    ReadDwordSetting(&g_ScreenTimeoutAC, &d);
    ReadDwordSetting(&g_ScreenTimeoutDC, &d);
    ReadDwordSetting(&g_PowerButtonAction, &d);
    ReadBoolSetting(&g_FastStartup, &b);
    ReadDwordSetting(&g_TimeZoneBias, &d);
    ReadDwordSetting(&g_TimeFormat, &d);
    ReadDwordSetting(&g_DateFormat, &d);
    ReadBoolSetting(&g_AutoTimeSync, &b);
    ReadBoolSetting(&g_HighContrast, &b);
    ReadBoolSetting(&g_StickyKeys, &b);
    ReadBoolSetting(&g_FilterKeys, &b);
    ReadBoolSetting(&g_ToggleKeys, &b);
    ReadBoolSetting(&g_MouseKeys, &b);
    ReadBoolSetting(&g_TelemetryEnabled, &b);
    ReadBoolSetting(&g_AdvertisingId, &b);
    ReadBoolSetting(&g_LocationService, &b);
    ReadBoolSetting(&g_NotificationsEnabled, &b);
    ReadBoolSetting(&g_DndMode, &b);
    ReadStringSetting(&g_WallpaperPath, buf, sizeof(buf));
    ReadStringSetting(&g_DesktopColor, buf, sizeof(buf));
    ReadStringSetting(&g_ComputerName, buf, sizeof(buf));
    ReadStringSetting(&g_RegisteredOwner, buf, sizeof(buf));
    ReadStringSetting(&g_RegisteredOrg, buf, sizeof(buf));
    ReadStringSetting(&g_ProductId, buf, sizeof(buf));

    /* Persist current effective values back to registry so all settings
     * have entries even on first boot. */
    WriteBoolSetting(&g_NightLight, g_NightLight.CachedValue);
    WriteBoolSetting(&g_HdrEnabled, g_HdrEnabled.CachedValue);
    WriteBoolSetting(&g_Mute, g_Mute.CachedValue);
    WriteDwordSetting(&g_ResolutionWidth, g_ResolutionWidth.CachedValue);
    WriteDwordSetting(&g_ResolutionHeight, g_ResolutionHeight.CachedValue);
    WriteDwordSetting(&g_RefreshRate, g_RefreshRate.CachedValue);
    WriteDwordSetting(&g_DpiScale, g_DpiScale.CachedValue);
    WriteDwordSetting(&g_MasterVolume, g_MasterVolume.CachedValue);
    WriteDwordSetting(&g_KeyboardRepeatRate, g_KeyboardRepeatRate.CachedValue);
    WriteDwordSetting(&g_KeyboardRepeatDelay, g_KeyboardRepeatDelay.CachedValue);
    WriteDwordSetting(&g_MouseSpeed, g_MouseSpeed.CachedValue);
    WriteDwordSetting(&g_DoubleClickTime, g_DoubleClickTime.CachedValue);
    WriteBoolSetting(&g_SwapMouseButtons, g_SwapMouseButtons.CachedValue);
    WriteDwordSetting(&g_SleepTimeoutAC, g_SleepTimeoutAC.CachedValue);
    WriteDwordSetting(&g_SleepTimeoutDC, g_SleepTimeoutDC.CachedValue);
    WriteDwordSetting(&g_ScreenTimeoutAC, g_ScreenTimeoutAC.CachedValue);
    WriteDwordSetting(&g_ScreenTimeoutDC, g_ScreenTimeoutDC.CachedValue);
    WriteDwordSetting(&g_PowerButtonAction, g_PowerButtonAction.CachedValue);
    WriteBoolSetting(&g_FastStartup, g_FastStartup.CachedValue);
    WriteDwordSetting(&g_TimeZoneBias, g_TimeZoneBias.CachedValue);
    WriteDwordSetting(&g_TimeFormat, g_TimeFormat.CachedValue);
    WriteDwordSetting(&g_DateFormat, g_DateFormat.CachedValue);
    WriteBoolSetting(&g_AutoTimeSync, g_AutoTimeSync.CachedValue);
    WriteBoolSetting(&g_HighContrast, g_HighContrast.CachedValue);
    WriteBoolSetting(&g_StickyKeys, g_StickyKeys.CachedValue);
    WriteBoolSetting(&g_FilterKeys, g_FilterKeys.CachedValue);
    WriteBoolSetting(&g_ToggleKeys, g_ToggleKeys.CachedValue);
    WriteBoolSetting(&g_MouseKeys, g_MouseKeys.CachedValue);
    WriteBoolSetting(&g_TelemetryEnabled, g_TelemetryEnabled.CachedValue);
    WriteBoolSetting(&g_AdvertisingId, g_AdvertisingId.CachedValue);
    WriteBoolSetting(&g_LocationService, g_LocationService.CachedValue);
    WriteBoolSetting(&g_NotificationsEnabled, g_NotificationsEnabled.CachedValue);
    WriteBoolSetting(&g_DndMode, g_DndMode.CachedValue);
    WriteStringSetting(&g_WallpaperPath, g_WallpaperPath.CachedValue);
    WriteStringSetting(&g_DesktopColor, g_DesktopColor.CachedValue);
    WriteStringSetting(&g_ComputerName, g_ComputerName.CachedValue);
    WriteStringSetting(&g_RegisteredOwner, g_RegisteredOwner.CachedValue);
    WriteStringSetting(&g_RegisteredOrg, g_RegisteredOrg.CachedValue);
    WriteStringSetting(&g_ProductId, g_ProductId.CachedValue);

    DbgPrint("SETTINGS: %u bool, %u dword, %u string settings ready\n",
             0, 0, 0); /* exact count computed at runtime below */

    return STATUS_SUCCESS;
}

/* ---- Public settings APIs ---------------------------------------------- */

NTSTATUS NTAPI SettingsGetMasterVolume(PULONG pValue) { return ReadDwordSetting(&g_MasterVolume, pValue); }
NTSTATUS NTAPI SettingsSetMasterVolume(ULONG Value) { return WriteDwordSetting(&g_MasterVolume, Value); }
NTSTATUS NTAPI SettingsGetMute(PBOOL pValue) { return ReadBoolSetting(&g_Mute, pValue); }
NTSTATUS NTAPI SettingsSetMute(BOOL Value) { return WriteBoolSetting(&g_Mute, Value); }

NTSTATUS NTAPI SettingsGetResolution(PULONG pWidth, PULONG pHeight)
{
    ULONG w, h;
    NTSTATUS s1, s2;
    s1 = ReadDwordSetting(&g_ResolutionWidth, &w);
    s2 = ReadDwordSetting(&g_ResolutionHeight, &h);
    if (pWidth) *pWidth = w;
    if (pHeight) *pHeight = h;
    return (NT_SUCCESS(s1) && NT_SUCCESS(s2)) ? STATUS_SUCCESS : s1;
}
NTSTATUS NTAPI SettingsSetResolution(ULONG Width, ULONG Height)
{
    NTSTATUS s1 = WriteDwordSetting(&g_ResolutionWidth, Width);
    NTSTATUS s2 = WriteDwordSetting(&g_ResolutionHeight, Height);
    return (NT_SUCCESS(s1) && NT_SUCCESS(s2)) ? STATUS_SUCCESS : s1;
}

NTSTATUS NTAPI SettingsGetRefreshRate(PULONG pValue) { return ReadDwordSetting(&g_RefreshRate, pValue); }
NTSTATUS NTAPI SettingsSetRefreshRate(ULONG Value) { return WriteDwordSetting(&g_RefreshRate, Value); }

NTSTATUS NTAPI SettingsGetDpiScale(PULONG pValue) { return ReadDwordSetting(&g_DpiScale, pValue); }
NTSTATUS NTAPI SettingsSetDpiScale(ULONG Value) { return WriteDwordSetting(&g_DpiScale, Value); }

NTSTATUS NTAPI SettingsGetNightLight(PBOOL pValue) { return ReadBoolSetting(&g_NightLight, pValue); }
NTSTATUS NTAPI SettingsSetNightLight(BOOL Value) { return WriteBoolSetting(&g_NightLight, Value); }

NTSTATUS NTAPI SettingsGetHdrEnabled(PBOOL pValue) { return ReadBoolSetting(&g_HdrEnabled, pValue); }
NTSTATUS NTAPI SettingsSetHdrEnabled(BOOL Value) { return WriteBoolSetting(&g_HdrEnabled, Value); }

NTSTATUS NTAPI SettingsGetKeyboardRepeatRate(PULONG pValue) { return ReadDwordSetting(&g_KeyboardRepeatRate, pValue); }
NTSTATUS NTAPI SettingsSetKeyboardRepeatRate(ULONG Value) { return WriteDwordSetting(&g_KeyboardRepeatRate, Value); }

NTSTATUS NTAPI SettingsGetKeyboardRepeatDelay(PULONG pValue) { return ReadDwordSetting(&g_KeyboardRepeatDelay, pValue); }
NTSTATUS NTAPI SettingsSetKeyboardRepeatDelay(ULONG Value) { return WriteDwordSetting(&g_KeyboardRepeatDelay, Value); }

NTSTATUS NTAPI SettingsGetMouseSpeed(PULONG pValue) { return ReadDwordSetting(&g_MouseSpeed, pValue); }
NTSTATUS NTAPI SettingsSetMouseSpeed(ULONG Value) { return WriteDwordSetting(&g_MouseSpeed, Value); }

NTSTATUS NTAPI SettingsGetDoubleClickTime(PULONG pValue) { return ReadDwordSetting(&g_DoubleClickTime, pValue); }
NTSTATUS NTAPI SettingsSetDoubleClickTime(ULONG Value) { return WriteDwordSetting(&g_DoubleClickTime, Value); }

NTSTATUS NTAPI SettingsGetSwapMouseButtons(PBOOL pValue) { return ReadBoolSetting(&g_SwapMouseButtons, pValue); }
NTSTATUS NTAPI SettingsSetSwapMouseButtons(BOOL Value) { return WriteBoolSetting(&g_SwapMouseButtons, Value); }

NTSTATUS NTAPI SettingsGetSleepTimeoutAC(PULONG pValue) { return ReadDwordSetting(&g_SleepTimeoutAC, pValue); }
NTSTATUS NTAPI SettingsSetSleepTimeoutAC(ULONG Value) { return WriteDwordSetting(&g_SleepTimeoutAC, Value); }

NTSTATUS NTAPI SettingsGetSleepTimeoutDC(PULONG pValue) { return ReadDwordSetting(&g_SleepTimeoutDC, pValue); }
NTSTATUS NTAPI SettingsSetSleepTimeoutDC(ULONG Value) { return WriteDwordSetting(&g_SleepTimeoutDC, Value); }

NTSTATUS NTAPI SettingsGetScreenTimeoutAC(PULONG pValue) { return ReadDwordSetting(&g_ScreenTimeoutAC, pValue); }
NTSTATUS NTAPI SettingsSetScreenTimeoutAC(ULONG Value) { return WriteDwordSetting(&g_ScreenTimeoutAC, Value); }

NTSTATUS NTAPI SettingsGetScreenTimeoutDC(PULONG pValue) { return ReadDwordSetting(&g_ScreenTimeoutDC, pValue); }
NTSTATUS NTAPI SettingsSetScreenTimeoutDC(ULONG Value) { return WriteDwordSetting(&g_ScreenTimeoutDC, Value); }

NTSTATUS NTAPI SettingsGetPowerButtonAction(PULONG pValue) { return ReadDwordSetting(&g_PowerButtonAction, pValue); }
NTSTATUS NTAPI SettingsSetPowerButtonAction(ULONG Value) { return WriteDwordSetting(&g_PowerButtonAction, Value); }

NTSTATUS NTAPI SettingsGetFastStartup(PBOOL pValue) { return ReadBoolSetting(&g_FastStartup, pValue); }
NTSTATUS NTAPI SettingsSetFastStartup(BOOL Value) { return WriteBoolSetting(&g_FastStartup, Value); }

NTSTATUS NTAPI SettingsGetTimeZoneBias(PULONG pValue) { return ReadDwordSetting(&g_TimeZoneBias, pValue); }
NTSTATUS NTAPI SettingsSetTimeZoneBias(ULONG Value) { return WriteDwordSetting(&g_TimeZoneBias, Value); }

NTSTATUS NTAPI SettingsGetTimeFormat(PULONG pValue) { return ReadDwordSetting(&g_TimeFormat, pValue); }
NTSTATUS NTAPI SettingsSetTimeFormat(ULONG Value) { return WriteDwordSetting(&g_TimeFormat, Value); }

NTSTATUS NTAPI SettingsGetDateFormat(PULONG pValue) { return ReadDwordSetting(&g_DateFormat, pValue); }
NTSTATUS NTAPI SettingsSetDateFormat(ULONG Value) { return WriteDwordSetting(&g_DateFormat, Value); }

NTSTATUS NTAPI SettingsGetAutoTimeSync(PBOOL pValue) { return ReadBoolSetting(&g_AutoTimeSync, pValue); }
NTSTATUS NTAPI SettingsSetAutoTimeSync(BOOL Value) { return WriteBoolSetting(&g_AutoTimeSync, Value); }

NTSTATUS NTAPI SettingsGetHighContrast(PBOOL pValue) { return ReadBoolSetting(&g_HighContrast, pValue); }
NTSTATUS NTAPI SettingsSetHighContrast(BOOL Value) { return WriteBoolSetting(&g_HighContrast, Value); }

NTSTATUS NTAPI SettingsGetStickyKeys(PBOOL pValue) { return ReadBoolSetting(&g_StickyKeys, pValue); }
NTSTATUS NTAPI SettingsSetStickyKeys(BOOL Value) { return WriteBoolSetting(&g_StickyKeys, Value); }

NTSTATUS NTAPI SettingsGetFilterKeys(PBOOL pValue) { return ReadBoolSetting(&g_FilterKeys, pValue); }
NTSTATUS NTAPI SettingsSetFilterKeys(BOOL Value) { return WriteBoolSetting(&g_FilterKeys, Value); }

NTSTATUS NTAPI SettingsGetToggleKeys(PBOOL pValue) { return ReadBoolSetting(&g_ToggleKeys, pValue); }
NTSTATUS NTAPI SettingsSetToggleKeys(BOOL Value) { return WriteBoolSetting(&g_ToggleKeys, Value); }

NTSTATUS NTAPI SettingsGetMouseKeys(PBOOL pValue) { return ReadBoolSetting(&g_MouseKeys, pValue); }
NTSTATUS NTAPI SettingsSetMouseKeys(BOOL Value) { return WriteBoolSetting(&g_MouseKeys, Value); }

NTSTATUS NTAPI SettingsGetTelemetryEnabled(PBOOL pValue) { return ReadBoolSetting(&g_TelemetryEnabled, pValue); }
NTSTATUS NTAPI SettingsSetTelemetryEnabled(BOOL Value) { return WriteBoolSetting(&g_TelemetryEnabled, Value); }

NTSTATUS NTAPI SettingsGetAdvertisingId(PBOOL pValue) { return ReadBoolSetting(&g_AdvertisingId, pValue); }
NTSTATUS NTAPI SettingsSetAdvertisingId(BOOL Value) { return WriteBoolSetting(&g_AdvertisingId, Value); }

NTSTATUS NTAPI SettingsGetLocationService(PBOOL pValue) { return ReadBoolSetting(&g_LocationService, pValue); }
NTSTATUS NTAPI SettingsSetLocationService(BOOL Value) { return WriteBoolSetting(&g_LocationService, Value); }

NTSTATUS NTAPI SettingsGetNotificationsEnabled(PBOOL pValue) { return ReadBoolSetting(&g_NotificationsEnabled, pValue); }
NTSTATUS NTAPI SettingsSetNotificationsEnabled(BOOL Value) { return WriteBoolSetting(&g_NotificationsEnabled, Value); }

NTSTATUS NTAPI SettingsGetDndMode(PBOOL pValue) { return ReadBoolSetting(&g_DndMode, pValue); }
NTSTATUS NTAPI SettingsSetDndMode(BOOL Value) { return WriteBoolSetting(&g_DndMode, Value); }

NTSTATUS NTAPI SettingsGetWallpaperPath(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_WallpaperPath, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetWallpaperPath(PCWSTR Value) { return WriteStringSetting(&g_WallpaperPath, Value); }

NTSTATUS NTAPI SettingsGetDesktopColor(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_DesktopColor, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetDesktopColor(PCWSTR Value) { return WriteStringSetting(&g_DesktopColor, Value); }

NTSTATUS NTAPI SettingsGetComputerName(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_ComputerName, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetComputerName(PCWSTR Value) { return WriteStringSetting(&g_ComputerName, Value); }

NTSTATUS NTAPI SettingsGetRegisteredOwner(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_RegisteredOwner, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetRegisteredOwner(PCWSTR Value) { return WriteStringSetting(&g_RegisteredOwner, Value); }

NTSTATUS NTAPI SettingsGetRegisteredOrg(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_RegisteredOrg, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetRegisteredOrg(PCWSTR Value) { return WriteStringSetting(&g_RegisteredOrg, Value); }

NTSTATUS NTAPI SettingsGetProductId(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_ProductId, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetProductId(PCWSTR Value) { return WriteStringSetting(&g_ProductId, Value); }

/* ---- Screensaver accessors ---- */

NTSTATUS NTAPI SettingsGetScreensaverPath(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_ScreensaverPath, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetScreensaverPath(PCWSTR Value) { return WriteStringSetting(&g_ScreensaverPath, Value); }

NTSTATUS NTAPI SettingsGetScreensaverTimeout(PULONG pValue) { return ReadDwordSetting(&g_ScreensaverTimeout, pValue); }
NTSTATUS NTAPI SettingsSetScreensaverTimeout(ULONG Value) { return WriteDwordSetting(&g_ScreensaverTimeout, Value); }

NTSTATUS NTAPI SettingsGetScreensaverSecure(PBOOL pValue) { return ReadBoolSetting(&g_ScreensaverSecure, pValue); }
NTSTATUS NTAPI SettingsSetScreensaverSecure(BOOL Value) { return WriteBoolSetting(&g_ScreensaverSecure, Value); }

NTSTATUS NTAPI SettingsGetScreensaverEnabled(PBOOL pValue) { return ReadBoolSetting(&g_ScreensaverEnabled, pValue); }
NTSTATUS NTAPI SettingsSetScreensaverEnabled(BOOL Value) { return WriteBoolSetting(&g_ScreensaverEnabled, Value); }

/* ---- User profile / avatar ---- */

NTSTATUS NTAPI SettingsGetUserProfilePicture(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_UserProfilePicture, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetUserProfilePicture(PCWSTR Value) { return WriteStringSetting(&g_UserProfilePicture, Value); }

NTSTATUS NTAPI SettingsGetUserDisplayName(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_UserDisplayName, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetUserDisplayName(PCWSTR Value) { return WriteStringSetting(&g_UserDisplayName, Value); }

/* ---- Taskbar / Start menu ---- */

NTSTATUS NTAPI SettingsGetTaskbarPosition(PULONG pValue) { return ReadDwordSetting(&g_TaskbarPosition, pValue); }
NTSTATUS NTAPI SettingsSetTaskbarPosition(ULONG Value) { return WriteDwordSetting(&g_TaskbarPosition, Value); }

NTSTATUS NTAPI SettingsGetTaskbarAutoHide(PULONG pValue) { return ReadDwordSetting(&g_TaskbarAutoHide, pValue); }
NTSTATUS NTAPI SettingsSetTaskbarAutoHide(ULONG Value) { return WriteDwordSetting(&g_TaskbarAutoHide, Value); }

NTSTATUS NTAPI SettingsGetTaskbarSmallIcons(PULONG pValue) { return ReadDwordSetting(&g_TaskbarSmallIcons, pValue); }
NTSTATUS NTAPI SettingsSetTaskbarSmallIcons(ULONG Value) { return WriteDwordSetting(&g_TaskbarSmallIcons, Value); }

NTSTATUS NTAPI SettingsGetTaskbarCombine(PULONG pValue) { return ReadDwordSetting(&g_TaskbarCombine, pValue); }
NTSTATUS NTAPI SettingsSetTaskbarCombine(ULONG Value) { return WriteDwordSetting(&g_TaskbarCombine, Value); }

NTSTATUS NTAPI SettingsGetShowStartButton(PBOOL pValue) { return ReadBoolSetting(&g_ShowStartButton, pValue); }
NTSTATUS NTAPI SettingsSetShowStartButton(BOOL Value) { return WriteBoolSetting(&g_ShowStartButton, Value); }

NTSTATUS NTAPI SettingsGetStartButtonLabel(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_StartButtonLabel, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetStartButtonLabel(PCWSTR Value) { return WriteStringSetting(&g_StartButtonLabel, Value); }

NTSTATUS NTAPI SettingsGetStartMenuStyle(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_StartMenuStyle, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetStartMenuStyle(PCWSTR Value) { return WriteStringSetting(&g_StartMenuStyle, Value); }

NTSTATUS NTAPI SettingsGetShowRunCommand(PBOOL pValue) { return ReadBoolSetting(&g_ShowRunCommand, pValue); }
NTSTATUS NTAPI SettingsSetShowRunCommand(BOOL Value) { return WriteBoolSetting(&g_ShowRunCommand, Value); }

NTSTATUS NTAPI SettingsGetShowSearchBox(PBOOL pValue) { return ReadBoolSetting(&g_ShowSearchBox, pValue); }
NTSTATUS NTAPI SettingsSetShowSearchBox(BOOL Value) { return WriteBoolSetting(&g_ShowSearchBox, Value); }

NTSTATUS NTAPI SettingsGetShowMyComputer(PBOOL pValue) { return ReadBoolSetting(&g_ShowMyComputer, pValue); }
NTSTATUS NTAPI SettingsSetShowMyComputer(BOOL Value) { return WriteBoolSetting(&g_ShowMyComputer, Value); }

NTSTATUS NTAPI SettingsGetShowMyDocuments(PBOOL pValue) { return ReadBoolSetting(&g_ShowMyDocuments, pValue); }
NTSTATUS NTAPI SettingsSetShowMyDocuments(BOOL Value) { return WriteBoolSetting(&g_ShowMyDocuments, Value); }

NTSTATUS NTAPI SettingsGetShowControlPanel(PBOOL pValue) { return ReadBoolSetting(&g_ShowControlPanel, pValue); }
NTSTATUS NTAPI SettingsSetShowControlPanel(BOOL Value) { return WriteBoolSetting(&g_ShowControlPanel, Value); }

NTSTATUS NTAPI SettingsGetShowRecycleBin(PBOOL pValue) { return ReadBoolSetting(&g_ShowRecycleBin, pValue); }
NTSTATUS NTAPI SettingsSetShowRecycleBin(BOOL Value) { return WriteBoolSetting(&g_ShowRecycleBin, Value); }

/* ---- File Explorer ---- */

NTSTATUS NTAPI SettingsGetShowHiddenFiles(PBOOL pValue) { return ReadBoolSetting(&g_ShowHiddenFiles, pValue); }
NTSTATUS NTAPI SettingsSetShowHiddenFiles(BOOL Value) { return WriteBoolSetting(&g_ShowHiddenFiles, Value); }

NTSTATUS NTAPI SettingsGetShowFileExtensions(PBOOL pValue) { return ReadBoolSetting(&g_ShowFileExtensions, pValue); }
NTSTATUS NTAPI SettingsSetShowFileExtensions(BOOL Value) { return WriteBoolSetting(&g_ShowFileExtensions, Value); }

NTSTATUS NTAPI SettingsGetShowProtectedOSFiles(PBOOL pValue) { return ReadBoolSetting(&g_ShowProtectedOSFiles, pValue); }
NTSTATUS NTAPI SettingsSetShowProtectedOSFiles(BOOL Value) { return WriteBoolSetting(&g_ShowProtectedOSFiles, Value); }

NTSTATUS NTAPI SettingsGetLaunchFolderWindows(PBOOL pValue) { return ReadBoolSetting(&g_LaunchFolderWindows, pValue); }
NTSTATUS NTAPI SettingsSetLaunchFolderWindows(BOOL Value) { return WriteBoolSetting(&g_LaunchFolderWindows, Value); }

NTSTATUS NTAPI SettingsGetDefaultFolderView(PULONG pValue) { return ReadDwordSetting(&g_DefaultFolderView, pValue); }
NTSTATUS NTAPI SettingsSetDefaultFolderView(ULONG Value) { return WriteDwordSetting(&g_DefaultFolderView, Value); }

/* ---- Terminal / shell defaults ---- */

NTSTATUS NTAPI SettingsGetDefaultTerminal(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_DefaultTerminal, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetDefaultTerminal(PCWSTR Value) { return WriteStringSetting(&g_DefaultTerminal, Value); }

NTSTATUS NTAPI SettingsGetDefaultShell(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_DefaultShell, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetDefaultShell(PCWSTR Value) { return WriteStringSetting(&g_DefaultShell, Value); }

NTSTATUS NTAPI SettingsGetPowerShellPath(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_PowerShellPath, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetPowerShellPath(PCWSTR Value) { return WriteStringSetting(&g_PowerShellPath, Value); }

/* ---- Theme ---- */

NTSTATUS NTAPI SettingsGetThemeName(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_ThemeName, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetThemeName(PCWSTR Value) { return WriteStringSetting(&g_ThemeName, Value); }

NTSTATUS NTAPI SettingsGetAccentColor(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_AccentColor, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetAccentColor(PCWSTR Value) { return WriteStringSetting(&g_AccentColor, Value); }

NTSTATUS NTAPI SettingsGetEnableTransparency(PBOOL pValue) { return ReadBoolSetting(&g_EnableTransparency, pValue); }
NTSTATUS NTAPI SettingsSetEnableTransparency(BOOL Value) { return WriteBoolSetting(&g_EnableTransparency, Value); }

/* ---- Customization (PowerToys-style) ---- */

NTSTATUS NTAPI SettingsGetFancyZonesEnabled(PBOOL pValue) { return ReadBoolSetting(&g_FancyZonesEnabled, pValue); }
NTSTATUS NTAPI SettingsSetFancyZonesEnabled(BOOL Value) { return WriteBoolSetting(&g_FancyZonesEnabled, Value); }

NTSTATUS NTAPI SettingsGetAlwaysOnTopEnabled(PBOOL pValue) { return ReadBoolSetting(&g_AlwaysOnTopEnabled, pValue); }
NTSTATUS NTAPI SettingsSetAlwaysOnTopEnabled(BOOL Value) { return WriteBoolSetting(&g_AlwaysOnTopEnabled, Value); }

NTSTATUS NTAPI SettingsGetColorPickerEnabled(PBOOL pValue) { return ReadBoolSetting(&g_ColorPickerEnabled, pValue); }
NTSTATUS NTAPI SettingsSetColorPickerEnabled(BOOL Value) { return WriteBoolSetting(&g_ColorPickerEnabled, Value); }

NTSTATUS NTAPI SettingsGetPowerRenameEnabled(PBOOL pValue) { return ReadBoolSetting(&g_PowerRenameEnabled, pValue); }
NTSTATUS NTAPI SettingsSetPowerRenameEnabled(BOOL Value) { return WriteBoolSetting(&g_PowerRenameEnabled, Value); }

NTSTATUS NTAPI SettingsGetKeyboardManagerEnabled(PBOOL pValue) { return ReadBoolSetting(&g_KeyboardManagerEnabled, pValue); }
NTSTATUS NTAPI SettingsSetKeyboardManagerEnabled(BOOL Value) { return WriteBoolSetting(&g_KeyboardManagerEnabled, Value); }

NTSTATUS NTAPI SettingsGetLightSwitchEnabled(PBOOL pValue) { return ReadBoolSetting(&g_LightSwitchEnabled, pValue); }
NTSTATUS NTAPI SettingsSetLightSwitchEnabled(BOOL Value) { return WriteBoolSetting(&g_LightSwitchEnabled, Value); }

/* ---- Security / privacy ---- */

NTSTATUS NTAPI SettingsGetTpmRequired(PBOOL pValue) { return ReadBoolSetting(&g_TpmRequired, pValue); }
NTSTATUS NTAPI SettingsSetTpmRequired(BOOL Value) { return WriteBoolSetting(&g_TpmRequired, Value); }

NTSTATUS NTAPI SettingsGetSecureBootRequired(PBOOL pValue) { return ReadBoolSetting(&g_SecureBootRequired, pValue); }
NTSTATUS NTAPI SettingsSetSecureBootRequired(BOOL Value) { return WriteBoolSetting(&g_SecureBootRequired, Value); }

NTSTATUS NTAPI SettingsGetRequireOnlineAccount(PBOOL pValue) { return ReadBoolSetting(&g_RequireOnlineAccount, pValue); }
NTSTATUS NTAPI SettingsSetRequireOnlineAccount(BOOL Value) { return WriteBoolSetting(&g_RequireOnlineAccount, Value); }

NTSTATUS NTAPI SettingsGetCortanaEnabled(PBOOL pValue) { return ReadBoolSetting(&g_CortanaEnabled, pValue); }
NTSTATUS NTAPI SettingsSetCortanaEnabled(BOOL Value) { return WriteBoolSetting(&g_CortanaEnabled, Value); }

NTSTATUS NTAPI SettingsGetShowSyncNotifications(PBOOL pValue) { return ReadBoolSetting(&g_ShowSyncNotifications, pValue); }
NTSTATUS NTAPI SettingsSetShowSyncNotifications(BOOL Value) { return WriteBoolSetting(&g_ShowSyncNotifications, Value); }

/* ---- Cosmetic: title bars, window borders ---- */

NTSTATUS NTAPI SettingsGetWindowBorderWidth(PULONG pValue) { return ReadDwordSetting(&g_WindowBorderWidth, pValue); }
NTSTATUS NTAPI SettingsSetWindowBorderWidth(ULONG Value) { return WriteDwordSetting(&g_WindowBorderWidth, Value); }

NTSTATUS NTAPI SettingsGetWindowCaptionHeight(PULONG pValue) { return ReadDwordSetting(&g_WindowCaptionHeight, pValue); }
NTSTATUS NTAPI SettingsSetWindowCaptionHeight(ULONG Value) { return WriteDwordSetting(&g_WindowCaptionHeight, Value); }

NTSTATUS NTAPI SettingsGetMenuHeight(PULONG pValue) { return ReadDwordSetting(&g_MenuHeight, pValue); }
NTSTATUS NTAPI SettingsSetMenuHeight(ULONG Value) { return WriteDwordSetting(&g_MenuHeight, Value); }

NTSTATUS NTAPI SettingsGetCursorScheme(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_CursorScheme, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetCursorScheme(PCWSTR Value) { return WriteStringSetting(&g_CursorScheme, Value); }

NTSTATUS NTAPI SettingsGetSoundScheme(PWCHAR pBuf, ULONG BufLen) { return ReadStringSetting(&g_SoundScheme, pBuf, BufLen); }
NTSTATUS NTAPI SettingsSetSoundScheme(PCWSTR Value) { return WriteStringSetting(&g_SoundScheme, Value); }

/* ---- Diagnostic / feedback ---- */

NTSTATUS NTAPI SettingsGetErrorReporting(PBOOL pValue) { return ReadBoolSetting(&g_ErrorReporting, pValue); }
NTSTATUS NTAPI SettingsSetErrorReporting(BOOL Value) { return WriteBoolSetting(&g_ErrorReporting, Value); }

NTSTATUS NTAPI SettingsGetFeedbackFrequency(PBOOL pValue) { return ReadBoolSetting(&g_FeedbackFrequency, pValue); }
NTSTATUS NTAPI SettingsSetFeedbackFrequency(BOOL Value) { return WriteBoolSetting(&g_FeedbackFrequency, Value); }

NTSTATUS NTAPI SettingsGetActivityHistory(PBOOL pValue) { return ReadBoolSetting(&g_ActivityHistory, pValue); }
NTSTATUS NTAPI SettingsSetActivityHistory(BOOL Value) { return WriteBoolSetting(&g_ActivityHistory, Value); }

NTSTATUS NTAPI SettingsGetDiagnosticData(PBOOL pValue) { return ReadBoolSetting(&g_DiagnosticData, pValue); }
NTSTATUS NTAPI SettingsSetDiagnosticData(BOOL Value) { return WriteBoolSetting(&g_DiagnosticData, Value); }

NTSTATUS NTAPI SettingsGetTailoredExperiences(PBOOL pValue) { return ReadBoolSetting(&g_TailoredExperiences, pValue); }
NTSTATUS NTAPI SettingsSetTailoredExperiences(BOOL Value) { return WriteBoolSetting(&g_TailoredExperiences, Value); }
