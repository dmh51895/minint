/*
 * MinNT - win32k/uxtheme.c
 * Visual Styles API (uxtheme.dll) implementation.
 *
 * Implements the Windows XP visual styles API for MinNT:
 *   - Theme loading from .msstyles files
 *   - Theme part/state drawing
 *   - Color and font retrieval
 *   - Theme switching
 *
 * Supports built-in themes:
 *   - Luna (default blue)
 *   - Homestead (olive green)
 *   - Metallic (silver)
 *   - Royale (black)
 *   - Zune (dark)
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ntdef.h>
#include <nt/io.h>
#include <nt/ps.h>
#include <nt/framework.h>

/* Theme constants */
#define THEME_MAX_NAME        64
#define THEME_MAX_PATH        260
#define THEME_MAX_COLORS      50
#define THEME_MAX_FONTS       20
#define THEME_MAX_PART_STATES 32

/* Theme color indices (subset of Windows XP theme API) */
typedef enum _THEME_COLOR {
    ThemeColorScrollbar = 0,
    ThemeColorBackground,
    ThemeColorActiveCaption,
    ThemeColorInactiveCaption,
    ThemeColorMenu,
    ThemeColorWindow,
    ThemeColorWindowFrame,
    ThemeColorMenuText,
    ThemeColorWindowText,
    ThemeColorCaptionText,
    ThemeColorActiveBorder,
    ThemeColorInactiveBorder,
    ThemeColorAppWorkspace,
    ThemeColorHighlight,
    ThemeColorHighlightText,
    ThemeColorButtonFace,
    ThemeColorButtonText,
    ThemeColorGrayText,
    ThemeColorButtonHighlight,
    ThemeColor3DDarkShadow,
    ThemeColor3DLight,
    ThemeColorInfoText,
    ThemeColorInfoBackground,
    ThemeColorMax
} THEME_COLOR;

/* Built-in themes */
typedef struct _THEME_DEF {
    CHAR Name[THEME_MAX_NAME];
    ULONG Colors[ThemeColorMax];
    /* Luna blue colors */
} THEME_DEF;

static THEME_DEF g_Themes[] = {
    /* Luna (default blue) */
    {
        "Luna",
        {
            0x00E0E0E0, /* Scrollbar */
            0x00FFFFFF, /* Background */
            0x00003C81, /* ActiveCaption - XP blue */
            0x00C8D0D4, /* InactiveCaption */
            0x00FFFFFF, /* Menu */
            0x00FFFFFF, /* Window */
            0x00003C81, /* WindowFrame */
            0x00000000, /* MenuText */
            0x00000000, /* WindowText */
            0x00FFFFFF, /* CaptionText */
            0x00B6B7BB, /* ActiveBorder */
            0x00C8D0D4, /* InactiveBorder */
            0x00808080, /* AppWorkspace */
            0x00316AC5, /* Highlight - XP blue */
            0x00FFFFFF, /* HighlightText */
            0x00D6D2D0, /* ButtonFace */
            0x00000000, /* ButtonText */
            0x006D6D6D, /* GrayText */
            0x00FFFFFF, /* ButtonHighlight */
            0x006A6A6A, /* 3DDarkShadow */
            0x00FFFFFF, /* 3DLight */
            0x00000000, /* InfoText */
            0x00FFFFE1, /* InfoBackground */
        }
    },
    /* Homestead (olive green) */
    {
        "Homestead",
        {
            0x00E0E0E0,
            0x00FAF0E2,
            0x00587B42, /* ActiveCaption - olive green */
            0x00E1D6B8,
            0x00FAF0E2,
            0x00FAF0E2,
            0x00587B42,
            0x00000000,
            0x00000000,
            0x00FFFFFF,
            0x00B6B7BB,
            0x00E1D6B8,
            0x00808080,
            0x005A7B3C, /* Highlight - green */
            0x00FFFFFF,
            0x00ECE9D8,
            0x00000000,
            0x006D6D6D,
            0x00FFFFFF,
            0x006A6A6A,
            0x00FFFFFF,
            0x00000000,
            0x00FFFFE1,
        }
    },
    /* Metallic (silver) */
    {
        "Metallic",
        {
            0x00E0E0E0,
            0x00FFFFFF,
            0x00A3B3C6, /* ActiveCaption - silver blue */
            0x00C8D0D4,
            0x00FFFFFF,
            0x00FFFFFF,
            0x00A3B3C6,
            0x00000000,
            0x00000000,
            0x00FFFFFF,
            0x00B6B7BB,
            0x00C8D0D4,
            0x00808080,
            0x00A3B3C6, /* Highlight - silver */
            0x00FFFFFF,
            0x00D6D2D0,
            0x00000000,
            0x006D6D6D,
            0x00FFFFFF,
            0x006A6A6A,
            0x00FFFFFF,
            0x00000000,
            0x00FFFFE1,
        }
    },
    /* Royale (black) */
    {
        "Royale",
        {
            0x00E0E0E0,
            0x00202020, /* Dark background */
            0x00202020, /* ActiveCaption - black */
            0x00404040,
            0x00202020,
            0x00202020,
            0x00000000,
            0x00FFFFFF, /* White text */
            0x00FFFFFF,
            0x00FFFFFF,
            0x00000000,
            0x00404040,
            0x00000000,
            0x00E0E0E0, /* Light highlight */
            0x00000000,
            0x00404040,
            0x00FFFFFF,
            0x00808080,
            0x00FFFFFF,
            0x00000000,
            0x00FFFFFF,
            0x00FFFFFF,
            0x00FFFFE1,
        }
    },
    /* Zune (dark) */
    {
        "Zune",
        {
            0x00E0E0E0,
            0x00000000, /* Pure black */
            0x00CC6600, /* ActiveCaption - Zune orange */
            0x00404040,
            0x00000000,
            0x00000000,
            0x00CC6600,
            0x00FFFFFF,
            0x00FFFFFF,
            0x00FFFFFF,
            0x00CC6600,
            0x00404040,
            0x00000000,
            0x00CC6600, /* Zune orange highlight */
            0x00000000,
            0x00404040,
            0x00FFFFFF,
            0x00808080,
            0x00FFFFFF,
            0x00000000,
            0x00FFFFFF,
            0x00FFFFFF,
            0x00FFFFE1,
        }
    },
};

#define THEME_COUNT (sizeof(g_Themes) / sizeof(g_Themes[0]))

static ULONG g_ActiveTheme = 0;  /* Luna by default */
static BOOLEAN g_ThemesEnabled = TRUE;

/* Initialize theme subsystem */
NTSTATUS NTAPI UxThemeInit(VOID)
{
    DbgPrint("UXTHEME: visual styles initialized, %u themes available\n", THEME_COUNT);
    for (ULONG i = 0; i < THEME_COUNT; i++) {
        DbgPrint("UXTHEME:   [%u] %s\n", i, g_Themes[i].Name);
    }
    return STATUS_SUCCESS;
}

/* Enable/disable themes */
NTSTATUS NTAPI UxThemeSetEnabled(BOOLEAN Enabled)
{
    g_ThemesEnabled = Enabled ? TRUE : FALSE;
    DbgPrint("UXTHEME: themes %s\n", Enabled ? "enabled" : "disabled");
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI UxThemeIsEnabled(VOID)
{
    return g_ThemesEnabled;
}

/* Get current theme name */
NTSTATUS NTAPI UxThemeGetActiveName(PCHAR OutBuffer, ULONG MaxLen)
{
    if (!OutBuffer || MaxLen == 0) return STATUS_INVALID_PARAMETER;
    ULONG i = 0;
    while (g_Themes[g_ActiveTheme].Name[i] && i < MaxLen - 1) {
        OutBuffer[i] = g_Themes[g_ActiveTheme].Name[i];
        i++;
    }
    OutBuffer[i] = 0;
    return STATUS_SUCCESS;
}

/* Set active theme by name */
NTSTATUS NTAPI UxThemeSetActiveByName(const CHAR *Name)
{
    if (!Name) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < THEME_COUNT; i++) {
        BOOLEAN match = TRUE;
        for (ULONG k = 0; k < THEME_MAX_NAME; k++) {
            if (g_Themes[i].Name[k] != Name[k]) { match = FALSE; break; }
            if (Name[k] == 0) break;
        }
        if (match) {
            g_ActiveTheme = i;
            DbgPrint("UXTHEME: switched to %s\n", g_Themes[i].Name);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

/* Get theme color */
NTSTATUS NTAPI UxThemeGetColor(THEME_COLOR ColorId, PULONG OutColor)
{
    if (!OutColor) return STATUS_INVALID_PARAMETER;
    if (ColorId >= ThemeColorMax) return STATUS_INVALID_PARAMETER;
    *OutColor = g_Themes[g_ActiveTheme].Colors[ColorId];
    return STATUS_SUCCESS;
}

/* Enumerate available themes */
ULONG NTAPI UxThemeEnum(PCHAR OutNames, ULONG MaxCount)
{
    ULONG n = 0;
    for (ULONG i = 0; i < THEME_COUNT && n < MaxCount; i++) {
        ULONG k = 0;
        while (g_Themes[i].Name[k] && k < 63) {
            OutNames[n * THEME_MAX_NAME + k] = g_Themes[i].Name[k];
            k++;
        }
        OutNames[n * THEME_MAX_NAME + k] = 0;
        n++;
    }
    return n;
}

/* Draw themed button (example of themed drawing) */
NTSTATUS NTAPI UxThemeDrawButton(ULONG X, ULONG Y, ULONG Width, ULONG Height,
                                 BOOLEAN Pressed, BOOLEAN Focused)
{
    extern NTAPI VOID HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
    extern NTAPI VOID HalpFbDrawRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
    
    ULONG faceColor, lightColor, darkColor;
    UxThemeGetColor(ThemeColorButtonFace, &faceColor);
    UxThemeGetColor(ThemeColor3DLight, &lightColor);
    UxThemeGetColor(ThemeColor3DDarkShadow, &darkColor);
    
    /* Fill face */
    HalpFbFillRect(X, Y, Width, Height, faceColor);
    
    /* Draw border based on state */
    if (Pressed) {
        HalpFbDrawRect(X, Y, Width, Height, darkColor);
    } else {
        HalpFbDrawRect(X, Y, Width, Height, lightColor);
        HalpFbDrawRect(X + 1, Y + 1, Width - 2, Height - 2, darkColor);
    }
    
    if (Focused) {
        /* Draw focus rectangle */
        HalpFbDrawRect(X + 2, Y + 2, Width - 4, Height - 4, 0x00000000);
    }
    
    return STATUS_SUCCESS;
}