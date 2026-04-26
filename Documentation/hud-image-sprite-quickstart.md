# HUD Image Sprite Quickstart

This note is for client-DLL work.

It describes the new engine-side bridge that allows the client HUD to load standalone image files as `HSPRITE`s without converting them to `.spr` first.

It also describes the new preferred draw endpoint for migrated HUD images.

Compatibility note:

- the new function was appended to the end of `cl_enginefuncs_t`
- the engine/client HUD interface version is now `9`
- any client DLL using this engine must be rebuilt against the updated `cdll_int.h`

## What Changed

For non-map sprites, the engine can now accept these file types through the normal sprite-loading path:

- `bmp`
- `tga`
- `png`
- `dds`
- `lmp`

This means client HUD code can call:

```c
HSPRITE h = gEngfuncs.pfnSPR_Load( "sprites/HUD/cross.tga" );
```

and receive a usable one-frame sprite handle.

The engine wraps the image as a synthetic sprite model internally, so existing sprite-facing HUD code can keep working.

## Preferred Endpoint For Migrated HUD Assets

There is now a dedicated draw function intended for migrated HUD images:

```c
void pfnSPR_DrawTrans( HSPRITE hPic, int frame, int x, int y, int width, int height, const wrect_t *prc, int r, int g, int b, int a );
```

Behavior:

- always uses the alpha-respecting texture draw path
- always respects the source image transparency
- lets the client pass alpha explicitly
- lets the client pass explicit width/height for scaling
- `width <= 0` or `height <= 0` uses the sprite's native size
- avoids the old split between `SPR_Draw`, `SPR_DrawHoles`, and `SPR_DrawAdditive` for normal HUD image rendering

This should be the default draw endpoint for standalone TGA/PNG HUD images going forward.

## Important Scope

This bridge applies to:

- `SPR_CLIENT`
- `SPR_HUDSPRITE`
- ordinary HUD/client sprite loads

It does not change the map-sprite path.

It also does not automatically replace old `hud.txt` sprite-sheet logic.

If the client never passes an actual image path to `SPR_Load`, this bridge will not be used.

## Minimal Client-Side Test

Put a real alpha image at:

```text
sprites/HUD/cross.tga
```

Then in client HUD code:

```c
static HSPRITE s_testCross;

if( !s_testCross )
	s_testCross = gEngfuncs.pfnSPR_Load( "sprites/HUD/cross.tga" );
```

Draw it somewhere obvious with the new endpoint:

```c
if( s_testCross )
{
	gEngfuncs.pfnSPR_DrawTrans( s_testCross, 0, 32, 32, 0, 0, NULL, 255, 255, 255, 255 );
}
```

This should draw the entire image at `(32, 32)`.

Key detail:

- pass `NULL` for the rect if you want the whole image

## Tinting

Tinting works through the RGBA parameters:

```c
gEngfuncs.pfnSPR_DrawTrans( s_testCross, 0, 32, 32, 0, 0, NULL, 255, 64, 64, 255 );
```

Useful smoke tests:

- white tint: `255, 255, 255`
- red tint: `255, 64, 64`
- green tint: `64, 255, 64`
- blue tint: `64, 64, 255`
- half alpha: `255, 255, 255, 128`

If tinting behaves normally, the synthetic sprite is flowing through the regular HUD sprite draw path as intended.

## Querying Dimensions

You can query the loaded image size through the normal sprite API:

```c
int w = gEngfuncs.pfnSPR_Width( s_testCross, 0 );
int h = gEngfuncs.pfnSPR_Height( s_testCross, 0 );
```

This is useful for centering or alignment tests.

Example:

```c
int w = gEngfuncs.pfnSPR_Width( s_testCross, 0 );
int h = gEngfuncs.pfnSPR_Height( s_testCross, 0 );

gEngfuncs.pfnSPR_DrawTrans( s_testCross, 0, ScreenWidth - w - 32, ScreenHeight - h - 32, 0, 0, NULL, 255, 255, 255, 255 );
```

## Using A Sub-Rect

If you want to crop the image manually, you can still pass a `wrect_t`:

```c
wrect_t rc;
rc.left = 0;
rc.top = 0;
rc.right = 32;
rc.bottom = 32;

gEngfuncs.pfnSPR_DrawTrans( s_testCross, 0, 32, 32, 0, 0, &rc, 255, 255, 255, 255 );
```

But for this new path, whole-image drawing is the main point.

## Scaled Draw

If you want to resize the image explicitly, pass width and height:

```c
gEngfuncs.pfnSPR_DrawTrans( s_testCross, 0, 32, 32, 64, 64, NULL, 255, 255, 255, 255 );
```

If you want native size, pass `0, 0`.

## TriAPI Use

If the client wants to keep a custom quad-rendering path, the model pointer can still be fetched:

```c
model_s *mdl = (model_s *)gEngfuncs.GetSpritePointer( s_testCross );
```

Then:

```c
if( mdl && gEngfuncs.pTriAPI )
	gEngfuncs.pTriAPI->SpriteTexture( mdl, 0 );
```

This lets the client bind the loaded image-backed sprite texture and render it through TriAPI.

For a first smoke test, though, prefer `pfnSPR_DrawTrans` over TriAPI.

## Transparency Expectations

If `cross.tga` has a real alpha channel, transparent background should behave like a normal alpha-backed HUD sprite.

The important thing is:

- transparent pixels must actually be transparent in the source image
- plain black RGB does not count as transparency by itself

So for TGA testing:

- use a true alpha channel
- do not rely on black-background chroma-key assumptions

## Best First Test Layout

A good first smoke test is to draw the same image three times with different tint and alpha:

```c
if( s_testCross )
{
	gEngfuncs.pfnSPR_DrawTrans( s_testCross, 0, 32, 32, 0, 0, NULL, 255, 255, 255, 255 );
	gEngfuncs.pfnSPR_DrawTrans( s_testCross, 0, 96, 32, 0, 0, NULL, 255, 64, 64, 255 );
	gEngfuncs.pfnSPR_DrawTrans( s_testCross, 0, 160, 32, 0, 0, NULL, 255, 255, 255, 128 );
	gEngfuncs.pfnSPR_DrawTrans( s_testCross, 0, 224, 32, 64, 64, NULL, 255, 255, 255, 255 );
}
```

That verifies:

- load works
- whole-image draw works
- tinting works
- explicit alpha works
- explicit scaling works

## What To Tell The Client-Side Agent

The key implementation rule is:

- if you want to test the new bridge, call `SPR_Load` with the real image path directly
- for migrated HUD images, prefer `pfnSPR_DrawTrans` instead of the legacy sprite draw variants

Do not start by routing through old `hud.txt` atlas names like:

- `640hud7`
- `320hud2`
- `selection`

Those are part of the legacy asset lookup system and may not exercise the new path at all.
