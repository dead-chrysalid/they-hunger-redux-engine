# Enemy ragdolls experiment

Feature: `ragdolls-experiment`, branch `codex/ragdolls-experiment`.
This is an isolated, single-player experiment. The first build received positive
user feedback for stable deaths and terrain contact. The follow-up addresses
save/load, the cost of old corpses, buoyancy, and post-death weapon hits; these
changes await gameplay feedback.

## Playing and reporting feedback

From the unified project root, run:

```powershell
.\tools\StillHunger.cmd launch-feature -Name ragdolls-experiment
```

The feature launches its own `work/ragdolls-experiment/runtime` installation.
Ragdolls are enabled by default. Kill walking enemies normally. For a controlled
check, open the game console, face a clear area with floor in front of you, and run:

```text
sv_cheats 1
impulse 101
cl_ragdoll_debug 1
ragdoll_spawn monster_zombie
```

Shoot the spawned enemy, or use `ragdoll_testkill` to kill all enemies created by
`ragdoll_spawn` without gibbing. `ragdoll_status` reports the simulated corpses,
body counts, active/frozen state, dynamics solver body count, and largest joint attachment error. Repeat with
`monster_human_grunt`, `monster_alien_slave`, `monster_houndeye`,
`monster_bullchicken`, `monster_headcrab`, `monster_th_chicken`,
`monster_th_zombiebull`, `monster_th_babykelly` and `monster_th_megasquid`.
Friendly scientists/Barneys and assassins are also accepted by the spawn command.
The spawn command requires cheats and open space; large creatures need more room
than its initial human-sized occupancy probe checks.

Please report the map, enemy class, weapon, and whether you saw stretching,
unwanted bouncing, clipping through terrain, floating, disappearance, or a poor
transition from the live pose. Stairs, slopes, ledges, doors and elevators are
particularly useful feedback cases. Visual checks are deliberately left to the
player; no computer-use evaluation was performed.

To compare original deaths, set `sv_ragdolls 0` before spawning/killing new actors.
`cl_ragdolls 0` removes the current client simulations. Set both back to `1` to
resume. Changes affect future deaths; existing server corpses retain their death
mode. Ordinary gibbing remains enabled for normal combat.

## Follow-up checks

Create a **fresh save with this build**, then load it: physical pose, velocity,
and frozen state are now stored inside the save archive. Older saves cannot
recover a physical pose they never stored, so they use a lifted, quiet animation
pose instead of replaying the lethal impulse into the floor.

Leave roughly 20 killed actors for 15-20 seconds. `ragdoll_status` should show
frozen actors and very few or zero limb bodies in the dynamics solver. The visible
corpses remain. Shoot a settled corpse: it should emit blood, move, and wake into
simulation, then settle again. Doors/elevators overlapping frozen actors also
wake them. The budget remains 64 corpses; no old corpse is removed to reduce
simulation cost.

Kill a walking actor over or in a water pool. Its limbs should rise toward the
surface and settle floating. Reload a fresh save there and repeat the hit test.
Buoyancy uses the BSP liquid column, not the RT visual water mesh. Static world
water/slime is supported; moving brush water and cosmetic glTF water are outside
this experiment.

## Research, checked 2026-09-30

| Primary source | Findings and relevance |
| --- | --- |
| [HL1RagdollMod](https://github.com/anchurcn/HL1RagdollMod) | GoldSrc studio-bone override with a managed GoldsrcPhysics dependency. Useful precedent for replacing the corpse pose, but its hook/runtime is unsuitable for this renderer. |
| [HL1RagdollMod-goldsrc](https://github.com/anchurcn/HL1RagdollMod-goldsrc) and [gsphysics](https://github.com/anchurcn/gsphysics) | Native successor with Bullet and articulated bone bodies. Checked source and submodule dependencies. Its native GameStudioModelRenderer integration confirms the pose/body separation. |
| [GoldsrcPhysics](https://github.com/anchurcn/GoldsrcPhysics) | Earlier managed implementation; inspected as background rather than introducing a managed runtime dependency. |
| [MetaHookSv](https://github.com/hzqst/MetaHookSv), [BulletPhysics documentation](https://github.com/hzqst/MetaHookSv/blob/main/docs/BulletPhysics.md) | Existing monster ragdolls, death-sequence recognition, body/constraint profiles, BSP geometry and moving-brush collision. Read BulletManager and BulletRagdoll source. The SvEngine hooks and animation-number detection are unsuitable here; reliable death metadata comes from our SDK instead. No MetaHook implementation code was copied. |
| [PrimeXT](https://github.com/SNMetamorph/PrimeXT) | Xash-based PhysX rigid physics, but its documented ragdoll work is still planned. Not a drop-in monster ragdoll implementation. |
| [XashXT](https://github.com/FWGS/XashXT) and [hlsdk-xash3d](https://github.com/tyabus/hlsdk-xash3d) | Checked related forks/physics work. Whole-entity corpse toss/prop physics is distinct from an articulated skeleton; did not find a usable ready-made Sultim RT ragdoll port. |
| [Bullet example](https://github.com/bulletphysics/bullet3/blob/master/examples/Benchmarks/BenchmarkDemo.cpp) | Official body/constraint setup reference. The vendored, unmodified library is Bullet 3.25, commit `2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5`, under its zlib license. |

The implementation below is original integration code. Only Bullet is vendored.
Its provenance and license are in `3rdparty/bullet`.

## Implementation

The Sultim RT studio path runs inside native `ref_gl` and bypasses the SDK studio
renderer. Consequently the physics world and matrix override live in `ref_gl`.
Bullet is statically linked, with no extra runtime DLL or download during builds.
Client API 10 and renderer API 4 remain unchanged.

The server marks eligible walking/stepping monsters through existing entity-state
extension fields: marker, death flag, hitgroup, birth time and impact velocity.
`HungerRedux/delta.lst` includes those fields. A live world-space bone pose is
cached, then used at death when compatible and recent; otherwise the current
animation pose is the seed. Headless model swaps validate the bone hierarchy.
Normal AI death schedules, triggers, weapon drops, fading and gibbing still run.
The obsolete server corpse hull becomes nonblocking.

Bodies come from studio hitboxes, with capsule limb shapes and broad box shapes
for oversized trunks. Bones with no collider inherit their captured local pose.
Nearest physical ancestors are connected with cone/twist constraints. The enlarged
megasquid needs broad contact patches and freer trunk joints. Self-collision is
disabled because the original hitboxes overlap; other corpses still collide.
Constrained masses and stronger joint attachment correction stabilize small spine
links. The simulation uses Z-up, an inch-to-metre conversion, fixed 120 Hz steps,
engine gravity, damping, sleeping and CCD. It does not advance when game time is
paused. Bounds follow the actual physics bodies for rendering/culling.

Static BSP surfaces provide world collision; solid brush entities provide moving
kinematic collision. Maps, hidden/removed corpses, entity reuse and model changes
retire the associated physics state. The budget is 64 corpses, 64 bodies per
corpse; extra deaths retain their original animation until a slot is available.

Eligibility follows walking/stepping monsters, including zombies, human troops,
aliens, small crawling creatures and the custom Hunger creatures. Scripted giant
bosses, turrets, furniture, generic scripted actors and G-Man retain original
behavior. Flying/swimming creatures and map-authored pre-dead corpses are excluded.

## Persistent and settled state

Optional local companion exports connect engine save/point traces to the renderer;
API-10/API-4 tables and versions remain unchanged. `SAVE/<map>.HL4` joins the engine's
existing `*.HL?` archive entries, including quicksave/autosave rotation and cleanup.
It stores model identity, a rig signature, the original constraint pose, current
bone/body transforms, velocities, and frozen state. Entity indices are remapped
through the engine restore table; no assumption of unchanged edict slots is made.
Only validated matching rigs consume restored records. Undrawn restored actors
retain their state through another save.

After an assembly settles, its limb bodies and constraints leave the dynamics
world. One static compound collider preserves corpse-on-corpse collision and
precise hit tracing. Matrix/bounds caches avoid recalculating frozen poses, and
native animation bone setup is skipped for simulated corpses. Unchanged brush
transforms no longer update/wake collision islands. Settlement is measured from
actual pose movement, with a looser check for old nearly stationary rigs, because
constraint-correction velocities can prevent ordinary Bullet sleeping indefinitely.
Hits and overlapping moving brushes restore the limb bodies/constraints. New hits
reset the settlement timer. Weapon impulses target the actual hit location;
movement hull traces remain unchanged, so corpses stay nonblocking.

## Validation and limits

The engine solution runs `tests/ragdoll_physics_tests.cpp` as part of the build.
It checks fixed-step behavior at 30/60/144 render Hz, pause, floor collision,
finite poses, attachment error, duplicate deaths, entity reuse, removal, budgets,
map reset and malformed bone hierarchy. Additional regressions check active and
frozen snapshot round trips, corrupt state rejection, legacy floor recovery,
ray-hit occlusion, waking on an impulse, a moving platform carrying a frozen
corpse, buoyancy equilibrium and restored floating poses. It also loads and simulates 29 shipped
model variants, including headless models, vortigaunt, houndeye, chicken,
headcrab, bullsquid and megasquid. These are numerical tests of the actual model
skeletons, not a claim of visual acceptance in gameplay. Renderer unit tests and
a bounded module-loading smoke check complement the build.

This first experiment has generic joint limits, rather than individually authored
anatomical rigs. Collider bodies approximate the mesh; thin fingers, toes and
unboxed appendages may intersect surfaces. Invisible BSP clip hulls and cosmetic
glTF scene geometry are not part of the triangle collision mesh. Sky and water
surfaces are excluded from solid collision; BSP liquid sampling supplies buoyancy.
Bodies remain nonblocking. Point-traced bullets and melee can hit the actual moved
colliders; swept projectile hulls and explosion impulses are not included in this
follow-up. The renderer still submits visible corpse meshes to RT each frame:
the numerical benchmark measures the physics saving, not end-to-end game FPS.
These areas should be considered during feedback before integration into master.

The 20-zombie numerical benchmark retained all 20 corpses and reduced the dynamics
solver from 400 limb bodies to zero after settling. The measured first 60 frames
cost about 239 ms of physics time total; the frozen 60-frame loop was below 0.01 ms.
This excludes skinning, mesh submission and RT acceleration-structure/render costs.
Gameplay performance still needs user feedback.
