#include "Misc/AutomationTest.h"
#include "Perf/OCFrameStats.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCFrameStatsEmptyTest, "OpenCity.Core.FrameStats.Empty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FOCFrameStatsEmptyTest::RunTest(const FString& Parameters)
{
	FOCFrameStats Stats;
	const FOCFrameStatsSummary S = Stats.Summarize();
	TestEqual(TEXT("No frames"), S.NumFrames, 0);
	TestEqual(TEXT("Avg is zero"), S.AvgFrameMs, 0.0);
	TestEqual(TEXT("FPS is zero, not inf"), S.AvgFps, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCFrameStatsSummaryTest, "OpenCity.Core.FrameStats.Summary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FOCFrameStatsSummaryTest::RunTest(const FString& Parameters)
{
	// Frames of 1..100 ms added in reverse order, so sorting is exercised.
	FOCFrameStats Stats;
	for (int32 Ms = 100; Ms >= 1; --Ms)
	{
		FOCFrameSample Sample;
		Sample.FrameMs = static_cast<float>(Ms);
		Sample.GameMs = 2.f;
		Sample.RenderMs = 3.f;
		Sample.GpuMs = 4.f;
		Stats.Add(Sample);
	}

	const FOCFrameStatsSummary S = Stats.Summarize();
	TestEqual(TEXT("Frame count"), S.NumFrames, 100);
	TestEqual(TEXT("Average"), S.AvgFrameMs, 50.5);
	TestEqual(TEXT("P50 (nearest rank)"), S.P50FrameMs, 50.0);
	TestEqual(TEXT("P99 (nearest rank)"), S.P99FrameMs, 99.0);
	TestEqual(TEXT("Max"), S.MaxFrameMs, 100.0);
	TestEqual(TEXT("Avg FPS"), S.AvgFps, 1000.0 / 50.5, 1e-9);
	TestEqual(TEXT("1% low uses the single slowest frame"), S.OnePercentLowFps, 10.0, 1e-9);
	TestEqual(TEXT("Avg game"), S.AvgGameMs, 2.0);
	TestEqual(TEXT("Avg render"), S.AvgRenderMs, 3.0);
	TestEqual(TEXT("Avg GPU"), S.AvgGpuMs, 4.0);

	Stats.Reset();
	TestEqual(TEXT("Reset clears samples"), Stats.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCFrameStatsPercentileTest, "OpenCity.Core.FrameStats.Percentile",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FOCFrameStatsPercentileTest::RunTest(const FString& Parameters)
{
	const TArray<float> One = { 7.f };
	TestEqual(TEXT("Single value, P0"), FOCFrameStats::Percentile(One, 0.0), 7.0);
	TestEqual(TEXT("Single value, P1"), FOCFrameStats::Percentile(One, 1.0), 7.0);

	const TArray<float> Four = { 1.f, 2.f, 3.f, 4.f };
	TestEqual(TEXT("P25 of four"), FOCFrameStats::Percentile(Four, 0.25), 1.0);
	TestEqual(TEXT("P26 of four rounds up"), FOCFrameStats::Percentile(Four, 0.26), 2.0);
	TestEqual(TEXT("P above 1 clamps"), FOCFrameStats::Percentile(Four, 5.0), 4.0);
	TestEqual(TEXT("Negative P clamps"), FOCFrameStats::Percentile(Four, -1.0), 1.0);

	const TArray<float> Empty;
	TestEqual(TEXT("Empty input"), FOCFrameStats::Percentile(Empty, 0.5), 0.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
