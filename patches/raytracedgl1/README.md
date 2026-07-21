# RayTracedGL1 live-editor dependency patch

The ignored `RTGL1` working tree is developed on its own
`codex/rt-live-editor` branch. The tracked mail patch in this directory makes
that dependency work reproducible from the exact upstream source revision.

- RayTracedGL1 base: `cba274a63c00ce2d2d2a8bb48c3a87e680afb27e`
- Editor commit: `31b8e593b2679dc2b2c2fc9563a6d42eb1173393`
- ImGuizmo snapshot: `dc25afb98bc3ebe00dfc9a23ba7235fead2ccb1d`
- ImGuizmo source and MIT license are included by the patch.

Apply the patch from a clean checkout of the base revision:

```powershell
git am D:\Modding\theyhunger\xash-rt\patches\raytracedgl1\0001-Add-live-in-game-RT-editor.patch
```

The editor-specific tests are enabled with `RG_WITH_EDITOR_TESTS=ON`.
