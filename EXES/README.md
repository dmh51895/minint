# Test Windows Executables

This folder contains real Windows executables used for testing MinNT's PE loader and WINE integration.

**Note:** These files are NOT included in the git repository due to GitHub's 100MB file size limit. They are kept locally for testing purposes.

## Contents

| File | Size | Type |
|------|------|------|
| DiscordSetup.exe | 83MB | x64 installer |
| dotnetfx48.exe | 121MB | x64 installer |
| OBS-Studio-27.2.4-Full-Installer-x64.exe | 117MB | x64 installer |
| Photoshop_Portable_CS6_Multi.exe | 100MB | x86 installer |
| python-3.13.7-amd64.exe | 28MB | x64 installer |
| SteamSetup.exe | 2.2MB | x64 installer |
| tsetup-x64.6.1.3.exe | 49MB | x64 installer |
| vlc-3.0.17.4-win32.exe | 42MB | x86 installer (tests WINE) |
| VSCodeUserSetup-x64-1.104.0.exe | 115MB | x64 installer |

## Testing

These executables are used to test:
- **Native PE loading** (x64 binaries run natively)
- **WINE routing** (x86 binaries route to WINE)
- **MSI execution** (installers)
- **PE format detection** (AMD64 vs i386)

## How to Test

1. Boot MinNT
2. Copy an EXE to the filesystem
3. Double-click it in Explorer
4. The shell will:
   - Detect the PE format
   - Route to native loader (x64) or WINE (x86)
   - Execute the binary

## Download

To get these test executables, download them from their official sources:
- Discord: https://discord.com/download
- Python: https://python.org/downloads
- VSCode: https://code.visualstudio.com/download
- VLC: https://videolan.org/vlc
- Steam: https://store.steampowered.com/about/downloads
- OBS: https://obsproject.com/download
- Photoshop: Adobe Creative Cloud
- .NET Framework 4.8: Microsoft Download Center

**Or use any Windows .exe you have!** The PE loader supports all standard Windows executables.