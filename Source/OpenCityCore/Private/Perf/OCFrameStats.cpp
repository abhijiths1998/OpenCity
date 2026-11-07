#include "Perf/OCFrameStats.h"

FString FOCFrameStatsSummary::ToString() const
{
	return FString::Printf(
		TEXT("frames=%d avg=%.2fms (%.1f fps) p50=%.2fms p99=%.2fms max=%.2fms 1%%low=%.1f fps | game=%.2fms render=%.2fms gpu=%.2fms"),
		NumFrames, AvgFrameMs, AvgFps, P50FrameMs, P99FrameMs, MaxFrameMs, OnePercentLowFps,
		AvgGameMs, AvgRenderMs, AvgGpuMs);
}

void FOCFrameStats::Reset()
{
	Samples.Reset();
}

void FOCFrameStats::Add(const FOCFrameSample& Sample)
{
	Samples.Add(Sample);
}

double FOCFrameStats::Percentile(const TArray<float>& SortedAscending, double P)
{
	const int32 Count = SortedAscending.Num();
	if (Count == 0)
	{
		return 0.0;
	}
	const double Clamped = FMath::Clamp(P, 0.0, 1.0);
	const int32 Rank = FMath::Max(1, FMath::CeilToInt32(Clamped * Count)); // 1-based nearest rank
	return SortedAscending[FMath::Min(Rank, Count) - 1];
}

FOCFrameStatsSummary FOCFrameStats::Summarize() const
{
	FOCFrameStatsSummary Out;
	Out.NumFrames = Samples.Num();
	if (Out.NumFrames == 0)
	{
		return Out;
	}

	TArray<float> FrameMs;
	FrameMs.Reserve(Out.NumFrames);
	double SumFrame = 0.0, SumGame = 0.0, SumRender = 0.0, SumGpu = 0.0;
	for (const FOCFrameSample& S : Samples)
	{
		FrameMs.Add(S.FrameMs);
		SumFrame += S.FrameMs;
		SumGame += S.GameMs;
		SumRender += S.RenderMs;
		SumGpu += S.GpuMs;
	}
	FrameMs.Sort();

	const double N = Out.NumFrames;
	Out.AvgFrameMs = SumFrame / N;
	Out.AvgGameMs = SumGame / N;
	Out.AvgRenderMs = SumRender / N;
	Out.AvgGpuMs = SumGpu / N;
	Out.P50FrameMs = Percentile(FrameMs, 0.50);
	Out.P99FrameMs = Percentile(FrameMs, 0.99);
	Out.MaxFrameMs = FrameMs.Last();
	Out.AvgFps = Out.AvgFrameMs > 0.0 ? 1000.0 / Out.AvgFrameMs : 0.0;

	// Average of the slowest 1% of frames (at least one frame).
	const int32 WorstCount = FMath::Max(1, FMath::CeilToInt32(N * 0.01));
	double SumWorst = 0.0;
	for (int32 i = Out.NumFrames - WorstCount; i < Out.NumFrames; ++i)
	{
		SumWorst += FrameMs[i];
	}
	const double AvgWorst = SumWorst / WorstCount;
	Out.OnePercentLowFps = AvgWorst > 0.0 ? 1000.0 / AvgWorst : 0.0;

	return Out;
}
