#pragma once

#include "CoreMinimal.h"

/**
 * Magazine + reserve bookkeeping for one equipped weapon (M6). Pure, so the rules are unit-tested
 * (OpenCity.Core.Combat.Ammo) and UOCWeaponComponent only adds timing and world queries on top.
 */
struct FOCAmmo
{
	int32 MagazineSize = 0;
	int32 PerShot = 1;
	int32 InMagazine = 0;
	int32 Reserve = 0;

	void Equip(int32 InMagazineSize, int32 InPerShot, int32 InReserve)
	{
		MagazineSize = FMath::Max(0, InMagazineSize);
		PerShot = FMath::Max(0, InPerShot);
		InMagazine = MagazineSize;
		Reserve = FMath::Max(0, InReserve);
	}

	bool CanFire() const { return InMagazine >= PerShot; }

	/** Spend one shot; false (and nothing spent) when the magazine is too low. */
	bool Consume()
	{
		if (!CanFire()) return false;
		InMagazine -= PerShot;
		return true;
	}

	/** A reload would add rounds: the magazine is not full and the reserve is not empty. */
	bool CanReload() const { return InMagazine < MagazineSize && Reserve > 0; }

	/** Move rounds from the reserve into the magazine; returns how many moved. */
	int32 Reload()
	{
		const int32 ToAdd = FMath::Min(MagazineSize - InMagazine, Reserve);
		if (ToAdd <= 0) return 0;
		InMagazine += ToAdd;
		Reserve -= ToAdd;
		return ToAdd;
	}

	void AddReserve(int32 Rounds, int32 MaxReserve)
	{
		Reserve = FMath::Clamp(Reserve + Rounds, 0, FMath::Max(0, MaxReserve));
	}
};
