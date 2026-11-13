# OpenCity — M4a Step 5: Modular Kits (superseded)

The Blueprint/PCG + Blender/Python pipeline this step once described was dropped (no Python, Blender
unusable). The building kit is generated in C++ and assembled by a C++ commandlet:

- Pieces: `Source/OpenCityEditor/Private/Kit/OCKitPieces.cpp`, contract in `Tools/kit/KIT_SPEC.md`.
- Facade plans: `Source/OpenCityCore/Public/World/OCFacadePlan.h` (pure logic, tested).
- Assembly: `UOCWorldBuilderLibrary::BuildKitBuildings` (`OCKitBuilder.cpp`).
- Work order for making the buildings look good: `BUILDINGS_PLAN.md`.

What still stands from this step is the target look per district, which phase 3 of `BUILDINGS_PLAN.md`
(district piece sets) builds towards:

| District | Kit pieces | Wear |
|---|---|---|
| Città Alta | plaster walls, timber shutters, market awnings, flower boxes | peeling paint, worn steps |
| Porto Vecchio | corrugated tin, rusted shutters, cargo doors, bollards | salt staining, oil, chipped paint |
| Lungomare | hotel façades, balcony rails, sign backboards | sea salt, fresh paint upstairs |
| Le Fornaci | brick kiln walls, goods doors, water tower | soot, brick dust |
| Le Colline | stone villa walls, cypress planters, estate gates | moss, weathered stone |
