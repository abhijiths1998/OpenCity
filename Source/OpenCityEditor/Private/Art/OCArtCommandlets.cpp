#include "OCArtCommandlets.h"

#include "Art/OCArt.h"
#include "Misc/Paths.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"

namespace
{
	void InitArtCommandlet(UCommandlet* C)
	{
		C->IsClient = false;
		C->IsEditor = true;
		C->IsServer = false;
		C->LogToConsole = true;
	}
}

UOCBuildArtCommandlet::UOCBuildArtCommandlet() { InitArtCommandlet(this); }

int32 UOCBuildArtCommandlet::Main(const FString& Params)
{
	FString StepList = TEXT("decals,materials,weather,physmat,props,kit");
	FParse::Value(*Params, TEXT("steps="), StepList, /*bShouldStopOnSeparator*/ false);
	TArray<FString> Steps;
	StepList.ParseIntoArray(Steps, TEXT(","));
	for (const FString& Step : Steps)
	{
		UE_LOG(LogOpenCity, Display, TEXT("OCBuildArt: step %s"), *Step);
		bool bOk = true;
		if (Step == TEXT("decals")) { bOk = OCArt::BuildDecals(); }   // before the masters: walls use a leak scan as grime
		else if (Step == TEXT("materials")) { bOk = OCArt::BuildMaterials(); }
		else if (Step == TEXT("weather")) { OCArt::BuildWeather(); }
		else if (Step == TEXT("physmat")) { OCArt::BuildPhysicalMaterials(); }
		else if (Step == TEXT("props")) { bOk = OCArt::ImportProps(); }
		else if (Step == TEXT("kit")) { bOk = OCArt::ImportKit(); }
		else if (Step == TEXT("stats")) { OCArt::MaterialStats(); }
		else
		{
			UE_LOG(LogOpenCity, Error, TEXT("OCBuildArt: unknown step %s"), *Step);
			bOk = false;
		}
		if (!bOk)
		{
			UE_LOG(LogOpenCity, Error, TEXT("OCBuildArt: step %s FAILED"), *Step);
			return 1;
		}
	}
	UE_LOG(LogOpenCity, Display, TEXT("OCBuildArt: done (%s)"), *StepList);
	return 0;
}

UOCImportTexturesCommandlet::UOCImportTexturesCommandlet() { InitArtCommandlet(this); }

int32 UOCImportTexturesCommandlet::Main(const FString& Params)
{
	FString Src, Dest, Parent;
	if (!FParse::Value(*Params, TEXT("src="), Src) || !FParse::Value(*Params, TEXT("dest="), Dest) || !FParse::Value(*Params, TEXT("parent="), Parent))
	{
		UE_LOG(LogOpenCity, Error, TEXT("usage: -run=OCImportTextures -src=<folder> -dest=/Game/... -parent=/Game/Materials/<Master>"));
		return 1;
	}
	if (FPaths::IsRelative(Src))
	{
		Src = OCEd::ProjectDir() / Src;
	}
	return OCArt::ImportTextureFolder(Src, Dest, Parent) ? 0 : 1;
}
