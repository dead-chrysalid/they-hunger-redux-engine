# Alpha-aware raster emission

RayTracedGL1 raster effects write their visible color and glow to separate
attachments. The color attachment already uses source-alpha blending, while the
emission attachment is additively blended. Its fragment shader therefore has to
apply texture alpha before writing emission. Without that multiplication,
transparent parts of additive models and sprites glow as opaque rectangles.

The external `RTGL1` source tree is intentionally ignored by this repository.
Apply the maintained patch before rebuilding its raster shader:

```powershell
git -C RTGL1 apply ..\scripts\patches\rtgl1-alpha-aware-raster-emission.patch
```

Then compile `Source/Shaders/RsWorld.frag` and deploy the resulting
`RsWorld.frag.spv` to `rt/shaders` with the renderer runtime. Opaque emissive
materials are unchanged because their alpha is one; non-emissive materials do
not write meaningful emission.
