#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OCCameraBookmarks.generated.h"

/** One visual-QA viewpoint (ART_PIPELINE.md step 10). */
USTRUCT(BlueprintType)
struct FOCCameraBookmark
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bookmark")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bookmark")
	FName District;

	/** Street, Elevated or Driving. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bookmark")
	FName Kind;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bookmark")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bookmark")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bookmark")
	float FOV = 75.f;
};

/**
 * Camera bookmarks for the visual-QA loop: `oc.dev.qa` (scripts/qa_shots.sh) visits each one at
 * noon, sunset and night, takes a screenshot and logs frame time. Generated per district by
 * the OCQABookmarks commandlet (DA_QABookmarks).
 */
UCLASS(BlueprintType)
class OPENCITY_API UOCCameraBookmarks : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bookmarks")
	TArray<FOCCameraBookmark> Bookmarks;
};
