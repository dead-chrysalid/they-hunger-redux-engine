# RayTracedGL1 live-editor dependency patch

The ignored `RTGL1` working tree is developed on its own
`codex/rt-live-editor` branch. The tracked mail patches in this directory make
that dependency work reproducible from the exact upstream source revision.

- RayTracedGL1 base: `cba274a63c00ce2d2d2a8bb48c3a87e680afb27e`
- Editor head: `fbdf19dd90702a31442c7db6add68650d5f5e5a9`
- ImGuizmo snapshot: `dc25afb98bc3ebe00dfc9a23ba7235fead2ccb1d`
- ImGuizmo source and MIT license are included by the patch.

Apply the patches from a clean checkout of the base revision:

```powershell
git am D:\Modding\theyhunger\xash-rt\patches\raytracedgl1\0001-Add-live-in-game-RT-editor.patch D:\Modding\theyhunger\xash-rt\patches\raytracedgl1\0002-Fix-live-editor-rendering-and-glTF-light-saves.patch D:\Modding\theyhunger\xash-rt\patches\raytracedgl1\0003-Improve-RT-editor-workflow-and-picking.patch D:\Modding\theyhunger\xash-rt\patches\raytracedgl1\0004-Add-OptiScaler-FSR2-module-interop.patch
```

The editor-specific tests are enabled with `RG_WITH_EDITOR_TESTS=ON`.
