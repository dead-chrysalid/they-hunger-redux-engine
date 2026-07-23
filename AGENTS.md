# They Still Hunger engine contract

- The project root is `D:\Modding\theyhunger\They Still Hunger`; resolve paths from its `project.json`.
- This is the Sultim RayTracedGL1 engine. Work in `ref_gl`; never route StillHunger work to the separate `ref_vk` project.
- Start new work with the root `tools\StillHunger.ps1 new` command. Develop in matching feature worktrees, then finish by merging intended work into local `master` or explicitly record why it remains experimental.
- Keep `engine\cdll_int.h` compatible with the SDK copy. `CLDLL_INTERFACE_VERSION` is 10 and `REF_API_VERSION` is 4.
- Engine API-facing releases require the complete paired runtime: `xash3d.exe`, `xash.dll`, `filesystem_stdio.dll`, `ref_gl.dll`, `menu.dll`, `RayTracedGL1.dll`, and `SDL2.dll`.
- Builds are build-only by default. Accept explicit RayTracedGL1 and SDL2 paths; never configure a project or post-build step to write into production.
- Stage and smoke-test only through the project command. Agents may launch project-owned feature, preprod, and production runtimes, but must never terminate or control a process they did not start.
- Preserve unrelated user content and untracked dependencies. Completed feature branches and worktrees must be consolidated or explicitly retained at handoff.
