# Kill-effect renderer integration

The client triggers the RT lens pulse with `rt_killfx_trigger`. The renderer passes a
negative red component to `RgPostEffectColorTint` as a private signal for the impact-lens
shader path; ordinary color-tint effects continue to use non-negative RGB values.

The RayTracedGL1 release bundle is an external, untracked dependency. Before rebuilding
that library, apply the matching shader patch from the RayTracedGL1 source root:

```sh
git apply --unidiff-zero <xash-rt>/scripts/patches/rtgl1-killfx-lens.patch
```

Regenerate the RayTracedGL1 shaders and rebuild the library normally after applying it.

Archived renderer controls:

- `rt_killfx_distortion`: enable the lens pulse.
- `rt_killfx_lens_strength`: fisheye/zoom strength.
- `rt_killfx_distortion_duration`: full-strength hold time.
- `rt_killfx_distortion_fade_in`: transition-in time.
- `rt_killfx_distortion_fade_out`: transition-out time.

Run `rt_killfx_trigger` in the console to preview the renderer portion by itself.
