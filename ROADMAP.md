# OpenCity: Roadmap

Status: **draft, awaiting approval**.

Work happens one milestone at a time. A milestone is done only when every acceptance item passes, code builds with no warnings, and the end-of-milestone report is delivered.

**Size key** (rough effort for one person): S = days, M = 1 to 3 weeks, L = 1 to 2 months, XL = over 2 months.

**Overall estimate:** this is well over a year of solo work, and possibly several, depending on your weekly hours. M0 to M3 gives you a drivable grey-box city, the first real checkpoint. See the cut list at the end.

## End-of-milestone report

1. What was built
2. How to test it in the editor
3. Performance from the fixed test path: FPS, `stat unit`, `stat gpu`, VRAM used and budget, plus an Insights trace for visual milestones
4. Known issues
5. Next step

## Before M0: your setup

These need your login or `sudo`, so they're yours to do:

1. Download UE 5.8.x from `https://www.unrealengine.com/linux` (needs your Epic account).
2. Extract it from a terminal into `~/UnrealEngine`, then delete the zip. A forum user found file-manager extraction ran out of space with 120 GB free.
3. Run `sudo pacman -S git-lfs blender krita`.
4. Choose an IDE: Rider or VS Code. Neither is installed. I'll generate `compile_commands.json` so `clangd` works in either.
5. Choose a git remote for off-machine backup. Free LFS quotas are small, so check the limits before pushing.

## M0: Project setup (S to M)

**Build:** C++ project with the three modules, plugin set, folder layout, git with LFS, `.gitignore`, `.gitattributes`, `scripts/` (`build.sh`, `run_editor.sh`, `test.sh`, `run_python.sh`, `package.sh`, `gen_clangd.sh`), `DefaultScalability.ini` skeleton, DDC location outside the repo, a test map with a fixed camera path.

**Acceptance:**

- [x] `scripts/build.sh` compiles with zero warnings
- [x] `scripts/run_editor.sh` opens the editor under XWayland, and menus and clicks work reliably
- [x] `scripts/test.sh` runs one sample automation test headless and reports a pass (3 tests)
- [x] Every plugin in the matrix passes its smoke test, and maturity labels are recorded in `ARCHITECTURE.md`
- [x] `r.Substrate` state confirmed, and SM6 on Vulkan confirmed in the log
- [x] Python script runs headless through the commandlet
- [x] `scripts/package.sh` produces a running **Linux Shipping** build of the empty project. Packaging problems must surface now, not at M11
- [x] `git lfs ls-files` shows a `.uasset` tracked by LFS
- [x] Baseline VRAM and frame time recorded for an empty lit level
- [x] **Stability gate:** 60 minutes of the Lumen and Nanite stress map running in a game window (`scripts/stability/stress_gate.sh`) with no hard freeze, with temperature logging on and the freeze-capture config installed. Normal editor use during M0 counts as the editor part

**If the stability gate fails:** pause feature work and treat the freeze as the priority. Try Hybrid GPU mode, the 60 FPS cap and `-sm5` to narrow it down.

**If 5.8 editor clicks are unusable under XWayland on KDE:** try the disabled-tooltip workaround, then 5.7, then a source build with community patches, in that order.

## M1: Player character (M)

**Build:** third-person movement, sprint, jump, crouch, over-the-shoulder camera, aim mode, Enhanced Input for keyboard, mouse and gamepad, animation Blueprint hookup using the UE Mannequin.

**Acceptance:**

- [ ] All actions work on keyboard and mouse and on a gamepad *(mappings verified after reload and possession logged; hands-on check by you pending)*
- [ ] Aim mode changes camera offset and FOV smoothly, and the camera doesn't clip through walls *(blend is unit-tested and boom collision is on; visual check by you pending)*
- [x] Input Mapping Contexts swap by state (on foot, aiming)
- [x] Automation test covers the movement state rules
- [x] `AOCCharacter` has no gameplay logic beyond wiring, and no component ticks without a reason

## M2: Vehicles (L)

**Build:** 2 cars and 1 motorbike, enter and exit, vehicle camera, basic damage, radio stub. Cars start from the UE vehicle template meshes. The motorbike is a grey-box Blender model.

**Acceptance:**

- [x] Each vehicle's tuning comes entirely from `UOCVehicleData`
- [x] Enter and exit work from both sides, and the player can't exit into geometry
- [x] Motorbike stays upright at speed, leans in turns and can crash and be recovered *(scripted runs; feel tuning pending)*
- [x] Damage reduces performance and can destroy the vehicle *(wreck state: no power; visual damage comes with the car paint material in M4b)*
- [x] Radio cycles stations and keeps its position when you leave and re-enter
- [x] Input contexts swap on enter and exit

**Main risk:** motorbike feel. Time-box it to 2 weeks. If it isn't fun by then, ship the simple arcade version and move on.

## M3: Open world (L)

**Build:** World Partition level (3 × 3 km playable), Landscape with coast, hills and the Monte Ferrale mountain band (plus distant peaks and Isola Ventura as non-playable scenery), road graph asset, PCG roads and building blockout, HLOD, ocean. The district layout follows `STORY.md`: the districts, the water between them (Fiume Ardo, the Diga del Ferrale reservoir, the estuary, the Darsena), the bridges, and a grey-box blockout of every story landmark. Area gates: `UOCProgressionSubsystem`, unlock data, Data Layer gates (raised bridge, cordon, checkpoint, swing bridge), locked-area volumes, and deep-water reset. Missions don't exist until M8, so gates are opened by `oc.dev.unlock` for now.

**Acceptance:**

- [ ] 3 × 3 km map drivable from end to end in grey-box, including the mountain pass road and over the dam *(`oc.dev.route` tour through every district, up the pass and over the dam: 100% with no resets in a game window on 2026-11-13; 1 reset headless after later junction changes, see INFRA_DESIGN.md section 10. Parked 2026-11-13 for M4 to M11)*
- [x] Road graph drives both road geometry and lane data (`FOCLaneGraph`, unit-tested)
- [x] Every story landmark in `STORY.md` exists as a named grey-box in the right district
- [x] River, estuary and Darsena exist, with drivable bridges; falling into deep water resets you to the bank *(seen in drive tests: 'sank' + reset)*
- [x] A new game starts with only Città Alta and Le Fornaci open; each gate opens with `oc.dev.unlock <milestone>`, and the unlock schedule is data, not code
- [x] Entering a locked area warns, then pushes back; gates cost no Tick
- [x] Driving at top speed never reaches unloaded ground or visible pop-in of collision *(NOGROUND 0 at up to 231 km/h)*
- [x] HLODs build from a script and appear at distance (`scripts/build_hlod.sh`, ~2 min)
- [x] Water spike decided: Water plugin or custom ocean, with the reason written down (custom; ARCHITECTURE.md section 7)
- [x] Medium holds 60 FPS on the test path, with VRAM measured *(with final-art buildings: 83 FPS avg, 1% low 40.9, GPU 11.5 ms, game VRAM 3,804 MiB; 2026-11-09, editor -game, browser open)*

**Checkpoint:** you can now drive a whole grey-box city. Review scope here, before spending on visuals.

## M4a: Look-dev pipeline (L)

**Build:** CC0 fetch and license ledger, ORM packing, texture import script, `MPC_World`, day/night cycle, `M_Master_Opaque`, `M_Master_Building`, `M_Master_Road`, and the first color-grade LUT.

**Acceptance:**

- [x] One command imports a texture folder with correct settings and creates a material instance (`OCArtMaterials.cpp`, `scripts/build_art.sh`)
- [x] Editor validator flags a deliberately wrong texture (`the OCAudit commandlet` flags compression/sRGB/size by suffix)
- [x] Master materials are generated in code and can be regenerated from clean *(C++ `OCBuildArt -steps=materials` since 2026-11-11; Python removed)*
- [x] Changing one `MPC_World` value visibly updates every material *(2026-11-13: every master reads it. `Wetness` darkens and glosses surfaces, roads, landscape, foliage, car paint, street paint, decals and characters; `RainAmount` drives puddles, car-glass drops, water and screen rain; `NightAmount` lights windows, lamps and sign faces; `Wind` moves foliage. Dry/rain QA pair: Saved/QA/2026-11-13_1159_Clear and _1200_Rain)*
- [x] Day/night loop runs with correct exposure and working street lights (`UOCWorldStateSubsystem`, pooled sodium lights)
- [x] Lumen Lite tested on Linux Vulkan, and Medium configuration decided *(2026-11-13: in 5.8 it is GI quality 1, the irradiance-field final gather. L_Marevia perf lap A/B/A: 75.6 and 67.3 fps, GPU 12.5 and 14.0 ms, against 55.8 fps, GPU 17.3 ms with the screen-probe gather. Medium keeps it; DefaultScalability.ini)*
- [x] Texture pool settled against measured numbers *(2026-11-13: the streamer needs at most 212 MiB on Medium and 192 MiB on High; pools set to about 4x for content to come: 600 / 800 / 1,000 / 1,200 MiB, from 1,200 to 2,800. Game VRAM on Medium ~4.0 GB)*

## M4b: Look-dev completion (L)

**Build:** remaining masters (car paint, glass, foliage, character, water), decals, Clear, Cloudy and Rain weather, final post-process, and the showcase street block. *(Done: glass (building, car), foliage (wind from `MPC_World.Wind` per weather), car paint (palette, dirt and damage per car), water, decals, weather, post-process with local exposure, character master (skin, cloth, hair; 2026-11-13).)*

**Acceptance:**

- [x] Showcase block looks final-quality at golden hour, midday, night and in rain *(approved 2026-11-09 from QA sets 21:39-21:43, Città Alta and Lungomare at 13:00, 18:54, 22:30 and in rain)*
- [x] All four presets produce measurably different visuals and costs *(Low basic shadows, no clouds, GPU ~7 ms; Medium Lumen GI, GPU ~11 ms; High adds clouds and sharper shadows, GPU 13 to 14 ms. Ultra is not run on this 6 GB laptop (decision 2026-11-09: it runs out of VRAM with the desktop's other apps open); it stays for stronger GPUs, unverified). **Open (2026-11-13):** High measured 13.6 fps, GPU 73 ms on the L_Marevia lap (53 fps on 11-09) while Medium held 67 to 76 fps; cause not found yet*
- [x] Medium on the showcase block: 60 FPS on the test path, at most 5,000 MiB VRAM *(83.3 FPS avg, 1% low 40.9, GPU 11.3 ms, game VRAM 3,418 MiB; 2026-11-09, editor -game, browser open)*
- [x] Shader Complexity and Quad Overdraw reviewed, with hot spots reported *(2026-11-09, all 24 bookmarks; hot spots in ART_PIPELINE.md: one master over the whole city, roads plus decals, landscape quad overdraw 3 to 5x at distance)*
- [x] Every master is within its instruction budget *(re-measured 2026-11-13 against the targets approved on 11-09: surface 419/450, road 503/550, landscape 582/600, car paint 415/450, decal 388/400, character 238/450; street paint 224 to 303 against a proposed 350 (needs approval); ART_PIPELINE.md)*
- [x] Rain drives wet surfaces, puddles, splashes and windshield droplets from one value *(`RainAmount` and `Wetness` drive wet surfaces, puddles with ripples, splash rings on up-facing ground, screen rain and drops on the coupe's glass; QA shots 2026-11-09)*

## M5: Life in the city (L)

**Build:** Mass spike (1 week, time-boxed), kinematic traffic on lanes, pooled pedestrians, flee and panic reactions, spawn and despawn around the player.

**Acceptance:**

- [ ] Mass decision recorded, with measured comparison
- [x] Traffic obeys lanes and junctions and avoids obvious collisions *(2026-11-13: `FOCTrafficSim` on the lane graph, IDM following, junction links checked for crossing paths, no entry unless the exit lane has room, closed gates never entered; 3 unit tests (0 conflicting crossings and no overlaps in 3 min of a crowded crossroads). In L_Marevia, 24 pooled kinematic cars around the player, 0 conflicting crossings; cars stop for the player's car and people. Still to look at hands-on: driving among them)*
- [x] A hit traffic car becomes physical and can be stolen *(2026-11-13: hit by a physics body (speed change over 8 km/h), or taken with the interact button, a traffic car swaps for a pooled drivable twin of the same model, paint and speed (3 per model, parked at load). Tested headless with `oc.dev.traffic knock`, `take` and `ram` (a real collision at 32 km/h))*
- [ ] Pedestrians react to gunfire and vehicles
- [ ] No visible pop-in within 150 m, and no hitch above 5 ms from spawning
- [ ] Medium still holds 60 FPS with full population

## M6: Combat (L)

**Build:** GAS attributes and abilities, pistol, SMG, melee, hit reactions, ragdoll, impact decals and Niagara effects, ammo and pickups, weapon wheel.

**Acceptance:**

- [ ] Each weapon is defined entirely by a data asset
- [ ] All damage goes through `IOCDamageable`
- [ ] Projectiles, decals and effects are pooled
- [ ] Automation tests cover damage maths and ammo rules
- [ ] A 3-minute firefight causes no frame-time regression beyond 2 ms

## M7: Wanted system (L)

**Build:** crime events, witnesses, heat levels 1 to 5, police on foot and in vehicles, search zones, escape logic, original heat HUD element.

**Acceptance:**

- [ ] Rule table for crimes and heat lives in data, with automation tests
- [ ] Unwitnessed crimes don't raise heat
- [ ] Police pursuit escalates at each level, on foot and in vehicles
- [ ] Player can lose the police by leaving the search zone and staying hidden
- [ ] Population budgets hold during a level-5 chase

## M8: Missions (L)

**Build:** data-driven framework for objectives, triggers, checkpoints, fail states and cutscene hooks, plus the 3 ★ story missions from `STORY.md`: "Funerale" (drive), "Strada Costiera" (chase) and "La Fornace" (shootout). The other 17 of the 20-mission campaign are post-v1 content built on the same framework.

**Acceptance:**

- [ ] New missions are authored without C++ changes
- [ ] Completing "Funerale", "Strada Costiera" and "La Fornace" opens Porto Vecchio, Le Colline and Lungomare as in `STORY.md`
- [ ] Each sample mission can be completed, failed and restarted from a checkpoint
- [ ] Automation tests cover the state machine

## M9: UI and UX (L)

**Build:** HUD, minimap and full map from the road graph, waypoints, phone menu, pause and graphics settings, mission text, all with an original visual design.

**Acceptance:**

- [ ] Full gamepad and keyboard navigation
- [ ] Graphics presets apply without restart where the engine allows it
- [ ] Waypoint routing follows the road graph
- [ ] UI costs under 0.5 ms of game thread

## M10: Persistence and polish (L)

**Build:** save and load, money and shops, MetaSounds audio, ambient city sound, vehicle paint and customization.

**Acceptance:**

- [ ] Save, quit and load restores player, world time, missions, story progress (open areas), money and owned vehicles
- [ ] Old save versions upgrade or fail cleanly, with tests
- [ ] Paint changes use Custom Primitive Data, with no new material per car

## M11: Optimization and packaging (M to L)

**Build:** Insights profiling, hot-spot fixes, final scalability tuning, Linux Shipping package.

**Acceptance:**

- [ ] A 30-minute play session on Medium: 60 FPS average, 1% lows above 45, at most 5,000 MiB VRAM
- [ ] Shipping package runs from a clean directory on this machine
- [ ] Low preset runs without Lumen or VSM

## Cut list

If time runs short, cut in this order, without breaking the core loop:

1. Windows build
2. Motorbike
3. Vehicle customization
4. Shops and economy
5. Phone menu (use the pause menu)
6. Third sample mission
7. Police helicopters and other extras (not in the plan; keep them out)
