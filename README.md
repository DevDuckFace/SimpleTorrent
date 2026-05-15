# SimpleTorrent

A simple torrent client for Windows with a Kill Switch button to block uploads.

## Features

- Download `.torrent` files
- Download via Magnet Links
- **Kill Switch** to block uploads (prevents seeding)
- Simple and intuitive interface
- Uploads blocked by default on startup
- **Multi-language support**: Portuguese, English, French, Spanish, German, Russian, and Chinese

## Prerequisites

1. **Visual Studio 2019+** with the "Desktop C++" workload
2. **CMake 3.20+** — [cmake.org](https://cmake.org/download/)
3. **vcpkg** — [github.com/microsoft/vcpkg](https://github.com/microsoft/vcpkg)
4. **Inno Setup 6+** — [jrsoftware.org](https://jrsoftware.org/isinfo.php)

## Installing vcpkg and libtorrent

```powershell
# Clone vcpkg (if you don't have it yet)
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# Set the environment variable
set VCPKG_ROOT=C:\vcpkg

# Install libtorrent (may take ~10-20 min)
C:\vcpkg\vcpkg install libtorrent:x64-windows-static
```

## Building

```powershell
cd SimpleTorrent
build.bat
```

The executable will be generated at `build\Release\SimpleTorrent.exe`.

## Creating the Installer

1. Open **Inno Setup Compiler**
2. Open the file `installer\setup.iss`
3. Click **Build > Compile** (or Ctrl+F9)
4. The installer will be generated at `dist\SimpleTorrent_Setup.exe`

## Project Structure

```
SimpleTorrent/
├── CMakeLists.txt          # Build configuration
├── vcpkg.json              # Dependencies
├── build.bat               # Build script
├── src/
│   ├── main.cpp            # Win32 interface
│   ├── torrent_engine.h    # Torrent engine (header)
│   ├── torrent_engine.cpp  # Torrent engine (implementation)
│   ├── resource.h          # Resource IDs
│   ├── app.rc              # Windows resources
│   └── app.manifest        # Visual styles
├── installer/
│   └── setup.iss           # Inno Setup script
└── README.md
```

## How to Use

1. Run `SimpleTorrent.exe`
2. Click **"Add .torrent"** or **"Magnet Link"**
3. The download starts automatically
4. The red **"UPLOAD BLOCKED"** button indicates uploads are cut off
5. Click the button to toggle (green = upload active)
6. Files are saved to `Documents\SimpleTorrent\`

## Language Support

SimpleTorrent is available in the following languages:

| Language   | Code  |
|------------|-------|
| Portuguese | `pt`  |
| English    | `en`  |
| French     | `fr`  |
| Spanish    | `es`  |
| German     | `de`  |
| Russian    | `ru`  |
| Chinese    | `zh`  |

You can select your preferred language from the settings menu inside the application.

## License

MIT License
