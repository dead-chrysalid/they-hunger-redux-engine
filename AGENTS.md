# StillHunger project contract

- This repository is the Sultim RayTracedGL1 engine half of They Still Hunger. Use `ref_gl`, never the separate `ref_vk` Vulkan project.
- The game source is `D:\Modding\theyhunger\hlsdk-portable`.
- The live installation is `D:\Games\Xash3d`; engine binaries go in `rt_bin`, and the game token/folder is `StillHunger`.
- Keep durable engine development on local branch `master`. Remove completed topic branches and temporary worktrees after preserving their commits.
- Keep `engine\cdll_int.h` compatible with `hlsdk-portable\engine\cdll_int.h`; the current client API is 10.
- Build and deploy the complete engine runtime set, including matching `menu.dll`. Never deploy `xash.dll` alone after API-facing changes.
- Use the personal `$stillhunger-workspace` and `$xash-rt-build-deploy` skills for project routing and builds.
- Preserve untracked local dependency directories such as `RTGL1` and `SDL2_VC` and unrelated user assets.

