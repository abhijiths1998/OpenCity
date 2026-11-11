# OpenCity: Architecture

Status: **draft, awaiting approval**. No code exists yet.
Working titles: project `OpenCity`, fictional city `Marevia`. Both are placeholders.

Labels used below:

- **Verified**: checked on this machine or in Epic's docs on 2026-11-07.
- **Target**: a number we aim for.
- **Check in Mx**: unknown until tested in that milestone.

## 1. Fixed decisions

| Area | Decision |
|---|---|
| Engine | Unreal Engine 5.8.x prebuilt Linux binaries at `~/UnrealEngine`. Stay on 5.8 for the whole project, with no move to UE6 (due 2027) |
| Game type | Single-player only. No replication code, no dedicated server |
| Performance | 1080p output, 60 FPS on Medium, by upscaling. **When visuals and frame rate conflict, frame rate wins** |
| VRAM | 5,000 MiB for the game, measured with browsers closed |
| Setting | Present-day fictional Mediterranean-style coast: 3 × 3 km playable (city, hills, a mountain band) plus non-playable distant peaks and an offshore island; see `STORY.md` |
| Assets | CC0 sources plus original Blender work. Megascans/Fab are not part of the plan |
| Rendering API | Vulkan SM6 only |

## 2. Hardware and platform facts

| Item | Value | Effect on the design |
|---|---|---|
| GPU | RTX 3050 6 GB Laptop, driver 610.57.04 (pinned), dGPU-only mode | Below Epic's recommended 8 GB. Everything on the desktop shares this card |
| VRAM already in use | 1,362 MiB with browsers open | Close browsers when measuring and for long editor sessions |
| CPU | Ryzen 5 7235HS, 4 cores / 8 threads | Likely the limit for physics, AI and streaming, so traffic is kinematic (section 7) |
| RAM | 23 GiB plus 8 GiB zram; 16 GiB in use at idle with browsers | Below Epic's recommended 32 GB. Cap parallel compiles |
| Disk | 214 GB free on btrfs | Engine takes about 70 GB after extraction; delete the 39 GB zip afterwards |
| Session | Plasma 6.8 on Wayland, no X11 session installed | Run the editor through XWayland with `SDL_VIDEODRIVER=x11`. Epic calls pure Wayland unusable for the editor |
| Vulkan extensions | `VK_KHR_shader_atomic_int64` present | Nanite and VSM requirement met (verified) |
| OS | Arch | Not an Epic-supported dev OS (they list Ubuntu 22.04 and Rocky 8). Expect some unsupported issues |

### Stability risk

Hard GPU freezes during games were unresolved at last record, on every kernel and driver tried, and the freeze-capture sysctl config is not installed. The editor loads the GPU in a similar way, though natively rather than through Proton. So:

- M0 has a stress gate before any feature work.
- Autosave runs every 5 minutes, and we commit after every working step.
- `scripts/run_editor.sh` caps the editor at 60 FPS (`t.MaxFPS`).
- If a freeze happens, run `~/Projects/forge-os/utils/freeze-report.sh` first. A freeze in native Vulkan would also be useful evidence, because it rules out Proton.

**M0 stability gate passed (2026-11-07, 17:40 to 18:40):** 60 minutes of the stress map, uncapped at about 63 to 66 FPS, with no crash, GPU error or freeze. Freeze capture was active. Heat ran higher than in the games that froze: GPU held at 87 C with driver thermal slowdown and clocks at about 1,600 to 1,990 MHz; CPU averaged 92 C and peaked at 100 C; NVMe reached 78 C. Sustained full load is thermally limited on this laptop, so long sessions should stay frame-capped.

## 3. Where this design differs from the brief

| Brief | Change | Why |
|---|---|---|
| Mass/ZoneGraph for traffic and crowds | **Pooled actors on our own lane graph first.** Mass gets a time-boxed spike in M5 and is used only if it beats the fallback | Steep for someone new to UE, and the fallback is easier to debug. Crowd sizes this CPU can run don't need Mass yet |
| GAS for all health and damage | **GAS for the player, police and armed NPCs only.** Ambient pedestrians use a light health component behind the same `IOCDamageable` interface | An ability system component on every pooled pedestrian wastes memory and CPU |
| Chaos Vehicles for the motorbike | **Hidden four-wheel chassis with stabilising torque and visual lean** | Standard wheeled vehicles don't balance on two wheels. Biggest risk in M2. Chaos Modular Vehicles is the alternative to evaluate |
| M4 as one milestone | **Split into M4a (pipeline, three key materials, world state) and M4b (remaining materials, weather, final tuning)** | Too large for one step, and character and vehicle materials are better tuned against real content |
| 4 GB limit | 5,000 MiB budget | The card has 6 GB (your decision) |
| All plugins on at M0 | Mass, Water and CommonUI are smoke-tested at M0, then **switched off until their milestone** | Faster editor start, less memory, fewer shaders |
| Star-based wanted display | Same 1 to 5 mechanic, shown with an **original "heat" design** | Keeps the HUD's look distinct from existing games |
| Water plugin assumed | Spike in M3, with a custom ocean plane as fallback | Maturity and World Partition behaviour are unverified. The game only needs one coastline |
| Substrate if stable | **Off for v1**, standard shading models | Reported production-ready since 5.7, but the car paint needs only Clear Coat, and it's a project-wide shader rebuild for little gain on this GPU. Check in M0 that it is actually off |

**Not used in v1:** Mesh Terrain and Fast Geometry Streaming (both Experimental in 5.8), MegaLights (disabled to reduce shaders), MetaHuman (too heavy), hardware ray tracing.

### M1/M2 implementation notes

- **Input policy** (sprint toggles, crouch cancels, buffered jump) lives in `FOCLocomotionIntent` in OpenCityCore, so it is unit-tested. The character only forwards events.
- **Look input** depends on `bEnableLegacyInputScales=True` (pinned in `DefaultInput.ini`, same as Epic's template). Turning it off inverts pitch and changes speed.
- **Vehicles** copy `UOCVehicleData` into the Chaos component in `PostInitializeComponents`, then rebuild its physics. `AWheeledVehiclePawn` leaves simulation off, so `AOCVehicle` turns it on.
- **Motorbike**: hidden 40 cm-track chassis, roll stabiliser, and a yaw assist (bicycle model) that stops spin-outs. Handling values live in `UOCVehicleData::Motorbike`; visual wheels follow the chassis wheel bones.
- **Exit spots** need a clear line from the seat, ground below and room for the capsule. At speed, the vehicle brakes in its travel direction first.
- **Driving map performance** (packaged Development, Medium, daylight, 3 vehicles + character, no volumetric clouds): 96 to 98 FPS, GPU 9.7 ms, game thread 2.5 ms, 2,183 MiB game VRAM.
- **Vehicle art** is assembled from `UOCVehicleData::VisualParts` (body, glass, wheels attached to wheel bones), as Epic's template does. The chassis skeletal mesh alone shows only suspension.

## 4. Modules

```text
OpenCity.uproject
Source/
  OpenCityCore/     Interfaces, gameplay tags, shared types, DataAsset base classes.
                    Depends only on engine modules.
  OpenCity/         All gameplay, by feature folder. Depends on OpenCityCore.
    Player/ Vehicles/ Combat/ Population/ Wanted/ Missions/
    World/  UI/       Audio/  Save/
  OpenCityEditor/   Editor-only tools and validators. Never packaged.
```

Three modules keep rebuilds short without too much boilerplate. Feature folders inside `OpenCity` must not include each other's private headers. They communicate through `OpenCityCore` interfaces, subsystems and events. A feature moves to its own module only if rebuild time becomes a real problem.

All C++ types use the `OC` prefix: `AOCCharacter`, `UOCWantedSubsystem`, `IOCDamageable`. Tests live in `Private/Tests/` in each module.

## 5. Key classes

### Interfaces (OpenCityCore)

| Interface | Purpose |
|---|---|
| `IOCDamageable` | The only entry point for damage. Implemented by GAS actors, light NPCs, vehicles and props |
| `IOCInteractable` | Doors, pickups, vehicle entry, mission triggers |
| `IOCVehicleSeatProvider` | Lets a character enter a vehicle without knowing its type |
| `IOCPoolable` | `OnAcquired` and `OnReleased` reset hooks |
| `IOCSaveable` | Actors and subsystems that write save data |

### Subsystems

| Class | Scope | Owns |
|---|---|---|
| `UOCWorldStateSubsystem` | World | Time of day, weather state, sole writer of `MPC_World` |
| `UOCEventSubsystem` | World | Tag-based gameplay events: crimes, deaths, objective signals |
| `UOCPoolSubsystem` | World | Projectile, NPC, vehicle and decal pools |
| `UOCPopulationSubsystem` | World | Spawn and despawn rings around the player, density budgets |
| `UOCRoadGraphSubsystem` | World | Lane and intersection queries |
| `UOCWantedSubsystem` | World | Heat level, search zones, police dispatch requests |
| `UOCMissionSubsystem` | World | Active mission, objective state, checkpoints |
| `UOCSaveSubsystem` | GameInstance | Save slots, versioning |
| `UOCSettingsSubsystem` | GameInstance | Applies scalability presets and graphics options |

### Actors and components

- `AOCCharacter` stays small: movement and component wiring only. Behaviour lives in components: `UOCCameraAimComponent`, `UOCInteractionComponent`, `UOCVehicleOccupantComponent`, `UOCWeaponComponent`, `UOCLightHealthComponent`.
- `AOCPlayerController` owns input context switching. It possesses the character or the vehicle, and the character is attached and hidden while driving.
- `AOCVehicle` derives from `AWheeledVehiclePawn` and adds `UOCVehicleDamageComponent`, `UOCVehicleCameraComponent` and `UOCRadioComponent`.
- `AOCTrafficVehicle` is a separate cheap kinematic actor. It swaps to a pooled physics `AOCVehicle` on impact or when the player takes it.

### Story progression and area gates

- **`UOCProgressionSubsystem`** (GameInstance): the set of completed story milestones (gameplay tags such as `Story.Mission.Funerale`). It answers "is area X open?", broadcasts `OnMilestoneCompleted`, and is saved in M10. Missions (M8) complete milestones; dev builds have `oc.dev.unlock <tag|all>`.
- **`UOCAreaUnlockData`** (data asset): each area's required milestone and its World Partition Data Layers. The unlock schedule lives in data, not code.
- **World Partition Data Layers** hold each gate's two states, e.g. `Gate_PonteMobile_Raised` and `Gate_PonteMobile_Lowered`, plus each area's locked-only props (cordons, checkpoints). The subsystem switches layers at runtime, so gates cost nothing while closed and need no Tick.
- **`AOCLockedAreaVolume`** covers each locked area. Entering it shows a warning; staying triggers the push-back (a fade back to the last open area before M7, police heat after).
- **Water**: deep water is a physics volume that resets characters to the nearest bank and wrecks vehicles. No swimming in v1.
- **As built (M3):** milestones, areas (`Area.<District>`) and gates (`Gate.<Name>`, matching the road graph) are gameplay tags in `Config/DefaultGameplayTags.ini`. The rule logic is `FOCUnlockSchedule` in OpenCityCore (unit-tested); `DA_AreaUnlocks` and `DA_MareviaRoads` are generated by `OCWorldStory.cpp` and named in `UOCProgressionSettings` (DefaultGame.ini). Each gate has a runtime Data Layer `DL_Gate_<Name>_Locked` (barriers, the raised Ponte Mobile, the turned Ponte Girevole), and the two moving bridges also have `DL_Gate_<Name>_Open`. A push-back or water reset fades out, then moves the player to the nearest road node reachable from the start with the current gates, skipping any node inside a still-locked area, and waits for World Partition to stream in ground there. Vehicles that go into deep water are wrecked through `IOCDrivable`. Locked-area volumes are always loaded; river volumes stream with their cells.

### Data assets

`UOCWeaponData`, `UOCVehicleData`, `UOCNPCArchetype`, `UOCMissionData` (with instanced `UOCObjective` objects), `UOCRadioStationData` and `UOCWeatherPreset`. All derive from `UPrimaryDataAsset` and load through the Asset Manager. Balance values live in DataTables and data assets, never in C++.

## 6. How systems communicate

Rules:

1. Direct calls go downward only, from gameplay code to a subsystem or an interface.
2. Sideways messages use subsystem delegates or `UOCEventSubsystem`.
3. No feature `Cast<>`s to another feature's concrete class.
4. Blueprint subclasses hold tuning and art hookup only. Gameplay logic stays in C++.

Example: the player shoots a pedestrian.

```text
UOCWeaponComponent --hit--> IOCDamageable::ApplyDamage (target)
target dies --> UOCEventSubsystem: Event.Crime.Murder {location, instigator}
                  |-- UOCWantedSubsystem: witness check, raises heat
                  |     \-- OnHeatChanged --> HUD, UOCPopulationSubsystem (police spawns)
                  |-- UOCPopulationSubsystem: nearby NPCs enter panic
                  \-- UOCMissionSubsystem: fails objective if the target was protected
```

Example: weather changes.

```text
UOCWorldStateSubsystem (timer, about 10 Hz, blends values)
   |-- writes MPC_World: every material updates
   |-- OnWeatherChanged --> Niagara rain, audio, vehicle grip, NPC behaviour
```

## 7. World and population

- **World Partition:** one persistent level, 128 m grid cells, 384 m loading range (Target; cars reach about 50 m/s). One file per actor also suits Git LFS.
- **Data Layers:** mission-specific content and night-only props.
- **HLOD:** two layers, built by commandlet. Check in M3 that the builder runs headless on Linux.
  *As built (M3):* one layer, `/Game/World/HLOD/HLOD0_Instanced` (instancing; landscape proxies use the landscape's own baked HLOD), set as the map's HLOD setup by `UOCWorldBuilderLibrary::ConfigureHLOD`: 256 m cells shown out to 8 km, past the 384 m streaming range. A second, far layer was tried and dropped: an instancing layer can't represent the landscape (distant terrain vanished), approximating needs the Experimental MeshModelingToolsetExp plugin, and merging the ~880k kit pieces ran the builder out of memory. `scripts/build_hlod.sh [Map] [--delete]` runs the engine's `WorldPartitionHLODsBuilder` commandlet (needs the GPU). Rebuild HLODs after every `scripts/build_world.sh`.
- **Lanes:** `FOCLaneGraph` (OpenCityCore) derives directed lanes and junction links from the road graph (right-hand traffic; right turns from the outer lane, left from the inner, U-turns only at dead ends). `UOCRoadGraphAsset::GetLanes()` serves it to traffic (M5), police routing (M7) and the minimap (M9).
- **Water (M3 decision): custom surfaces, not the Water plugin.** Sea, river and reservoir are plain planes with `M_Dev_Water` (`OCWorldWater.cpp`), and deep-water gameplay is `AOCDeepWaterVolume`. Reasons: the plugin is Experimental; it brings things v1 doesn't use (buoyancy, swimming, a water-zone render target and its own mesh quadtree) that cost VRAM on a 6 GB card; its water bodies carve the landscape, while our generator already carves the heightmap from `marevia_layout.json`; and its World Partition and Linux behaviour is unverified. The plugin was **not** spiked at runtime: this decision rests on those grounds. Revisit only if boats come back post-v1. M4b replaces `M_Dev_Water` with `M_Master_Water` (translucency, waves, shoreline foam) on the same meshes.
- **Time of day and weather (M4a/M4b, as built):** `UOCWorldStateSubsystem` (world subsystem, 10 Hz timer, no Tick) is the only writer of `MPC_World` (`NightAmount`, `TimeOfDay`, `Wetness`, `RainAmount`, `Wind`). The sun follows `FOCSunModel` (unit-tested); at night the same light is a dim moon. A pool of 24 unshadowed sodium spot lights follows the street lamps nearest the camera at night. Weather presets (`UOCWeatherPreset`: `DA_Weather_Clear/Cloudy/Rain`) blend over `WeatherBlendSeconds`, scaling sun, sky light and fog and the sky atmosphere's Rayleigh/Mie; wetness soaks in while it rains (`SoakSeconds`) and dries slowly (`DrySeconds`). Local exposure in the map's post-process volume (`ApplyLook` in `OCWorldBuildCommon.cpp`: shadow contrast 0.6, highlight contrast 0.8) keeps shaded streets readable when a low sun lights the facades; it cost nothing measurable at Medium. By day the sky light scale and an exposure bias (`ExposureBias`, added to the unbound post-process volume's bias) compensate for the dimmed sun under overcast skies; at night both give way to the map's own night levels, so night rain keeps its look. Materials read the two MPC values: porous darkening, gloss, puddles with rain ripples (surface, road, landscape masters), rougher water, and `M_PP_Rain` screen rain. Console: `oc.tod <hour>`, `oc.tod.run`, `oc.weather <Clear|Cloudy|Rain> [seconds]`; command line `-octod=`, `-ocweather=`.
- **Scenery:** distant peaks, the coast past the east edge and Isola Ventura are collision-free, always-loaded, shadowless meshes (`Scenery_*`): CC0 rock scans scaled to hill size.
- **Networks (INFRA_DESIGN.md, Phase 2):** roads, rail and water are data in the layout (`the layout generator (scripts/make_layout.sh, Source/OpenCityEditor/Private/Layout/)`), loaded into `DA_MareviaRoads` (`FOCRoadGraph` with `FOCRoadProfile` cross-sections from `SourceArt/Infra/road_profiles.json`, `FOCRoadJunction` types, per-segment paving/era/wear/ZTL/parking/tram/one-way), `DA_MareviaRail` (`FOCRailNetwork`) and `DA_MareviaWater` (`FOCWaterNetwork`). `FOCNetworkValidator` (OpenCityCore, unit-tested) checks them; `oc_validate_networks.py` runs it on the assets. Italian rules: right-hand traffic, D.M. 5/11/2001 cross-sections, urban 50 and Zona 30 in the ZTL.
- **Terrain:** standard Landscape. RVT handles road and terrain blending.
- **City generation:** a road graph asset is the single source of truth. PCG reads it for roads and building blockouts. Traffic, the minimap and police routing read the same graph. PCG runs at **edit time only**, with results saved into the level.
- **Population rings (Targets):** physics vehicles within 80 m when needed, kinematic traffic up to 200 m, despawn beyond 250 m and out of view.
- **Budgets on Medium (Targets):** 24 traffic vehicles, 40 pedestrians, 8 police. Pools are allocated at load, so play causes no spawn spikes.
- **NPC AI:** State Tree, updating at 2 to 5 Hz based on distance. No per-frame AI on ambient NPCs.

## 8. Rendering budget

All figures below are **Targets to be replaced by measurements** in M0 and M4.

### VRAM: 5,000 MiB (packaged build, 1080p output, Medium, browsers closed)

| Category | MiB |
|---|---:|
| Texture streaming pool (`r.Streaming.PoolSize=2000`) | 2,000 |
| Render targets, TSR history | 650 |
| Non-Nanite meshes, skeletal meshes, GPU Scene | 600 |
| Nanite streaming pool | 400 |
| Lumen scene | 350 |
| Shadows (VSM physical pages) | 300 |
| Niagara, decals, other | 150 |
| Headroom | 550 |
| **Total** | **5,000** |

The desktop takes roughly 800 MiB of the remaining 1,144 (an estimate from the current per-process list). If `stat unit` reports over budget, cut the texture pool first. UE 5.8 shows VRAM used and budget directly in `stat unit` on discrete GPUs.

### Frame time: 16.6 ms

| GPU pass | ms |
|---|---:|
| Nanite and base pass | 3.5 |
| Shadows | 2.5 |
| Lumen GI and reflections | 4.0 |
| Post-process and TSR | 2.0 |
| Translucency, particles, decals | 1.0 |
| Other | 1.5 |
| Margin | 2.1 |

CPU targets: game thread and render thread each under 12 ms.

### M0 measured baseline (2026-11-07)

> **Correction (later the same day):** the M0 numbers below were measured with the sun *below the horizon*. The setup scripts passed rotations to `unreal.Rotator` as (pitch, yaw, roll), but its Python order is (roll, pitch, yaw). Re-measured in daylight, Shipping, Medium, browsers closed:
>
> | Map | FPS | GPU ms | Render thread ms | Game VRAM (MiB) |
> |---|---:|---:|---:|---:|
> | `L_PerfBaseline` (empty, volumetric clouds) | 63 | 15.4 to 15.6 | 2.2 to 2.3 | 1,209 |
> | `L_DevDriving` (3 vehicles + character, no clouds; Development package) | 96 to 98 | 9.7 | 8.5 to 8.9 | 2,183 |
>
> **An empty daylight level cost 15.5 ms GPU at Medium.** Volumetric clouds were the main cost (~5.9 ms in an A/B test). Fixed the same day: no volumetric clouds on Low/Medium, cheap settings on High. **Shipping, Medium, empty level now: 127.5 FPS, GPU 7.5 ms, 1,199 MiB.**

Empty lit level `L_PerfBaseline` (sky atmosphere, volumetric clouds, height fog, movable sun and sky light, 400 m ground plane), 1920x1026 window, GPU at 60 to 80 C. A browser was open, so whole-GPU VRAM is about 0.4 GB higher than with browsers closed.

| Build | Preset | Avg FPS | GPU ms | Render thread ms | Game VRAM (MiB) |
|---|---|---:|---:|---:|---:|
| Shipping (first-run default) | Medium | 80 to 82 | 12.0 to 12.2 | 2.2 to 2.3 | 1,202 |
| Shipping (engine default, before fix) | Epic, 100% res | 51 | 19.2 | 7.7 | not sampled |
| Editor `-game`, uncooked | Low | 129 to 143 | 6.5 to 7.4 | 7.0 to 7.7 | 1,161 |
| Development package, `L_Stress` (900 Nanite meshes, 24 shadowed lights) | Medium | 77 | 12.7 | 13.1 | 2,277 |

Findings:

- **An empty level already costs 12 ms GPU at Medium**, leaving about 4.5 ms for the city. The budget table above is not reachable without cuts.
- **Volumetric clouds cost about 3.5 to 4.5 ms** (interleaved on/off runs in a packaged Development build). Low now disables them. Medium cloud quality is the first M4 decision.
- One profiled Medium frame also showed TSR at about 2.6 ms and the global distance field update at about 1.8 ms. Lumen GI runs on async compute, about 5 ms overlapped.
- `RHIGetGPUFrameCycles` reports close to the full frame time when the render thread is the bottleneck. Only trust GPU ms when render-thread ms is clearly lower (Shipping).
- Laptop heat moves results by 10 to 20%. Compare runs made back to back, and interleave A/B tests.

### Scalability presets

The brief requires presets that really change the output. Low must be a true fallback.

| Setting | Low | Medium (target) | High | Ultra |
|---|---|---|---|---|
| Render scale to 1080p | 58% | 67 to 75%, dynamic | 83% | 100% |
| GI | Screen-space plus sky ambient | **Lumen Lite** (Beta) | Lumen software | Lumen software, higher quality |
| Reflections | Screen-space | Lumen Lite | Lumen | Lumen |
| Shadows | Cascaded | VSM reduced, or cascaded if measured faster | VSM | VSM |
| Clouds | None (painted sky layer in M4) | **None** (measured: clouds cost ~5.9 ms) | Volumetric, cheap settings | Volumetric, full |
| Foliage density | 40% | 70% | 100% | 100% |
| View distance scale | 0.6 | 0.8 | 1.0 | 1.2 |
| Texture pool (MiB) | 1,200 | 2,000 | 2,400 | 2,800 |
| Traffic and crowd | 50% | 100% | 120% | 150% |

High and Ultra exist for stronger GPUs. This laptop is not expected to hold 60 FPS on them. Dynamic resolution is available on Vulkan in 5.8 and helps hold 60 on Medium. Whether DLSS works on Linux is unverified: **check in M4**, and TSR remains the baseline either way.

**Measured preset costs (2026-11-09, `L_Marevia` perf path, editor `-game`, 1080p, browsers open):**

| Preset | Avg FPS | 1% low | GPU ms | Game VRAM (MiB) |
|---|---:|---:|---:|---:|
| Low (engine Low: no sun shadows) | 98.6 | 59.4 | 4.6 | 3,770 |
| Low with basic sun shadows (as shipped, `r.ShadowQuality=2`) | 83.8 / 82.4 | 40.1 / 41.4 | 6.8 / 7.4 | 4,271 |
| Medium | 83.3 | 40.9 | 11.3 | 3,418 |
| Medium, after the static-switch material cut (21:00) | 79.8 / 77.2 | 39.3 / 34.0 | 12.0 / 12.3 | 3,982 |
| Medium, with local exposure (21:45) | 86.3 / 82.3 | 43.8 / 42.5 | 11.0 / 11.6 | 3,983 |
| Medium, foliage master with wind (2026-11-10 00:37) | 91.7 / 94.3 | 43.5 / 46.3 | 9.2 / 9.5 | 3,752 |
| High (cool machine) | 53.0 | 25.4 | 18.0 | 4,388 |
| High (same settings, after 25 min of runs) | 28.1 | 15.8 | 34.8 | 4,271 |
| Ultra | out of VRAM at startup with browsers open | | | 4,211 peak |

Low now keeps basic sun shadows: the engine's Low turns them off and streets read flat. With them, Low is render-thread bound in editor `-game` (~12 ms), so it lands near Medium's FPS here while using ~40% less GPU. High swings by 2x with heat alone: the render thread went from 10 to 35 ms over one back-to-back batch while the CPU sat at 78 C with its clock capped. Single-cvar A/B tests on High (`r.DistanceFieldShadowing 0`, `r.Lumen.TraceMeshSDFs 0`, `r.VolumetricFog 0`) stayed inside that noise, so High keeps the engine values (`Config/DefaultScalability.ini` has the numbers). For A/B tests use `OC_PERF_EXEC="r.Foo 0" scripts/perf_run.sh ...`: `r.*` cvars passed with `-dpcvars` crash this build at startup with a Vulkan out-of-memory.

Lumen Lite is the basis of the Medium plan: it is reported at about 2x the speed of high-quality Lumen, using `sg.GlobalIlluminationQuality 1` and `sg.ReflectionQuality 1`. **It is Beta and untested here.** If it fails on Linux Vulkan, Medium falls back to reduced software Lumen at a lower render scale.

## 9. Testing

- **Automation tests** in C++ for pure logic: heat rules, mission state machine, pool lifecycle, save versioning, damage maths, lane graph queries.
- **Functional tests** run in a small test map for possession, enter/exit and pickups.
- **Headless runs** use `UnrealEditor-Cmd` with `-nullrhi` through `scripts/test.sh`.
- **Editor validators** in `OpenCityEditor` reject wrongly named assets, wrong texture settings and materials not using approved masters.
- **Performance reports** come from a fixed camera path in a test map, so numbers can be compared between milestones.

## 10. Plugin matrix

Maturity labels come from each plugin's `.uplugin` file in the installed 5.8.4 engine (checked 2026-11-07). None of these plugins restricts its platforms. The M0 smoke test checks each one actually loads on Linux.

| Plugin | Label in 5.8.4 | Needed at | At M0 |
|---|---|---|---|
| Enhanced Input | Stable | M1 | On |
| Chaos Vehicles | **Experimental** | M2 | On |
| Python Editor Script Plugin | Beta | M0 | On |
| Editor Scripting Utilities | Beta | M0 | On |
| PCG | Stable | M3 | On |
| Water | **Experimental** | M3 | Smoke test, then off |
| Gameplay Abilities | Stable | M6 | On |
| State Tree, Gameplay State Tree | Stable | M5 | On |
| Mass Gameplay, Mass AI, Mass Crowd | **Experimental** | M5 spike | Smoke test, then off |
| ZoneGraph | **Experimental** | M5 spike | Smoke test, then off |
| CommonUI | Stable | M9 | Smoke test, then off |
| MetaSounds, Niagara | Stable | M2+ | On |

Chaos Vehicles has carried the Experimental label for several releases and is the engine's only built-in vehicle system. We use it, but treat odd vehicle behaviour as a possible engine bug and keep vehicle code behind `AOCVehicle` so it can be swapped.

The Fab plugin and Quixel Bridge are not needed.

## 11. Sources

- [Epic: UE 5.8 release notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes?lang=en-US) (partially read; it is very long)
- [Epic: Linux development requirements](https://dev.epicgames.com/documentation/unreal-engine/linux-development-requirements-for-unreal-engine)
- [Epic: SDL3 on Linux](https://dev.epicgames.com/documentation/unreal-engine/updating-unreal-engine-on-linux-to-sdl3)
- [Tom Looman: UE 5.8 performance highlights](https://tomlooman.com/unreal-engine-5-8-performance-highlights/)
