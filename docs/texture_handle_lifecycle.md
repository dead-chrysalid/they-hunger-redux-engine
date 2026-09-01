# Engine effect sprite texture lifetime

## Failure

The built-in muzzle-flash, tracer-dot, glow, ricochet, and shell sprite models
persist across map changes. Their frame texture IDs were previously allocated
in the same `#HUD/` name namespace as client sprite registrations. A per-map
HUD sprite sweep could therefore delete a shared named texture while the
engine model continued caching its numeric slot. Reuse produced an unrelated
effect texture; an empty slot produced `GL_Bind: invalid or stale texturenum`
and the purple default texture.

## Ownership rule

- Engine-owned effect sprites use `SPR_ENGINE` and `MODEL_ENGINE_SPRITE`.
- Their renderer names live under `#ENGINE/`, never `#HUD/`.
- The map registration path revalidates the seven built-in sprites after the
  HUD/client sprite sweep.
- All old-map model and client-sprite destruction completes before `R_NewMap`
  allocates the next skybox and lightmaps. This prevents an old numeric handle
  from deleting a newly allocated texture that reused its slot.
- `R_GetSpriteTexture` rejects deleted GL slots before `TriSpriteTexture` can
  draw the purple missing-texture fallback.
- Grouped sprite teardown frees frame `j` rather than repeatedly freeing frame
  `i`, and all client-sprite sweeps now index the same slot they test.
- Studio and alias remaps free only their own backing storage. Studio remaps
  may exceed `MAX_SKINS`; their variable-length texture count must never index
  the fixed alias handle array, or texture-name bytes can be freed as handles.
- `GL_Bind` retains one diagnostic per stale slot, but suppresses per-frame
  repetition until that slot is allocated again.

This keeps renderer API 4 unchanged; the ownership marker is an internal
`model_t` flag shared by the paired engine and ref_gl build.
