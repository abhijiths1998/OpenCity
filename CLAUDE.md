# OpenCity: Project Conventions

Solo open-world action game. Unreal Engine 5.8.x, C++, native Linux, Vulkan SM6. Fully original: no names, assets, logos, story, characters, map or UI from any existing game, and no real brands.

Read `ARCHITECTURE.md`, `ROADMAP.md` and `ART_PIPELINE.md` before making changes. Work on one milestone at a time, and don't start the next without approval.

## Environment

- Engine: `~/UnrealEngine` (prebuilt). Scripts read `UE_ROOT` and default to that path.
- Project: `~/Projects/OpenCity`
- Compiler: the toolchain bundled with Unreal. Don't use system gcc or clang to build.
- The editor must run through XWayland (`SDL_VIDEODRIVER=x11`). Never use `SDL_VIDEODRIVER=wayland`.
- GPU has 6 GB and is shared with the desktop. RAM is 24 GB, CPU 4 cores / 8 threads.

## Commands

The `scripts/` folder is created in M0. These commands don't exist until then.

| Task | Command |
|---|---|
| Build editor target | `scripts/build.sh` |
| Launch editor | `scripts/run_editor.sh` |
| Run headless tests | `scripts/test.sh [filter]` |
| Run editor Python | `scripts/run_python.sh <script> [args]` |
| Package Linux Shipping | `scripts/package.sh` |
| Generate clangd database | `scripts/gen_clangd.sh` |

Underlying tools:

- `Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh`
- `Engine/Build/BatchFiles/Linux/Build.sh OpenCityEditor Linux Development -Project=<path>.uproject`
- `Engine/Build/BatchFiles/RunUAT.sh`

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
- Don't change the engine version or `r.Substrate`, or enable Experimental features (Mesh Terrain, Fast Geometry Streaming) without approval.
- No `git push --force`, history rewrites or LFS pruning without approval.
- No Fab, Bridge or Megascans dependency, and no asset without a row in `SourceArt/LICENSES.csv`.
