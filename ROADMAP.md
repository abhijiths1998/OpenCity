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

**Build:** World Partition level, Landscape with coast and hills, road graph asset, PCG roads and building blockout, HLOD, ocean. The district layout follows `STORY.md`: the districts, the water between them (Fiume Ardo, the estuary, the Darsena), the bridges, and a grey-box blockout of every story landmark. Area gates: `UOCProgressionSubsystem`, unlock data, Data Layer gates (raised bridge, cordon, checkpoint, swing bridge), locked-area volumes, and deep-water reset. Missions don't exist until M8, so gates are opened by `oc.dev.unlock` for now.

**Acceptance:**

- [ ] 2x2 km district drivable from end to end in grey-box
- [ ] Road graph drives both road geometry and lane data
- [ ] Every story landmark in `STORY.md` exists as a named grey-box in the right district
- [ ] River, estuary and Darsena exist, with drivable bridges; falling into deep water resets you to the bank
- [ ] A new game starts with only Città Alta and Le Fornaci open; each gate opens with `oc.dev.unlock <milestone>`, and the unlock schedule is data, not code
- [ ] Entering a locked area warns, then pushes back; gates cost no Tick
- [ ] Driving at top speed never reaches unloaded ground or visible pop-in of collision
- [ ] HLODs build from a script and appear at distance
- [ ] Water spike decided: Water plugin or custom ocean, with the reason written down
- [ ] Medium holds 60 FPS on the test path in grey-box, with VRAM measured

**Checkpoint:** you can now drive a whole grey-box city. Review scope here, before spending on visuals.

## M4a: Look-dev pipeline (L)

**Build:** CC0 fetch and license ledger, ORM packing, texture import script, `MPC_World`, day/night cycle, `M_Master_Opaque`, `M_Master_Building`, `M_Master_Road`, and the first color-grade LUT.

**Acceptance:**

- [ ] One command imports a texture folder with correct settings and creates a material instance
- [ ] Editor validator flags a deliberately wrong texture
- [ ] Master materials are generated by Python and can be regenerated from clean
- [ ] Changing one `MPC_World` value visibly updates every material
- [ ] Day/night loop runs with correct exposure and working street lights
- [ ] Lumen Lite tested on Linux Vulkan, and Medium configuration decided
- [ ] Texture pool settled against measured numbers

## M4b: Look-dev completion (L)

**Build:** remaining masters (car paint, glass, foliage, character, water), decals, Clear, Cloudy and Rain weather, final post-process, and the showcase street block.

**Acceptance:**

- [ ] Showcase block looks final-quality at golden hour, midday, night and in rain
- [ ] All four presets produce measurably different visuals and costs
- [ ] Medium on the showcase block: 60 FPS on the test path, at most 5,000 MiB VRAM
- [ ] Shader Complexity and Quad Overdraw reviewed, with hot spots reported
- [ ] Every master is within its instruction budget
- [ ] Rain drives wet surfaces, puddles, splashes and windshield droplets from one value

## M5: Life in the city (L)

**Build:** Mass spike (1 week, time-boxed), kinematic traffic on lanes, pooled pedestrians, flee and panic reactions, spawn and despawn around the player.

**Acceptance:**

- [ ] Mass decision recorded, with measured comparison
- [ ] Traffic obeys lanes and junctions and avoids obvious collisions
- [ ] A hit traffic car becomes physical and can be stolen
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

**Build:** data-driven framework for objectives, triggers, checkpoints, fail states and cutscene hooks, plus the 3 story missions from `STORY.md`: "Funerale" (drive), "Strada Costiera" (chase) and "La Fornace" (shootout).

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
