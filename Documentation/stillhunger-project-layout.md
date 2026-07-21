# They Still Hunger local project layout

They Still Hunger is split across two local repositories:

- Game code: `D:\Modding\theyhunger\hlsdk-portable`
- Sultim RayTracedGL1 engine: `D:\Modding\theyhunger\xash-rt`

Both repositories use `master` for durable local development. The engine's `3rdparty/mainui` directory is a submodule and must remain at the commit recorded by the parent repository.

The only live installation is `D:\Games\Xash3d`:

- Engine runtime: `D:\Games\Xash3d\rt_bin`
- Game directory/token: `StillHunger`
- Client DLL: `StillHunger\cl_dlls\client_amd64.dll`
- Server DLL: `StillHunger\dlls\einar_amd64.dll`

The engine is the `ref_gl.dll` + `RayTracedGL1.dll` path-traced renderer. It is unrelated to the separate FWGS `ref_vk` Vulkan project and the obsolete `D:\Games\Xash3d_RT` test installation.

## Compatibility contract

`xash-rt\engine\cdll_int.h` and `hlsdk-portable\engine\cdll_int.h` must define the same `CLDLL_INTERFACE_VERSION`, currently 10. Build scripts must stop before deployment when these values differ.

Treat the following as one engine runtime set: `xash3d.exe`, `xash.dll`, `filesystem_stdio.dll`, `ref_gl.dll`, `menu.dll`, `RayTracedGL1.dll`, and `SDL2.dll`. Build and deploy them together because an older menu or filesystem DLL can pass initial loading and still corrupt or reject the engine ABI.

Game builds must produce both `client_amd64.dll` and `einar_amd64.dll` inside the HLSDK build directory before either is copied into the live installation.

Use `-game StillHunger`. The `-log` switch takes no filename and writes `D:\Games\Xash3d\engine.log`.

