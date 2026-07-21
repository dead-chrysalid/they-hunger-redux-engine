# Live in-game RT editor

This experimental editor spans the matching `codex/rt-live-editor` branches
in xash-rt, hlsdk-portable, and RayTracedGL1. It is intentionally restricted
to an active developer-mode single-player game using `ref_gl`.

## Controls

- `F10`: enter or leave editing mode.
- Mouse and normal movement keys: fly and look around; Shift boosts speed.
- Primary click while flying: select the light glyph or visible surface at the
  center reticle.
- Light glyph centers use the authored light color. Selecting a spotlight shows
  its inner and outer cone wireframes in that color.
- `Tab`: open the workspace without selecting anything.
- `Escape`: close the workspace and resume editor flight.
- `rt_editor_retry_save`: retry files which remain visibly unsaved.

Opening the workspace freezes the detached camera and releases the cursor.
Gameplay commands, HUD, VGUI, and the viewmodel are suppressed for the entire
editor session and restored on exit.

## Authored data

Atmosphere changes update the current entry in `scenes.json`. Texture controls
edit either global or per-map `textures.json`, with field-level inheritance.
All optional fields have explicit Used/Unused state.

Editor lights are retained in the map glTF root `extras` as
`rt_editor_lights`, with stable `rt_editor_id` values and the source-radius,
angular-diameter, lightstyle, and volumetric properties needed by the runtime.
`rt_suppress_original_lights` records explicit ownership even when the authored
list is empty. Existing glTF fields, extensions, buffers, and images are left
untouched.

Every completed action is saved using a validated sibling temporary file and
atomic replacement. The first version of each touched file is copied to the
runtime data directory's timestamped `editor-backups` session folder. Failed
saves stay live and visibly unsaved. Clean external changes reload
automatically; conflicting changes require an explicit reload or cancellation.

## Renderer dependency

The tracked dependency patch is documented in
`patches/raytracedgl1/README.md`. The renderer interface is version 4; the
client DLL interface remains version 10.
