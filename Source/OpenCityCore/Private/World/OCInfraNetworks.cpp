#include "World/OCInfraNetworks.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/OCRoadGraph.h"

namespace OCInfra
{
	TSharedPtr<FJsonObject> ParseSection(const FString& JsonText, const TCHAR* Section, FString& OutError, bool& bOutValid)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		bOutValid = FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid();
		if (!bOutValid)
		{
			OutError = TEXT("not valid JSON");
			return nullptr;
		}
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		return Root->TryGetObjectField(Section, Obj) ? *Obj : nullptr;
	}

	bool ReadPoints(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, TArray<FVector>& Out, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!O->TryGetArrayField(Field, Arr))
		{
			return true;
		}
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TArray<TSharedPtr<FJsonValue>>& P = V->AsArray();
			if (P.Num() < 2)
			{
				OutError = FString::Printf(TEXT("%s point needs [x, y] or [x, y, z]"), Field);
				return false;
			}
			Out.Add(FOCRoadGraph::LayoutToWorld(P[0]->AsNumber(), P[1]->AsNumber(), P.Num() > 2 ? P[2]->AsNumber() : 0.0));
		}
		return true;
	}

	FName NameField(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		FString S;
		return O->TryGetStringField(Field, S) && !S.IsEmpty() ? FName(*S) : NAME_None;
	}

	float NumberField(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, float Default)
	{
		double V;
		return O->TryGetNumberField(Field, V) ? static_cast<float>(V) : Default;
	}
}

float FOCRailLine::LengthCm() const
{
	float L = 0.f;
	for (int32 i = 1; i < Points.Num(); ++i)
	{
		L += static_cast<float>(FVector::Dist(Points[i - 1], Points[i]));
	}
	return L;
}

float FOCRailLine::GradeLimitPct() const
{
	if (MaxGradePct > 0.f)
	{
		return MaxGradePct;
	}
	if (Kind == FName(TEXT("tram")))
	{
		return 8.f;
	}
	if (Kind == FName(TEXT("funicular")))
	{
		return 100.f;
	}
	return 2.5f;
}

bool FOCRailNetwork::LoadFromLayoutJson(const FString& JsonText, FString& OutError)
{
	Lines.Reset();
	bool bValid = false;
	const TSharedPtr<FJsonObject> Rail = OCInfra::ParseSection(JsonText, TEXT("rail"), OutError, bValid);
	if (!bValid)
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Rail.IsValid() || !Rail->TryGetArrayField(TEXT("lines"), Arr))
	{
		return true;  // no rail yet
	}
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid())
		{
			continue;
		}
		FOCRailLine L;
		L.Id = OCInfra::NameField(O, TEXT("id"));
		L.Kind = OCInfra::NameField(O, TEXT("kind"));
		L.GaugeMm = static_cast<int32>(OCInfra::NumberField(O, TEXT("gauge_mm"), 1435.f));
		L.Electrification = OCInfra::NameField(O, TEXT("electrification"));
		L.MaxGradePct = OCInfra::NumberField(O, TEXT("max_grade_pct"), 0.f);
		if (!OCInfra::ReadPoints(O, TEXT("points"), L.Points, OutError))
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* StationArr = nullptr;
		if (O->TryGetArrayField(TEXT("stations"), StationArr))
		{
			for (const TSharedPtr<FJsonValue>& SV : *StationArr)
			{
				const TSharedPtr<FJsonObject> S = SV->AsObject();
				if (S.IsValid())
				{
					L.Stations.Add({ OCInfra::NameField(S, TEXT("name")), static_cast<int32>(OCInfra::NumberField(S, TEXT("point"), -1.f)),
						OCInfra::NumberField(S, TEXT("platform_m"), 0.f) * 100.f });
				}
			}
		}
		Lines.Add(L);
	}
	return true;
}

const FOCWaterBody* FOCWaterNetwork::FindBody(FName Id) const
{
	return Bodies.FindByPredicate([Id](const FOCWaterBody& B) { return B.Id == Id; });
}

bool FOCWaterNetwork::LoadFromLayoutJson(const FString& JsonText, FString& OutError)
{
	Bodies.Reset();
	bool bValid = false;
	const TSharedPtr<FJsonObject> Water = OCInfra::ParseSection(JsonText, TEXT("water_network"), OutError, bValid);
	if (!bValid)
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Water.IsValid() || !Water->TryGetArrayField(TEXT("bodies"), Arr))
	{
		return true;
	}
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid())
		{
			continue;
		}
		FOCWaterBody B;
		B.Id = OCInfra::NameField(O, TEXT("id"));
		B.Type = OCInfra::NameField(O, TEXT("type"));
		B.DepthM = OCInfra::NumberField(O, TEXT("depth_m"), 0.f);
		B.FlowMps = OCInfra::NumberField(O, TEXT("flow_mps"), 0.f);
		B.LevelM = OCInfra::NumberField(O, TEXT("level_m"), 0.f);
		O->TryGetBoolField(TEXT("seasonal"), B.bSeasonal);
		O->TryGetBoolField(TEXT("rises_with_weather"), B.bRisesWithWeather);
		if (!OCInfra::ReadPoints(O, TEXT("centerline"), B.Centerline, OutError))
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* EdgeArr = nullptr;
		if (O->TryGetArrayField(TEXT("edges"), EdgeArr))
		{
			for (const TSharedPtr<FJsonValue>& EV : *EdgeArr)
			{
				const TSharedPtr<FJsonObject> E = EV->AsObject();
				if (E.IsValid())
				{
					B.Edges.Add({ OCInfra::NameField(E, TEXT("treatment")), OCInfra::NameField(E, TEXT("side")),
						OCInfra::NumberField(E, TEXT("from_m"), 0.f), OCInfra::NumberField(E, TEXT("to_m"), 0.f) });
				}
			}
		}
		Bodies.Add(B);
	}
	return true;
}
