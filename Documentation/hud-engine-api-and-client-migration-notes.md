# HUD Engine API And Client Migration Notes

This document is a portable reference for agents and developers working on HUD changes in a separate client/game project.

It describes:

- what the engine exposes to the client HUD
- what the engine can actually draw
- what parts of the current HUD architecture are historical rather than required
- where modernization work should happen

This version is intentionally self-contained so it can be copied into another repo and still be useful.

## Executive Summary

The HUD is split into two layers:

- The engine provides a rendering and integration API to the client DLL.
- The client DLL decides what HUD elements exist, where they are placed, which assets they use, and what gets drawn each frame.

The main takeaway for HUD modernization is:

- most of the work belongs in the client project
- the engine already exposes enough functionality to support a much cleaner HUD system
- removing `hud.txt` and old sprite-sheet lookup conventions is primarily a client-side architecture change

In other words, the likely plan is:

1. Keep using the engine's HUD-facing API.
2. Replace the old client HUD code and asset conventions.
3. Only change the engine if you want convenience helpers, not because the engine fundamentally blocks a better HUD.

## Lifecycle: How The Engine Drives The HUD

The engine loads the client DLL and calls a familiar set of HUD exports. The important ones are:

- `Initialize`
- `HUD_VidInit`
- `HUD_Init`
- `HUD_Redraw`
- `HUD_UpdateClientData`
- `HUD_Reset`

Conceptually, the sequence is:

1. The engine loads the client DLL.
2. The engine passes a table of engine callbacks to the client through `Initialize`.
3. The client stores those callbacks and uses them as its HUD API.
4. Each frame, the engine calls `HUD_Redraw(time, intermission)`.
5. The client HUD decides what to draw and calls back into the engine to render it.

Important architectural point:

- the engine does not define the actual HUD layout
- the client DLL owns HUD composition

## The Main Engine HUD API

The engine callback table exposed to the client HUD is `cl_enginefuncs_t`.

Below is the subset that matters most for HUD work.

## Sprite API

The classic HUD uses sprites heavily. The engine exposes this interface:

```c
typedef int HSPRITE;

typedef struct client_sprite_s
{
	char szName[64];
	char szSprite[64];
	int hspr;
	int iRes;
	wrect_t rc;
} client_sprite_t;
```

Relevant callbacks:

```c
HSPRITE (*pfnSPR_Load)( const char *szPicName );
int (*pfnSPR_Frames)( HSPRITE hPic );
int (*pfnSPR_Height)( HSPRITE hPic, int frame );
int (*pfnSPR_Width)( HSPRITE hPic, int frame );
void (*pfnSPR_Set)( HSPRITE hPic, int r, int g, int b );
void (*pfnSPR_Draw)( int frame, int x, int y, const wrect_t *prc );
void (*pfnSPR_DrawHoles)( int frame, int x, int y, const wrect_t *prc );
void (*pfnSPR_DrawAdditive)( int frame, int x, int y, const wrect_t *prc );
void (*pfnSPR_EnableScissor)( int x, int y, int width, int height );
void (*pfnSPR_DisableScissor)( void );
client_sprite_t *(*pfnSPR_GetList)( char *psz, int *piCount );
```

What this means in practice:

- the client can load `.spr` files directly
- the client can query frame count and dimensions
- the client can tint sprites
- the client can draw either full sprites or sub-rectangles
- the client can clip sprite drawing to a scissor region
- the client can still use old-style sprite list parsing through `pfnSPR_GetList`

Important observation:

- the engine itself is not limited to sprite atlases
- full-sprite rendering already exists
- `hud.txt` is an old client convention, not a hard renderer requirement

## Screen And Primitive Drawing API

The engine exposes these HUD-oriented helpers:

```c
void (*pfnFillRGBA)( int x, int y, int width, int height, int r, int g, int b, int a );
int (*pfnGetScreenInfo)( SCREENINFO *pscrinfo );
void (*pfnSetCrosshair)( HSPRITE hspr, wrect_t rc, int r, int g, int b );
```

There is also an extended blend helper in newer/extended builds:

```c
void (*pfnFillRGBABlend)( int x, int y, int width, int height, int r, int g, int b, int a );
```

These are enough for:

- panel backgrounds
- bars
- overlays
- separators
- color flashes
- full-screen fades or HUD underlays

## Text API

The engine exposes text and text-measurement helpers:

```c
client_textmessage_t *(*pfnTextMessageGet)( const char *pName );
int (*pfnDrawCharacter)( int x, int y, int number, int r, int g, int b );
int (*pfnDrawConsoleString)( int x, int y, char *string );
void (*pfnDrawSetTextColor)( float r, float g, float b );
void (*pfnDrawConsoleStringLen)( const char *string, int *length, int *height );
void (*pfnConsolePrint)( const char *string );
void (*pfnCenterPrint)( const char *string );
```

There are also extended string helpers commonly available:

```c
int (*pfnDrawString)( int x, int y, const char *str, int r, int g, int b );
int (*pfnDrawStringReverse)( int x, int y, const char *str, int r, int g, int b );
int (*pfnVGUI2DrawCharacter)( int x, int y, int ch, unsigned int font );
int (*pfnVGUI2DrawCharacterAdditive)( int x, int y, int ch, int r, int g, int b, unsigned int font );
```

This gives the client HUD:

- text drawing
- text color control
- string measurement
- right-aligned string drawing
- access to text-message definitions

## Message, Cvar, And Command API

HUD code usually depends on message hooks and cvars more than on rendering details.

Relevant callbacks include:

```c
struct cvar_s *(*pfnRegisterVariable)( const char *szName, const char *szValue, int flags );
float (*pfnGetCvarFloat)( const char *szName );
const char *(*pfnGetCvarString)( const char *szName );

int (*pfnAddCommand)( const char *cmd_name, void (*function)(void) );
int (*pfnHookUserMsg)( const char *szMsgName, pfnUserMsgHook pfn );
int (*pfnServerCmd)( const char *szCmdString );
int (*pfnClientCmd)( const char *szCmdString );
```

This is the basis for classic HUD message handling such as:

- `WeaponList`
- `CurWeapon`
- `AmmoX`
- `AmmoPickup`
- `WeapPickup`
- `ItemPickup`
- `Health`
- `Battery`
- `HideWeapon`

A modern HUD rewrite will probably keep the message-driven gameplay state updates while replacing the visual asset and layout system on top of them.

## Game State And Player Access Helpers

Useful HUD-adjacent callbacks include:

```c
void (*pfnGetPlayerInfo)( int ent_num, hud_player_info_t *pinfo );
struct cl_entity_s *(*GetLocalPlayer)( void );
struct cl_entity_s *(*GetViewModel)( void );
struct cl_entity_s *(*GetEntityByIndex)( int idx );
float (*GetClientTime)( void );
const char *(*Key_LookupBinding)( const char *pBinding );
```

These help with:

- local-player state lookups
- spectator or scoreboard HUD elements
- binding prompts
- time-based HUD effects

## Lower-Level Rendering Escape Hatch: Triangle API

The biggest reason a HUD rewrite is feasible without heavy engine changes is that the engine also exposes `pTriAPI`.

Relevant interface:

```c
typedef struct triangleapi_s
{
	int version;

	void (*RenderMode)( int mode );
	void (*Begin)( int primitiveCode );
	void (*End)( void );

	void (*Color4f)( float r, float g, float b, float a );
	void (*Color4ub)( unsigned char r, unsigned char g, unsigned char b, unsigned char a );
	void (*TexCoord2f)( float u, float v );
	void (*Vertex3fv)( const float *worldPnt );
	void (*Vertex3f)( float x, float y, float z );
	void (*Brightness)( float brightness );
	void (*CullFace)( TRICULLSTYLE style );
	int (*SpriteTexture)( struct model_s *pSpriteModel, int frame );
	int (*WorldToScreen)( const float *world, float *screen );
	void (*Fog)( float flFogColor[3], float flStart, float flEnd, int bOn );
	void (*ScreenToWorld)( const float *screen, float *world );
	void (*GetMatrix)( const int pname, float *matrix );
	int (*BoxInPVS)( float *mins, float *maxs );
	void (*LightAtPoint)( float *pos, float *value );
	void (*Color4fRendermode)( float r, float g, float b, float a, int rendermode );
	void (*FogParams)( float flDensity, int iFogSkybox );
} triangleapi_t;
```

Practical meaning:

- the client HUD is not limited to classic sprite helper calls
- it can draw textured quads directly
- it can draw lines and other primitives
- it can bind sprite textures and build its own renderer

So if a modern HUD wants its own layout/render pass, it can.

## What The Engine Can Actually Draw

The engine can already draw the primitives needed for a modern 2D HUD.

## 1. Full Sprites

The sprite drawing path can render a whole sprite frame if the rect parameter is null.

Conceptually:

```c
SPR_Set( sprite, r, g, b );
SPR_Draw( frame, x, y, NULL );
```

That means:

- standalone HUD icons in separate `.spr` files are already compatible with the engine draw path
- the old atlas style is not mandatory

## 2. Sprite Sub-Rects

The same draw path can render only a sub-rectangle:

```c
wrect_t rc = { left, top, right, bottom };
SPR_Draw( frame, x, y, &rc );
```

This is what the old `hud.txt` model is built around.

## 3. Tinted Sprites

The client can tint sprites before drawing them:

```c
SPR_Set( sprite, r, g, b );
SPR_Draw( frame, x, y, rect_or_null );
```

Useful for:

- active/inactive states
- damage flashes
- highlight states
- selection emphasis

## 4. Different Sprite Blend Modes

The engine provides:

- normal sprite draw
- holes/trans-color draw
- additive draw

This is enough for most classic HUD-style effects.

## 5. Clipped Sprite Regions

The client can clip HUD drawing:

```c
SPR_EnableScissor( x, y, width, height );
// draw clipped content here
SPR_DisableScissor();
```

Useful for:

- progress bars
- container clipping
- scrolling lists
- weapon carousel windows

## 6. Filled Rectangles

The client can draw boxes:

```c
FillRGBA( x, y, w, h, r, g, b, a );
FillRGBABlend( x, y, w, h, r, g, b, a );
```

Useful for:

- panels
- meter backgrounds
- separators
- glassy or translucent overlays

## 7. Text

The client can draw and measure strings:

```c
DrawSetTextColor( r, g, b );
DrawConsoleString( x, y, "Ammo" );
DrawConsoleStringLen( "Ammo", &w, &h );
```

Useful for:

- ammo counters
- health numbers
- weapon names
- prompts

## 8. Custom Geometry

If sprite/box/text helpers are not enough, the client can use `pTriAPI`.

That allows:

- custom textured quads
- lines
- custom batching
- a more modern retained-mode or immediate-mode HUD renderer

## Screen Coordinates And Scaling

HUD code should use `SCREENINFO` as its canonical layout space.

Relevant struct:

```c
typedef struct SCREENINFO_s
{
	int iSize;
	int iWidth;
	int iHeight;
	int iFlags;
	int iCharHeight;
	short charWidths[256];
} SCREENINFO;
```

The important point is:

- HUD coordinates are logical HUD coordinates
- they may not match the raw framebuffer size

The engine can scale HUD space depending on configuration. In this engine fork there are two relevant concepts:

- `hud_scale`
- `rt_hud_scale`

Practical implication for client HUD code:

- never assume 1 HUD unit equals 1 framebuffer pixel
- ask the engine for screen info
- treat `SCREENINFO.iWidth` and `SCREENINFO.iHeight` as your layout resolution

This is especially important for any anchor-based or panel-based redesign.

## Why `hud.txt` Exists In The Old System

The classic HUD uses parsed sprite list files because the old client code expects sprite metadata in this general form:

```c
typedef struct client_sprite_s
{
	char szName[64];
	char szSprite[64];
	int hspr;
	int iRes;
	wrect_t rc;
} client_sprite_t;
```

And the classic sprite list format conceptually provides:

- logical name
- resolution bucket such as `320` or `640`
- sprite sheet file name
- rectangle inside that sheet

That is why old HUD data is fragmented:

- UI code asks for a named logical sprite
- `hud.txt` maps that name to a sheet and rectangle
- weapon HUD code may separately depend on weapon-specific sprite lists

This is historical layering, not a hard engine requirement.

## What Is Actually Mandatory Versus Historical

## Mandatory

The client HUD must still do these things somehow:

- receive gameplay state from user messages and client data
- decide what to draw
- issue draw calls through the engine API
- respect screen info and scaling

## Historical, And Safe To Replace

These are conventions, not engine requirements:

- `hud.txt` as the master HUD visual definition file
- atlas-only HUD assets
- manually defined sprite sheet rectangles for every icon
- stock HL HUD class structure
- per-widget hardcoded positioning style

## What Should Be Modernized On The Client Side

If the goal is a unified, less restrictive HUD architecture, the client project should likely introduce a small HUD layer with concepts like:

- `HudImage`
- `HudText`
- `HudBar`
- `HudPanel`
- `HudAnchor`
- `HudStack`
- `HudStyle`
- `HudAssetRegistry`

For example, a cleaner asset model could be:

```c
struct HudImageAsset
{
	const char *id;
	const char *path;
};
```

instead of:

```c
struct OldHudSpriteEntry
{
	const char *logical_name;
	const char *sheet_name;
	int x;
	int y;
	int w;
	int h;
};
```

A modern client-side HUD system could then:

- load sprites directly by path
- draw whole sprites by default
- only use sub-rects when truly needed
- define layout declaratively or through widget code
- keep message parsing separate from visual asset lookup

## Recommended Scope Split

## Client Project Work

This is where most HUD modernization should happen:

- replace stock HUD widget code
- replace `hud.txt` lookups with a cleaner asset system
- replace old hardcoded layout logic with anchors/panels/widgets
- keep gameplay message handling but decouple it from rendering details

## Optional Engine Work

Engine changes are optional and mainly about convenience. Examples:

- helper to load and draw a full sprite by path in one step
- helper to fetch sprite dimensions more directly for UI layout
- helper to support a new simplified asset manifest format

None of those are required to begin a serious client-side HUD rewrite.

## Guidance For Agents Planning HUD Changes

If you are an agent working in the client project, the safest assumption is:

- do not start by redesigning the engine API
- first redesign the client HUD architecture using the existing engine callbacks

Recommended planning order:

1. Identify the current client HUD state model.
2. Separate gameplay state updates from rendering code.
3. Replace old sprite-list lookups with a direct asset registry.
4. Introduce reusable layout primitives.
5. Migrate one HUD feature at a time, such as weapon select or the bottom bar.
6. Only request engine-side helpers if the client rewrite hits a real limitation.

## Bottom Line

The engine already provides enough to build a better HUD.

It can already render:

- full sprites
- sprite sub-rectangles
- tinted sprites
- clipped sprite regions
- filled rectangles
- blended rectangles
- text
- custom geometry through Triangle API

So the real challenge is not making the engine capable of a better HUD.

The real challenge is replacing the old client-side HUD conventions with a cleaner client-side HUD architecture.

## Preliminary Engine Bridge: Generic HUD Images

The engine now has a small compatibility bridge for HUD sprite loading.

For non-map sprites, `SPR_Load` can now accept these file types directly:

- `bmp`
- `tga`
- `png`
- `dds`
- `lmp`

Instead of requiring a real `.spr` file, the engine can wrap one of those images as a synthetic one-frame sprite model.

The goal of this bridge is not to redesign the HUD system. The goal is to let existing client HUD code keep using familiar sprite-facing APIs while beginning to migrate away from `.spr` assets.

What still works on the client side with one of these image-backed sprites:

- `SPR_Load`
- `SPR_Width`
- `SPR_Height`
- `SPR_Draw`
- `SPR_DrawAdditive`
- `GetSpritePointer`
- `TriAPI->SpriteTexture( model, 0 )`

Important limitation:

- this bridge only activates when the client actually passes an image path into `SPR_Load`
- old `hud.txt` atlas entries like `640hud7` do not automatically exercise this path

There is also now a dedicated draw endpoint intended for migrated HUD images:

```c
void pfnSPR_DrawTrans( HSPRITE hPic, int frame, int x, int y, int width, int height, const wrect_t *prc, int flags, int r, int g, int b, int a );
```

This should be preferred over the legacy split between:

- `SPR_Draw`
- `SPR_DrawHoles`
- `SPR_DrawAdditive`

for ordinary alpha-backed HUD image rendering.

Behavior notes:

- respects texture alpha and explicit draw alpha
- supports tinting
- supports explicit width/height scaling
- `width <= 0` or `height <= 0` means use the sprite's native size
- supports `HUDSPR_FLIP_X`, `HUDSPR_FLIP_Y`, and clockwise `HUDSPR_ROT_90/180/270`
- still goes through normal HUD coordinate scaling

Because this extends `cl_enginefuncs_t`, engine and client builds should now move together on `CLDLL_INTERFACE_VERSION 10`.

In other words:

- if the client still only loads classic sheet names from `hud.txt`, this patch is dormant
- to test it, the client must directly request something like `sprites/HUD/test_icon.png`

## Practical Smoke Test For The New Loading Path

The safest first test is a tiny client-side hardcoded smoke test.

Do not start with the weapon menu or the existing HUD asset registry yet.

Do this first:

1. Put a standalone image with a real alpha channel in the mod, for example:
   - `sprites/HUD/test_cross.png`
2. In client HUD code, add a temporary load:

```c
static HSPRITE s_testSprite;

if( !s_testSprite )
	s_testSprite = gEngfuncs.pfnSPR_Load( "sprites/HUD/test_cross.png" );
```

3. Draw it with the normal sprite path:

```c
if( s_testSprite )
{
	gEngfuncs.pfnSPR_Set( s_testSprite, 255, 255, 255 );
	gEngfuncs.pfnSPR_Draw( 0, 32, 32, NULL );
}
```

4. Verify dimensions if needed:

```c
int w = gEngfuncs.pfnSPR_Width( s_testSprite, 0 );
int h = gEngfuncs.pfnSPR_Height( s_testSprite, 0 );
```

Expected result:

- the image loads without going through `.spr`
- frame `0` exists
- width and height match the source image
- `SPR_Draw(..., NULL)` draws the whole image

## Why This Is The Best First Test

This test isolates only the new engine bridge.

It avoids three separate sources of confusion:

- legacy `hud.txt` lookup behavior
- sprite-sheet rect logic
- custom TriAPI blend-mode experiments

If this smoke test works, then the engine-side image loading bridge is good enough to start real client-side migration work.

If this smoke test fails, the problem is likely one of:

- wrong asset path
- image not packaged where the filesystem can see it
- source image lacks a real alpha channel
- the client code never reaches the load/draw path

## Optional TriAPI Follow-Up Test

Once direct `SPR_Draw` works, you can verify that the synthetic sprite also exposes a usable model pointer:

```c
model_s *mdl = (model_s *)gEngfuncs.GetSpritePointer( s_testSprite );
```

and then:

```c
if( mdl && gEngfuncs.pTriAPI )
	gEngfuncs.pTriAPI->SpriteTexture( mdl, 0 );
```

This is useful if the client plans to keep a custom textured-quad HUD path.

However, for the first test, prefer `SPR_Draw` over TriAPI.

That keeps the question narrow:

- "can the engine load a PNG/TGA and expose it as an `HSPRITE`?"

## Testing Through Existing HUD Code

After the hardcoded smoke test works, the next migration step is usually:

1. choose one low-risk HUD element
2. stop sourcing it from a sprite sheet
3. load it by direct image path instead
4. keep the existing draw call style for now

Good first candidates:

- a crosshair variant
- a single decorative HUD icon
- a weapon selection highlight frame

Bad first candidates:

- digit rendering
- anything still heavily dependent on `hud.txt` atlas rectangles
- anything that mixes old holes/additive assumptions with opaque source art
