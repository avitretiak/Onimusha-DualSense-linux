Onimusha-DualSense-Linux
Nexus source package and runtime downloads

Nexus source package
--------------------
This ZIP contains the tracked source code, documentation, licenses, and default
INI, but excludes compiled binaries and shell scripts. It is for source review
and building, not a complete game install.

Runtime-only GitHub download:
https://github.com/avitretiak/Onimusha-DualSense-linux/releases/latest/download/Onimusha-DualSense-Linux-runtimes-latest.zip

The runtime ZIP contains the compiled plug-in, helper, asset generator,
required runtime files, and default settings. Extract it into the game folder
containing OnimushaWotS.exe. For the easiest install, use the full package:
https://github.com/avitretiak/Onimusha-DualSense-linux/releases/latest/download/Onimusha-DualSense-Linux-latest.zip

Generate game-local assets once from the game folder:

   dotnet asset-generator/OnimushaDualSense.dll prepare-assets --game "$PWD"

The generator needs the .NET 10 runtime. Wine is needed only when the native
ree-pak-cli and vgmstream-cli tools are not available. Keep the generated files
in reframework/data with the matching plug-in release.

Steam launch options for the runtime-only package are the normal %command%.
The HID relay is optional; if needed, start ./onimusha_hidrelay from a terminal
in the game folder and stop it with Ctrl+C. The full package includes scripts
that manage these steps.

Source and build instructions
-----------------------------
Repository and release workflow:
https://github.com/avitretiak/Onimusha-DualSense-linux
https://github.com/avitretiak/Onimusha-DualSense-linux/blob/main/.github/workflows/release.yml

Build prerequisites: CMake, LLVM-MinGW, libhidapi development files, and the
.NET 10 SDK. From the repository root:

1. Set MINGW_CXX to the LLVM-MinGW x86_64-w64-mingw32-clang++ executable.
2. Build the Windows plug-in:

   cmake -S native/reframework -B build/reframework -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_CXX_COMPILER="$MINGW_CXX" -DREF_INCLUDE_DIR="$PWD/native/reframework" -DCMAKE_BUILD_TYPE=Release
   cmake --build build/reframework --config Release

3. Build the Linux HID relay:

   cmake -S native/hidrelay -B build/hidrelay -DCMAKE_BUILD_TYPE=Release
   cmake --build build/hidrelay --config Release

4. Publish the asset generator:

   dotnet restore OnimushaDualSense/OnimushaDualSense.csproj --configfile NuGet.Config -r linux-x64
   dotnet publish OnimushaDualSense/OnimushaDualSense.csproj -c Release -r linux-x64 --self-contained false --no-restore -o build/asset-generator

5. Build the full ZIP with tools/package-release.py. The GitHub Actions workflow
   shows the exact release invocation and validation.
