#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Perf/OCFrameStats.h"
#include "OCPerfCameraPath.generated.h"

class UCameraComponent;
class USplineComponent;

/**
 * Flies a camera along a closed spline at constant speed and records frame timings,
 * so performance numbers are comparable between milestones.
 *
 * Runs when bAutoStart is set or the game is started with -ocperf. Options:
 *   -ocperfloops=N   laps to measure (0 = run forever, for stability runs)
 *   -ocperfquit      quit when measuring is finished
 *   -ocperfprofilegpu  log a one-frame GPU pass breakdown (ProfileGPU) when measuring starts
 *
 * Each lap is logged, and a JSON report is written to Saved/Profiling/OCPerf/.
 */
UCLASS()
class OPENCITY_API AOCPerfCameraPath : public AActor
{
	GENERATED_BODY()

public:
	AOCPerfCameraPath();

	virtual void OnConstruction(const FTransform& Transform) override;
	// Tick is needed here: the camera moves and samples timings every frame, and only while active.
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "Perf")
	TObjectPtr<USplineComponent> Path;

	UPROPERTY(VisibleAnywhere, Category = "Perf")
	TObjectPtr<UCameraComponent> Camera;

	/** Camera speed along the path. */
	UPROPERTY(EditAnywhere, Category = "Perf", meta = (ClampMin = "10", Units = "cm/s"))
	float SpeedCmPerSec = 1500.f;

	/** Seconds to fly before measuring, so shaders and streaming settle. */
	UPROPERTY(EditAnywhere, Category = "Perf", meta = (ClampMin = "0", Units = "s"))
	float WarmupSeconds = 5.f;

	/** Downward camera tilt in degrees. */
	UPROPERTY(EditAnywhere, Category = "Perf")
	float CameraPitch = -10.f;

	/** Start without the -ocperf switch (useful in PIE). */
	UPROPERTY(EditAnywhere, Category = "Perf")
	bool bAutoStart = false;

	/** Laps to measure when -ocperfloops is not given. 0 = forever. */
	UPROPERTY(EditAnywhere, Category = "Perf", meta = (ClampMin = "0"))
	int32 DefaultLaps = 1;

	/** Build the path as a circle from the values below. Turn off to edit the spline by hand. */
	UPROPERTY(EditAnywhere, Category = "Perf|Generated Path")
	bool bGenerateCircle = true;

	UPROPERTY(EditAnywhere, Category = "Perf|Generated Path", meta = (EditCondition = "bGenerateCircle", ClampMin = "100", Units = "cm"))
	float CircleRadius = 5000.f;

	UPROPERTY(EditAnywhere, Category = "Perf|Generated Path", meta = (EditCondition = "bGenerateCircle", Units = "cm"))
	float CircleHeight = 300.f;

	UPROPERTY(EditAnywhere, Category = "Perf|Generated Path", meta = (EditCondition = "bGenerateCircle", ClampMin = "4", ClampMax = "64"))
	int32 CirclePoints = 16;

private:
	void BuildCircle();
	void UpdateCamera();
	bool EnsureViewTarget();
	void FinishLap();
	void Finish();
	void WriteReport() const;

	TArray<FOCFrameStatsSummary> LapSummaries;
	FOCFrameStats CurrentLap;
	FString StartTimestamp;
	float Distance = 0.f;
	float WarmupLeft = 0.f;
	int32 LapsWanted = 1;
	bool bActive = false;
	bool bMeasuring = false;
	bool bQuitWhenDone = false;
	bool bProfileGpu = false;
};
