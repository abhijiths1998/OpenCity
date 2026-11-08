#include "Progression/OCAreaUnlockData.h"

const FOCAreaUnlock* UOCAreaUnlockData::Find(const FGameplayTag& Area) const
{
	return Areas.FindByPredicate([&Area](const FOCAreaUnlock& A) { return A.Area == Area; });
}
