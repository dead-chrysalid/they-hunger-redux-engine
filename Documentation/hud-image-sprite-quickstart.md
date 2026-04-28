# HUD Image Sprite API

This note is for client-DLL work.

## Requirements

- Rebuild the client DLL against the current engine headers.
- `CLDLL_INTERFACE_VERSION` is `10`.

## Supported Loads

For non-map HUD/client sprites, `SPR_Load` can now load:

- `bmp`
- `tga`
- `png`
- `dds`
- `lmp`

Example:

```c
HSPRITE h = gEngfuncs.pfnSPR_Load( "sprites/HUD/cross.tga" );
```

## Main Draw Function

Use this for migrated HUD images:

```c
void pfnSPR_DrawTrans(
	HSPRITE hPic,
	int frame,
	int x,
	int y,
	int width,
	int height,
	const wrect_t *prc,
	int flags,
	int r,
	int g,
	int b,
	int a );
```

## Parameters

- `hPic`: sprite handle returned by `pfnSPR_Load`
- `frame`: sprite frame, usually `0` for image-backed HUD sprites
- `x`, `y`: HUD position
- `width`, `height`: draw size
- `prc`: optional source sub-rect, use `NULL` for the whole image
- `flags`: rotation/mirroring flags
- `r`, `g`, `b`, `a`: tint and opacity

## Size Rules

- `width <= 0` or `height <= 0` means use the sprite's native size.
- HUD screen scaling still applies after this.

## Flags

```c
HUDSPR_ROT_0
HUDSPR_ROT_90
HUDSPR_ROT_180
HUDSPR_ROT_270
HUDSPR_FLIP_X
HUDSPR_FLIP_Y
```

Notes:

- Rotation is clockwise.
- Flags can be combined with `|`.

## Examples

Basic draw:

```c
gEngfuncs.pfnSPR_DrawTrans( h, 0, 32, 32, 0, 0, NULL, 0, 255, 255, 255, 255 );
```

Tinted and semi-transparent:

```c
gEngfuncs.pfnSPR_DrawTrans( h, 0, 32, 32, 0, 0, NULL, 0, 255, 64, 64, 128 );
```

Scaled:

```c
gEngfuncs.pfnSPR_DrawTrans( h, 0, 32, 32, 64, 64, NULL, 0, 255, 255, 255, 255 );
```

Rotated:

```c
gEngfuncs.pfnSPR_DrawTrans( h, 0, 32, 32, 0, 0, NULL, HUDSPR_ROT_90, 255, 255, 255, 255 );
```

Mirrored:

```c
gEngfuncs.pfnSPR_DrawTrans( h, 0, 32, 32, 0, 0, NULL, HUDSPR_FLIP_X, 255, 255, 255, 255 );
```

Rotated and mirrored:

```c
gEngfuncs.pfnSPR_DrawTrans( h, 0, 32, 32, 0, 0, NULL, HUDSPR_ROT_90 | HUDSPR_FLIP_X, 255, 255, 255, 255 );
```

Sub-rect:

```c
wrect_t rc = { 0, 0, 32, 32 };
gEngfuncs.pfnSPR_DrawTrans( h, 0, 32, 32, 0, 0, &rc, 0, 255, 255, 255, 255 );
```

## Useful Helpers

Query loaded size:

```c
int w = gEngfuncs.pfnSPR_Width( h, 0 );
int hgt = gEngfuncs.pfnSPR_Height( h, 0 );
```

Get sprite model for custom rendering:

```c
model_s *mdl = (model_s *)gEngfuncs.GetSpritePointer( h );
```

## Notes

- `pfnSPR_DrawTrans` is the preferred draw path for migrated HUD images.
- It replaces the old split between `SPR_Draw`, `SPR_DrawHoles`, and `SPR_DrawAdditive` for normal image-backed HUD rendering.
- Transparency comes from the source image alpha channel. Black RGB pixels are not transparent by themselves.
- This path is only used when the client loads a real image path through `SPR_Load`. Old `hud.txt` atlas names do not automatically use it.
