#include "World/OCSunModel.h"

float FOCSunModel::Wrap(float Hour)
{
	const float H = FMath::Fmod(Hour, 24.f);
	return H < 0.f ? H + 24.f : H;
}

float FOCSunModel::ElevationDeg(float Hour) const
{
	const float H = Wrap(Hour);
	const float Day = FMath::Max(0.1f, SunsetHour - SunriseHour);
	if (H >= SunriseHour && H <= SunsetHour)
	{
		return MaxElevationDeg * FMath::Sin(PI * (H - SunriseHour) / Day);
	}
	// Night: a half sine below the horizon, lowest halfway between sunset and the next sunrise.
	const float Night = 24.f - Day;
	const float Since = Wrap(H - SunsetHour);
	return -0.5f * MaxElevationDeg * FMath::Sin(PI * Since / Night);
}

float FOCSunModel::AzimuthDeg(float Hour) const
{
	const float H = Wrap(Hour);
	const float Day = FMath::Max(0.1f, SunsetHour - SunriseHour);
	if (H >= SunriseHour && H <= SunsetHour)
	{
		return 90.f + 180.f * (H - SunriseHour) / Day;
	}
	const float Night = 24.f - Day;
	return FMath::Fmod(270.f + 180.f * Wrap(H - SunsetHour) / Night, 360.f);  // on round through north
}

float FOCSunModel::NightAmount(float ElevationDeg)
{
	const float T = FMath::Clamp((ElevationDeg + 6.f) / 12.f, 0.f, 1.f);
	return 1.f - T * T * (3.f - 2.f * T);
}
