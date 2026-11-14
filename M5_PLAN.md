# M5: Life in the city — work log

Status: pedestrians done; committed.

Done this session (all C++, no Python/Blender, headless-testable):
- OpenCityCore: `IOCDamageable` interface (`Public/Damage/OCDamageable.h`) — shared damage entry point for pedestrians/vehicles/props (was missing; referenced by ARCHITECTURE.md §3).
- OpenCityCore: pure `FOCCrowdSim` crowd logic (`Public/Private/Population/OCCrowdSim.{h,cpp}`) — idle/wander/flee on 2D plane, neighbour separation, threats reported per Step, FleeUntil keeps fleeing after the last threat, damage→dead. Unit-tested headless.
- OpenCity: `AOCPedestrian` pooled actor (`Public/Private/Population/OCPedestrian.{h,cpp}`) — capsule + engine cylinder placeholder mesh, no per-frame tick, IOCDamageable.
- OpenCity: `UOCPopulationSubsystem` world subsystem (`Public/Private/Population/OCPopulationSubsystem.{h,cpp}`) — fixed pool at load, crowd sim at ~4 Hz AI rate, spawn/despawn ring 15–200 m (never in view within 150 m), despawn >250 m, ground trace per ped, lane-graph spawn points, flees traffic cars and gunshots (`ReportGunshot`).
- OpenCity: `UOCPopulationSettings` + DefaultGame.ini section (MediumBudget=40, AiRateHz=4, ThreatRadiusM=15, GunshotRadiusM=25, etc.).
- OpenCity: `oc.dev.population` dev command (stats; gunshot X Y).
- OpenCityCore: unit tests `OCPopulationTest.cpp` — flee, damage→dead, idle/wander. All 3 pass; full `OpenCity.Core` suite: 32 passed, 0 failed.

Build: `OpenCityEditor` target compiles clean, no warnings.

Pending M5 acceptance items (not started):
- Mass spike with measured comparison vs pooled-actor fallback.
- Pop-in / hitch check.
- Medium 60 FPS with full population (24 traffic + 40 pedestrians + 8 police).
- Hands-on driving among traffic (per memory note).
Do NOT start these until the user asks.
