# Live in-game RT editor

This experimental editor spans the matching `codex/rt-live-editor` branches
in xash-rt, hlsdk-portable, and RayTracedGL1. It is intentionally restricted
to an active developer-mode single-player game using `ref_gl`.

## Controls

- `F10`: enter the editor in Edit mode, or leave it and restore normal play.
- `Tab`: toggle between Edit and Flight modes.
- Edit mode locks the camera, shows the workspace and cursor, and selects light
  glyphs or visible map surfaces at the cursor. `Shift` temporarily enters
  Flight mode for as long as either Shift key is held.
- Flight mode uses the actual player as a fast noclip camera. Mouse clicks are
  consumed and cannot select or trigger gameplay actions.
- `R`: switch the selected light gizmo between move and rotate.
- `Delete`: delete the selected light.
- `Ctrl+C` / `Ctrl+V`: copy and paste a light's authored settings while keeping
  the target's position and stable ID.
- `Ctrl+D`: duplicate the selected light.
- `Ctrl+Z` / `Ctrl+Y`: undo or redo the active Lights, Atmosphere, or Textures
  panel.
- `Escape`: cancel the focused UI or clear the current selection.
- `rt_editor_retry_save`: retry files which remain visibly unsaved.

Gameplay commands, HUD, VGUI, and the viewmodel are suppressed for the entire
editor session and restored on exit. Focus loss synthesizes Shift release so a
temporary flight cannot remain stuck.

Point and spot light glyphs keep their previous screen size as a minimum and
grow when approached. Directional lights use a sun glyph. Selected spotlights
show compact inner and outer cone wireframes; selected point and spot lights
also show the spherical or disk emitter represented by **Emitter radius (game
units)**. New emitters use 0.05 metres divided by the map world scale (2.0 game
units at Xash's 0.025 m/unit scale). Very small radii concentrate flux over a
small area and can produce noisy denoiser results.

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

Exactly one authored light is kept as the volumetric source whenever the list
is non-empty. If its owner is removed, the brightest directional light is used
first, otherwise the brightest remaining light. The empty authored-light set
remains intentional and valid.

Every completed action is saved using a validated sibling temporary file and
atomic replacement. The first version of each touched file is copied to the
runtime data directory's timestamped `editor-backups` session folder. Failed
saves stay live and visibly unsaved. Clean external changes reload
automatically; conflicting changes require an explicit reload or cancellation.

## Renderer dependency

The tracked dependency patch is documented in
`patches/raytracedgl1/README.md`. The renderer interface is version 4; the
client DLL interface remains version 10.
