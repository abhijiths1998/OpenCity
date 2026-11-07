#pragma once

#include "CoreMinimal.h"

/** Timings for one frame, in milliseconds. */
struct FOCFrameSample
{
	float FrameMs = 0.f;
	float GameMs = 0.f;
	float RenderMs = 0.f;
	float GpuMs = 0.f;
};

/** Summary of a run of frames. All times in milliseconds. */
struct OPENCITYCORE_API FOCFrameStatsSummary
{
	int32 NumFrames = 0;
	double AvgFrameMs = 0.0;
	double P50FrameMs = 0.0;
	double P99FrameMs = 0.0;
	double MaxFrameMs = 0.0;
	/** Frames divided by total time, so long frames weigh correctly. */
	double AvgFps = 0.0;
	/** FPS computed from the average of the slowest 1% of frames. */
	double OnePercentLowFps = 0.0;
	double AvgGameMs = 0.0;
	double AvgRenderMs = 0.0;
	double AvgGpuMs = 0.0;

	FString ToString() const;
};

/** Collects frame samples and summarises them. Pure logic, no engine state. */
class OPENCITYCORE_API FOCFrameStats
{
public:
	void Reset();
	void Add(const FOCFrameSample& Sample);
	int32 Num() const { return Samples.Num(); }
	FOCFrameStatsSummary Summarize() const;

	/** Nearest-rank percentile of an ascending array. P in [0, 1]. Empty input returns 0. */
	static double Percentile(const TArray<float>& SortedAscending, double P);

private:
	TArray<FOCFrameSample> Samples;
};
