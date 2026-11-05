# Castlevania Launcher

A native Windows x64 launcher for the experimental Grimoire of Souls OpenGL port. It finds the game and its native library relative to the launcher, starts the game, and opens the latest Unity log.

## Use with a portable installation

1. Close the launcher and extract the release ZIP into the root of your existing portable installation, beside its Game and Native folders.
2. Open CastlevaniaLauncher.exe and select **Play**.

The ZIP contains the launcher executable, folder marker, and this guide. You need a compatible portable installation with Game/Castlevania.exe and Native/Frameworks/GameAssembly.dylib. If the native library is stored elsewhere, choose it using **Browse**. Keep the Game and Native folders together when moving the installation.

## Build

Use CMake 3.25 or later and Visual Studio with the C++ desktop workload. Run these commands from the repository root.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is at build/Release/CastlevaniaLauncher.exe. The launcher uses a statically linked MSVC runtime.
