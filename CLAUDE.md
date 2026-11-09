# OpenCity: Project Conventions

Solo open-world action game. Unreal Engine 5.8.x, C++, native Linux, Vulkan SM6. Fully original: no names, assets, logos, story, characters, map or UI from any existing game, and no real brands.

Read `ARCHITECTURE.md`, `ROADMAP.md`, `ART_PIPELINE.md` and `STORY.md` before making changes. World layout, mission content and radio text must follow `STORY.md`. Work on one milestone at a time, and don't start the next without approval.

## Environment

- Engine: `~/UnrealEngine` (prebuilt). Scripts read `UE_ROOT` and default to that path.
- Project: `~/Projects/OpenCity`
- Compiler: the toolchain bundled with Unreal. Don't use system gcc or clang to build.
- The editor must run through XWayland (`SDL_VIDEODRIVER=x11`). Never use `SDL_VIDEODRIVER=wayland`.
- GPU has 6 GB and is shared with the desktop. RAM is 24 GB, CPU 4 cores / 8 threads.

## Commands

| Task | Command |
|---|---|
| Build editor target | `scripts/build.sh` |
| Launch editor | `scripts/run_editor.sh` (starts at Medium quality: Epic ran out of VRAM on Play in L_Marevia) |
| Play a map as the game (no editor UI) | `scripts/play.sh [Map]` (default L_Marevia) |
| Run headless tests | `scripts/test.sh [filter]` |
| Run editor Python | `scripts/run_python.sh <script> [args]` |
| Package Linux Shipping | `scripts/package.sh` |
| Generate clangd database | `scripts/gen_clangd.sh` |
| Perf run (fixed camera path) | `scripts/perf_run.sh [Map] [Preset 0-3] [Laps]` |
| Plugin load check | `scripts/plugin_smoke.sh [Plugin,...]` |
| Stability gate (60 min) | `scripts/stability/stress_gate.sh [minutes]` |
| Freeze capture (sudo) | `sudo scripts/stability/install_freeze_capture.sh [--remove]` |
| Recreate M0 test maps | `scripts/run_python.sh Content/Python/oc_m0_create_test_maps.py` |
| Rebuild M1 / M2 assets and maps | `scripts/run_python.sh Content/Python/oc_m1_setup.py` (then `oc_m2_setup.py`) |
| Verify M1 / M2 assets after reload | `scripts/run_python.sh Content/Python/oc_m1_verify.py` (and `oc_m2_verify.py`) |
| Regenerate the Marevia layout (roads, districts, water, landmarks, gates, heightmap, preview) | `python3 -I Tools/make_marevia_layout.py` → `SourceArt/World/` |
| Rebuild the Marevia world map from SourceArt/World | `scripts/build_world.sh [--regen]` (deletes and recreates the map; needs the GPU, about 80 s with warm shaders) |
| Build World Partition HLODs (after every world rebuild; needs the GPU) | `scripts/build_hlod.sh [Map] [--delete]` |
| Check the Marevia map against the M3 acceptance list | `scripts/run_python.sh Content/Python/oc_m3_verify.py` (`OC_VERIFY_HLOD=1` also requires built HLODs) |
| Regenerate the bike chassis | `python3 -I Tools/make_bike_chassis.py`, then `oc_m2_setup.py` |

Underlying tools:

- `Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh`
- `Engine/Build/BatchFiles/Linux/Build.sh OpenCityEditor Linux Development -Project=<path>.uproject`
- `Engine/Build/BatchFiles/RunUAT.sh`

Notes:

- Scripts share settings in `scripts/common.sh`: engine path, 6 parallel compile jobs, build accelerator bound to localhost.
- DDC is the engine's local Zen server at `~/.config/Epic/UnrealEngine/Common/Zen/Data`, outside the repo and shared by all projects. The first cook compiles every shader (about 11 min); later ones reuse it.
- `build.sh` fails on any compiler warning in project code. Tests write reports to `Saved/Automation/`.
- Later-milestone plugins (Water, Mass, ZoneGraph, CommonUI) are absent from `OpenCity.uproject` on purpose. Add them in their milestone.
- Scripted runtime checks: non-Shipping builds have `oc.dev.*` console commands (`status`, `goto X Y Z`, `interact`, `exitvehicle`, `drive T S B`, `impact KMH`, `radio`, `resetvehicle`, `flip`, `tilt DEG`, `spawn coupe|offroad|bike`, `unlock <Story.Mission.X | X | all>`, `progress`, `resetprogress`, `route X0 Y0 X1 Y1 [...] [kmh=N] [quit]` autopilots the driven vehicle along the lane graph and logs STUCK/FLIPPED/OFFROUTE/NOGROUND plus a summary). `-ocdevscript="2:oc.dev.status;3:oc.dev.interact;9:quit"` runs them on a timeline and the results land in the log. Use these to verify gameplay changes headfully without input.
- World checks: `oc.dev.viewfrom X Y Z Pitch Yaw [FOV]` places a camera (cm); `oc.dev.ground X Y` logs terrain height at layout metres. World Partition streams around the *camera*, so for overview shots first run `wp.Runtime.OverrideRuntimeLoadingRange -grid=MainPartition -range=600000` (not via `-dpcvars`: that crashes at startup).
- `oc.dev.viewfrom` freezes the player character until `oc.dev.viewfrom off`, so it doesn't fall through ground that unloads while the camera is away.
- Landscapes must be built with a GPU renderer (`OC_PY_RENDER=1` adds `-AllowCommandletRendering`). Without it collision gets the heights but the terrain renders flat.
- Don't combine `rm` with `bash -c` scripts: Claude Code's safety check blocks it. To pick out new screenshots, `touch` a marker file and use `find -newer`.
- Editor runs need about 4 GiB free RAM. Browsers and Claude Code's own `clangd` (`--background-index`) can starve them: kill it with `pkill -f '^clangd '` (an unanchored pattern kills the calling shell).

## Code conventions

- Unreal prefixes `A`, `U`, `F`, `E`, `I`, `T`, followed by `OC`: `AOCCharacter`, `UOCWantedSubsystem`, `IOCDamageable`, `FOCHitInfo`, `EOCHeatLevel`.
- Modules: `OpenCityCore`, `OpenCity`, `OpenCityEditor`.
- Features live in folders in `OpenCity`. Features talk through `OpenCityCore` interfaces, subsystems and events, never each other's concrete classes.
- Small classes. Prefer components and interfaces to a large character class.
- `#include` paths must match file case exactly. Linux is case-sensitive.
- No Windows-only APIs or plugins.
- Blueprint subclasses hold tuning and art hookup only.
- Balance values go in DataAssets and DataTables, never in C++.

## Performance rules

- `PrimaryActorTick.bCanEverTick = false` by default. A Tick needs a reason in a comment.
- Prefer timers, events and delegates.
- Pool projectiles, NPCs, traffic, decals and effects.
- Only `UOCWorldStateSubsystem` writes `MPC_World`.
- Respect the budgets in `ARCHITECTURE.md` section 8. Report any breach.
- Measure with browsers closed, in standalone or packaged builds. Play-in-editor numbers aren't comparable.

## Asset conventions

| Prefix | Type |
|---|---|
| `T_` | Texture (suffix `_BC`, `_N`, `_ORM`, `_E`, `_M`) |
| `M_` / `MI_` / `MF_` | Material, instance, function |
| `MPC_` | Material Parameter Collection |
| `SM_` / `SK_` | Static, skeletal mesh |
| `BP_` | Blueprint |
| `ABP_` | Animation Blueprint |
| `NS_` | Niagara System |
| `DA_` / `DT_` | Data asset, table |
| `IA_` / `IMC_` | Input action, mapping context |
| `WBP_` | Widget Blueprint |

Full rules are in `ART_PIPELINE.md`.

## Folder layout

```text
Config/  Source/  scripts/  Tools/
SourceArt/         manifest.json, LICENSES.csv, Blender files
Content/
  Core/            Shared data assets, input, game modes
  Characters/  Vehicles/  Weapons/
  Environment/     Materials, Meshes, Foliage, Decals
  FX/  Audio/  UI/  Missions/
  Maps/            City, Test
  Materials/       Master materials and functions
  Python/          Editor scripts
```

## Workflow rules

- Build after every C++ change, and fix all errors and warnings before calling a step done.
- Run tests for any logic touched.
- Small commits after each working step. Commit before long or risky editor sessions, because this machine has had hard GPU freezes.
- Use Editor Python for repeatable asset work. If a step must be done by hand, give a short checklist with exact menu paths.
- Say plainly when something could not be tested headless, and what the user needs to check in the editor.
- Report real measurements only. Never present a Target as a result.

## Don't touch

- `~/UnrealEngine`: no engine edits.
- `IgnorePkg` in `/etc/pacman.conf`: the NVIDIA driver is pinned deliberately.
- Don't hand-edit `.uasset` or `.umap` files, or run asset-changing scripts while the editor has the same project open.
- Don't commit `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, or raw CC0 downloads.
- Don't change the engine version or `r.Substrate`, or enable Experimental features without approval. Approved exceptions are listed in `ARCHITECTURE.md` section 10 (Chaos Vehicles, Water, Mass, ZoneGraph). Mesh Terrain and Fast Geometry Streaming are not approved.
- No `git push --force`, history rewrites or LFS pruning without approval.
- No Fab, Bridge or Megascans dependency, and no asset without a row in `SourceArt/LICENSES.csv`.
