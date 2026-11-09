#pragma once

#include "CoreMinimal.h"

/**
 * Where the sun is at a time of day, for UOCWorldStateSubsystem. Pure logic, unit-tested.
 * The sun rises in the east at SunriseHour, peaks at MaxElevationDeg halfway to SunsetHour and
 * sets in the west; at night it is below the horizon, deepest at local midnight.
 */
struct OPENCITYCORE_API FOCSunModel
{
	float SunriseHour = 6.5f;
	float SunsetHour = 20.0f;
	float MaxElevationDeg = 68.f;

	/** Degrees above the horizon (negative at night). */
	float ElevationDeg(float Hour) const;

	/** Compass bearing in degrees: 90 = east at sunrise, 180 = south at noon, 270 = west at sunset. */
	float AzimuthDeg(float Hour) const;

	/** 0 in daylight, 1 at night, blended through twilight (sun between +6 and -6 degrees). */
	static float NightAmount(float ElevationDeg);

	/** Hour wrapped into [0, 24). */
	static float Wrap(float Hour);
};
