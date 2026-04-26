# Verified Windows Build Instructions

This document records the Windows build path that was verified in this checkout.

It is intentionally practical rather than generic.

## What Was Verified

The following were built successfully from this repository on Windows:

- the full solution `xash3d-fwgs.sln`
- the engine project `engine/xash.vcxproj`
- the main launcher executable
- the RT renderer DLL
- the filesystem DLL
- the menu DLL

Successful output files end up in `x64/Release/`.

## Required Prerequisites

You need:

- Visual Studio 2022 or Build Tools 2022 with the C++ `v143` toolchain
- the Windows SDK used by the toolchain
- SDL2 2.0.14 unpacked into `SDL2_VC`
- RTGL1 available locally

This repo already contains an `RTGL1` directory, so the simplest setup is to use that.

## RTGL1 Setup

Set the `RTGL1_SDK_PATH` environment variable to the local `RTGL1` directory:

```powershell
$env:RTGL1_SDK_PATH='D:\Modding\theyhunger\xash-rt\RTGL1'
```

The Visual Studio project expects these paths under that root:

- `Include\RTGL1\RTGL1.h`
- `Build\x64-Release\RayTracedGL1.lib`
- `Build\x64-Release\RayTracedGL1.dll`

In this checkout, the DLL may exist only as:

- `RTGL1\RayTracedGL1.dll`

If so, copy it to the expected build location once:

```powershell
Copy-Item `
  -LiteralPath 'D:\Modding\theyhunger\xash-rt\RTGL1\RayTracedGL1.dll' `
  -Destination 'D:\Modding\theyhunger\xash-rt\RTGL1\Build\x64-Release\RayTracedGL1.dll' `
  -Force
```

## Verified Solution Build Command

Run this from the repository root in PowerShell:

```powershell
$env:RTGL1_SDK_PATH='D:\Modding\theyhunger\xash-rt\RTGL1'
[System.Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  'xash3d-fwgs.sln' `
  '/p:Configuration=Release' `
  '/p:Platform=x64' `
  '/m:1' `
  '/v:minimal'
```

## Why `/m:1` Is Important

The solution was verified with single-node MSBuild.

Using the default parallel project build could fail early in project-reference resolution even when the source code was fine. In practice:

- `/m` or GUI-parallel builds could report a failure around `public.vcxproj`
- `/m:1` built the same projects successfully

So for now, treat single-node MSBuild as the reliable command-line build path for this repo.

## Why `PATH` Is Cleared In The Process

This specific environment hit a Windows/MSBuild issue caused by both `Path` and `PATH` existing in the process environment.

Clearing the process-level `PATH` before launching MSBuild avoided that conflict.

This does not permanently change your user or system environment. It only affects the current PowerShell process.

## Verified Engine-Only Build Command

If you only want the engine DLL:

```powershell
$env:RTGL1_SDK_PATH='D:\Modding\theyhunger\xash-rt\RTGL1'
[System.Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  'engine\xash.vcxproj' `
  '/p:Configuration=Release' `
  '/p:Platform=x64' `
  '/m:1' `
  '/v:minimal'
```

## Expected Outputs

After a successful solution build, these files should exist in `x64/Release/`:

- `xash3d.exe`
- `xash.dll`
- `ref_gl.dll`
- `filesystem_stdio.dll`
- `menu.dll`
- `public.lib`
- `opus.lib`

You should also see matching `.pdb`, `.lib`, and `.exp` files for the DLL projects.

## Current Status Notes

The build still emits many warnings, but the verified Release x64 build succeeds.

At the time these instructions were written:

- the engine included the preliminary HUD image-sprite bridge in `engine/client/cl_game.c`
- that change compiled and linked successfully as part of the full solution build
