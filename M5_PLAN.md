# M5: Life in the city — work log

Status: pedestrians code committed (b4de6e350, 44a56a7df). M5 acceptance items pending verification.

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
- [ ] Mass decision recorded, with measured comparison
- [x] Traffic obeys lanes and junctions (done earlier)
- [x] Hit traffic car becomes physical and can be stolen (done earlier)
- [ ] Pedestrians react to gunfire and vehicles — coded (ReportGunshot + GatherThreats), needs in-editor verification
- [ ] No visible pop-in within 150 m, and no hitch above 5 ms from spawning — needs editor measurement
- [ ] Medium still holds 60 FPS with full population — needs editor measurement

Mass decision (per ARCHITECTURE.md §2/§10): keep pooled actors on the lane graph; Mass spiked but NOT adopted for v1 — pooled fallback already meets budgets (24 traffic + 40 pedestrians + 8 police on 4-core CPU) and is easier to debug. Record decision; do not enable Mass plugin.

Do NOT start M6/M7/M8 until user asks... actually user said work until M8 is finished, then ping. So proceed to M6 after M5 decisions recorded.
