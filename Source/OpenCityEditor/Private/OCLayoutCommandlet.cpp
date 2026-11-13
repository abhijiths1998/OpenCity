#include "OCLayoutCommandlet.h"

#include "ImageCore.h"
#include "ImageUtils.h"
#include "Layout/OCMareviaLayout.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OCLog.h"

UOCLayoutCommandlet::UOCLayoutCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

namespace
{
	bool SelfTest()
	{
		using namespace OCLayout;
		bool bOk = true;
		auto Check = [&bOk](bool bPass, const FString& What)
		{
			UE_LOG(LogOpenCity, Display, TEXT("selftest %s: %s"), bPass ? TEXT("ok  ") : TEXT("FAIL"), *What);
			bOk &= bPass;
		};
		// Reference values: numpy.random.default_rng(seed).random() and random.Random(seed).random().
		Check(FNpRandom(0).Random() == 0.6369616873214543, FString::Printf(TEXT("default_rng(0).random() = %s"), *PyRepr(FNpRandom(0).Random())));
		Check(FNpRandom(42).Random() == 0.7739560485559633, FString::Printf(TEXT("default_rng(42).random() = %s"), *PyRepr(FNpRandom(42).Random())));
		Check(FPyRandom(0).Random() == 0.8444218515250481, FString::Printf(TEXT("Random(0).random() = %s"), *PyRepr(FPyRandom(0).Random())));
		Check(FPyRandom(42).Random() == 0.6394267984578837, FString::Printf(TEXT("Random(42).random() = %s"), *PyRepr(FPyRandom(42).Random())));
		Check(PyRepr(0.1) == TEXT("0.1") && PyRepr(1e-05) == TEXT("1e-05") && PyRepr(100.0) == TEXT("100.0") && PyRepr(1e16) == TEXT("1e+16")
			&& PyRepr(-2.5) == TEXT("-2.5") && PyRepr(0.0001) == TEXT("0.0001"), TEXT("float repr"));
		Check(PyRound(2.675, 2) == 2.67 && PyRound(0.125, 2) == 0.12 && PyRound(0.375, 2) == 0.38 && PyRoundInt(2.5) == 2 && PyRoundInt(3.5) == 4,
			TEXT("round (correctly rounded, ties to even)"));
		return bOk;
	}

	int32 CompareOutputs(const FString& Mine, const FString& Ref)
	{
		using namespace OCLayout;
		FString A, B, Err;
		if (!FFileHelper::LoadFileToString(A, *(Mine / TEXT("marevia_layout.json"))) || !FFileHelper::LoadFileToString(B, *(Ref / TEXT("marevia_layout.json"))))
		{
			UE_LOG(LogOpenCity, Error, TEXT("compare: cannot read the layouts"));
			return -1;
		}
		const FJsonPtr JA = FJson::Parse(A, Err), JB = FJson::Parse(B, Err);
		TArray<FString> Report;
		const int32 N = FJson::Compare(JA, JB, 0.0, 60, Report, TEXT("layout"));
		UE_LOG(LogOpenCity, Display, TEXT("compare: %d difference(s) in marevia_layout.json; text identical: %s"), N, A == B ? TEXT("yes") : TEXT("no"));
		for (const FString& R : Report)
		{
			UE_LOG(LogOpenCity, Display, TEXT("  %s"), *R);
		}
		FImage IA, IB;
		if (FImageUtils::LoadImage(*(Mine / TEXT("marevia_height.png")), IA) && FImageUtils::LoadImage(*(Ref / TEXT("marevia_height.png")), IB))
		{
			IA.ChangeFormat(ERawImageFormat::G16, EGammaSpace::Linear);
			IB.ChangeFormat(ERawImageFormat::G16, EGammaSpace::Linear);
			if (IA.SizeX != IB.SizeX || IA.SizeY != IB.SizeY)
			{
				UE_LOG(LogOpenCity, Display, TEXT("compare: heightmap sizes differ"));
			}
			else
			{
				int64 Diff = 0, Big = 0;
				int32 MaxD = 0;
				const TArrayView64<uint16> PA = IA.AsG16(), PB = IB.AsG16();
				for (int64 i = 0; i < PA.Num(); ++i)
				{
					const int32 D = FMath::Abs(int32(PA[i]) - int32(PB[i]));
					Diff += D ? 1 : 0;
					Big += D > 1 ? 1 : 0;
					MaxD = FMath::Max(MaxD, D);
				}
				UE_LOG(LogOpenCity, Display, TEXT("compare: heightmap %lld of %lld pixels differ (%lld by more than 1 step of 1.2 cm), largest %d steps"),
					(long long)Diff, (long long)PA.Num(), (long long)Big, MaxD);
			}
		}
		return N;
	}
}

int32 UOCLayoutCommandlet::Main(const FString& Params)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString OutDir = Root / TEXT("SourceArt/World");
	FString CompareDir;
	FParse::Value(*Params, TEXT("out="), OutDir);
	FParse::Value(*Params, TEXT("compare="), CompareDir);
	int32 Seed = OCLayout::DefaultSeed;
	FParse::Value(*Params, TEXT("seed="), Seed);
	if (FPaths::IsRelative(OutDir)) { OutDir = Root / OutDir; }
	if (FParse::Param(*Params, TEXT("selftest")) && !SelfTest())
	{
		UE_LOG(LogOpenCity, Error, TEXT("OCLayout: self-test failed"));
		return 1;
	}
	const double T0 = FPlatformTime::Seconds();
	FString Error;
	if (!OCLayout::GenerateMarevia(OutDir, Root / TEXT("SourceArt/Infra/road_profiles.json"), Error, Seed))
	{
		UE_LOG(LogOpenCity, Error, TEXT("OCLayout: %s"), *Error);
		return 1;
	}
	UE_LOG(LogOpenCity, Display, TEXT("OCLayout: wrote %s in %.1f s"), *OutDir, FPlatformTime::Seconds() - T0);
	if (!CompareDir.IsEmpty())
	{
		if (FPaths::IsRelative(CompareDir)) { CompareDir = Root / CompareDir; }
		CompareOutputs(OutDir, CompareDir);
	}
	return 0;
}
