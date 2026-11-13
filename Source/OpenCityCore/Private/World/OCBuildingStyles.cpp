#include "World/OCBuildingStyles.h"

const FOCBuildingArchetype* UOCBuildingStyleSet::Find(FName Name) const
{
	return Archetypes.FindByPredicate([Name](const FOCBuildingArchetype& A) { return A.Name == Name; });
}

namespace
{
	TArray<FOCPieceWeight> W(std::initializer_list<TPair<const TCHAR*, float>> List)
	{
		TArray<FOCPieceWeight> Out;
		for (const TPair<const TCHAR*, float>& P : List)
		{
			Out.Add({ FName(P.Key), P.Value });
		}
		return Out;
	}
}

void UOCBuildingStyleSet::FillDefaults()
{
	WallPalettes = {
		{ { { 1.00f, 0.90f, 0.74f }, { 0.96f, 0.76f, 0.48f }, { 0.90f, 0.58f, 0.44f }, { 1.00f, 0.86f, 0.62f }, { 0.98f, 0.95f, 0.88f }, { 0.94f, 0.72f, 0.60f },
			{ 0.92f, 0.66f, 0.42f }, { 0.86f, 0.52f, 0.38f } } },
		{ { { 1.00f, 0.82f, 0.76f }, { 0.86f, 0.92f, 0.90f }, { 1.00f, 0.95f, 0.80f }, { 0.84f, 0.88f, 0.96f }, { 1.00f, 1.00f, 0.97f }, { 0.98f, 0.86f, 0.66f } } },
		{ { { 0.80f, 0.78f, 0.74f }, { 0.86f, 0.80f, 0.70f }, { 0.72f, 0.70f, 0.68f }, { 0.88f, 0.70f, 0.56f }, { 0.78f, 0.76f, 0.70f }, { 0.82f, 0.74f, 0.64f } } },
		{ { { 1.00f, 0.95f, 0.86f }, { 0.95f, 0.88f, 0.74f }, { 0.98f, 0.92f, 0.82f }, { 0.92f, 0.80f, 0.64f }, { 1.00f, 0.98f, 0.93f }, { 0.90f, 0.84f, 0.74f } } } };
	WoodColours = { { { 0.30f, 0.52f, 0.36f }, { 0.30f, 0.46f, 0.68f }, { 0.26f, 0.52f, 0.52f }, { 0.52f, 0.36f, 0.24f },
		{ 0.55f, 0.60f, 0.64f }, { 0.92f, 0.92f, 0.88f }, { 0.62f, 0.24f, 0.20f } } };

	auto Plaster = [](const TCHAR* Name) { FOCBuildingArchetype A; A.Name = Name; A.Crowns = W({ { TEXT("Cornice"), 1.f } }); return A; };
	Archetypes.Reset();
	{
		FOCBuildingArchetype A = Plaster(TEXT("townhouse"));
		A.PitchedRoof = 0.7f; A.Gable = 0.6f; A.Shutters = 0.85f; A.Shop = 0.45f; A.Balcony = 0.3f; A.Symmetric = 0.3f;
		A.Crowns = W({ { TEXT("Cornice"), 2.f }, { TEXT("CorniceBracket"), 1.f } });
		A.Upper = W({ { TEXT("Window"), 3.f }, { TEXT("WindowArch"), 0.6f } });
		A.Top = W({ { TEXT("Window"), 1.f }, { TEXT("WindowSmall"), 0.6f }, { TEXT("WindowPlain"), 0.5f } });
		A.Balconies = W({ { TEXT("Balcony"), 2.f }, { TEXT("Juliet"), 1.5f } });
		A.Shops = W({ { TEXT("Shopfront"), 3.f }, { TEXT("ShopAwning"), 1.f }, { TEXT("ShopClosed"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 3.f }, { TEXT("Garage"), 0.4f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("palazzo"));
		A.PitchedRoof = 0.6f; A.Gable = 0.2f; A.Shutters = 0.9f; A.Shop = 0.3f; A.Balcony = 0.45f; A.Symmetric = 0.95f;
		A.bStringCourse = true; A.Door = TEXT("Portone"); A.bWideDoor = true; A.Trim = 0;
		A.Crowns = W({ { TEXT("CorniceBracket"), 1.f }, { TEXT("Cornice"), 1.f } });
		A.Upper = W({ { TEXT("Window"), 2.f }, { TEXT("WindowArch"), 1.f } });
		A.UpperFirst = W({ { TEXT("WindowTall"), 2.f }, { TEXT("WindowArch"), 1.f } });
		A.Top = W({ { TEXT("WindowSmall"), 1.f }, { TEXT("Window"), 1.f } });
		A.Balconies = W({ { TEXT("Balcony"), 1.f } });
		A.Shops = W({ { TEXT("Shopfront"), 2.f }, { TEXT("ShopAwning"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 1.f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("tower"));
		A.PitchedRoof = 0.9f; A.Gable = 0.1f; A.Shutters = 0.8f; A.Shop = 0.2f; A.Balcony = 0.05f; A.SideWindows = 0.4f; A.Symmetric = 0.5f;
		A.Upper = W({ { TEXT("Window"), 1.f }, { TEXT("WindowSmall"), 1.f } });
		A.Top = W({ { TEXT("WindowArch"), 1.f }, { TEXT("WindowSmall"), 1.f } });
		A.Shops = W({ { TEXT("Shopfront"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 1.f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("courtyard"));
		A.PitchedRoof = 0.5f; A.Gable = 0.5f; A.Shutters = 0.85f; A.Shop = 0.4f; A.Balcony = 0.35f; A.Symmetric = 0.8f;
		A.Door = TEXT("Portone"); A.bWideDoor = true;
		A.Upper = W({ { TEXT("Window"), 3.f }, { TEXT("WindowPlain"), 1.f } });
		A.Balconies = W({ { TEXT("Balcony"), 1.f }, { TEXT("BalconyLong"), 0.5f } });
		A.Shops = W({ { TEXT("Shopfront"), 2.f }, { TEXT("ShopClosed"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 2.f }, { TEXT("Garage"), 0.5f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("tenement"));
		A.PitchedRoof = 0.35f; A.Gable = 0.5f; A.Shutters = 0.7f; A.Shop = 0.55f; A.Balcony = 0.35f; A.Symmetric = 0.7f; A.Trim = 1;
		A.Crowns = W({ { TEXT("Cornice"), 2.f }, { TEXT("None"), 0.5f } });
		A.Upper = W({ { TEXT("Window"), 3.f }, { TEXT("WindowPlain"), 1.f } });
		A.Balconies = W({ { TEXT("Balcony"), 2.f }, { TEXT("BalconyLong"), 1.f } });
		A.Shops = W({ { TEXT("Shopfront"), 3.f }, { TEXT("ShopAwning"), 1.5f }, { TEXT("ShopClosed"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 2.f }, { TEXT("Garage"), 1.f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("office"));
		A.Palette = 3; A.Trim = 2; A.PitchedRoof = 0.f; A.Shutters = 0.f; A.bQuoins = false; A.Shop = 0.4f; A.Balcony = 0.1f; A.Symmetric = 0.8f;
		A.bStringCourse = true; A.Crowns = W({ { TEXT("Cornice"), 1.f }, { TEXT("None"), 1.f } });
		A.Upper = W({ { TEXT("WindowPlain"), 2.f }, { TEXT("Hotel_Window"), 1.f } });
		A.Balconies = W({ { TEXT("Juliet"), 1.f } });
		A.Shops = W({ { TEXT("Shopfront"), 2.f }, { TEXT("ShopAwning"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 1.f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("corner"));
		A.Trim = 1; A.PitchedRoof = 0.f; A.Shutters = 0.75f; A.bQuoins = false; A.Shop = 0.8f; A.Balcony = 0.4f; A.Symmetric = 0.f;
		A.Upper = W({ { TEXT("Window"), 2.f }, { TEXT("WindowPlain"), 1.f } });
		A.Balconies = W({ { TEXT("Balcony"), 1.f }, { TEXT("BalconyLong"), 1.f } });
		A.Shops = W({ { TEXT("Shopfront"), 2.f }, { TEXT("ShopAwning"), 2.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 1.f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A;
		A.Name = TEXT("shed");
		A.Material = TEXT("Shed"); A.Palette = 2; A.Trim = 1; A.PitchedRoof = 1.f; A.Gable = 1.f; A.Pitch = 0.18f; A.Shutters = 0.f;
		A.bQuoins = false; A.Shop = 0.4f; A.Balcony = 0.f; A.SideWindows = 0.5f; A.Symmetric = 0.3f;
		A.Crowns = W({ { TEXT("None"), 1.f } });
		A.Upper = W({ { TEXT("Shed_WindowStrip"), 1.f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A;
		A.Name = TEXT("warehouse");
		A.Material = TEXT("Brick"); A.Palette = 2; A.Trim = 3; A.PitchedRoof = 0.6f; A.Gable = 1.f; A.Pitch = 0.3f; A.Shutters = 0.f;
		A.bQuoins = false; A.Shop = 0.3f; A.Balcony = 0.f; A.SideWindows = 0.6f; A.Symmetric = 0.7f;
		A.Crowns = W({ { TEXT("Brick_Corbel"), 1.f } });
		A.Upper = W({ { TEXT("Brick_WindowArch"), 1.f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("annex"));
		A.Palette = 2; A.Trim = 1; A.PitchedRoof = 0.f; A.Shutters = 0.2f; A.bQuoins = false; A.Shop = 0.2f; A.Balcony = 0.f; A.Symmetric = 0.5f;
		A.Crowns = W({ { TEXT("None"), 1.f } });
		A.Upper = W({ { TEXT("WindowPlain"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 1.f }, { TEXT("Garage"), 1.f } });
		Archetypes.Add(A);
	}
	for (const TCHAR* Hotel : { TEXT("slabhotel"), TEXT("steppedhotel") })
	{
		FOCBuildingArchetype A = Plaster(Hotel);
		A.Palette = 1; A.Trim = 2; A.PitchedRoof = 0.f; A.Shutters = 0.2f; A.bQuoins = false; A.Shop = 0.6f; A.Balcony = 0.9f; A.Symmetric = 0.8f;
		A.Door = TEXT("Hotel_Entrance"); A.bWideDoor = true;
		A.Crowns = W({ { TEXT("Cornice"), 1.f }, { TEXT("None"), 1.f } });
		A.Upper = W({ { TEXT("Hotel_Window"), 2.f }, { TEXT("WindowPlain"), 1.f } });
		A.Balconies = W({ { TEXT("Hotel_Balcony"), 3.f }, { TEXT("BalconyLong"), 1.f } });
		A.Shops = W({ { TEXT("ShopAwning"), 2.f }, { TEXT("Shopfront"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 1.f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("villa"));
		A.Palette = 1; A.PitchedRoof = 0.85f; A.Gable = 0.15f; A.Shutters = 0.9f; A.Shop = 0.f; A.Balcony = 0.5f; A.Symmetric = 0.9f;
		A.Crowns = W({ { TEXT("CorniceBracket"), 2.f }, { TEXT("Cornice"), 1.f } });
		A.Upper = W({ { TEXT("Window"), 2.f }, { TEXT("WindowArch"), 1.f } });
		A.UpperFirst = W({ { TEXT("WindowTall"), 1.f }, { TEXT("Window"), 1.f } });
		A.Balconies = W({ { TEXT("Balcony"), 1.f }, { TEXT("Juliet"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 1.f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("kiosk"));
		A.Palette = 1; A.Trim = 2; A.PitchedRoof = 0.f; A.bQuoins = false; A.Shop = 1.f; A.Balcony = 0.f; A.Symmetric = 0.f;
		A.Crowns = W({ { TEXT("None"), 1.f } });
		A.Upper = W({ { TEXT("WindowPlain"), 1.f } });
		A.Shops = W({ { TEXT("ShopAwning"), 3.f }, { TEXT("Shopfront"), 1.f } });
		Archetypes.Add(A);
	}
	for (const TCHAR* Village : { TEXT("farmhouse"), TEXT("cottage") })
	{
		FOCBuildingArchetype A = Plaster(Village);
		A.PitchedRoof = 0.9f; A.Gable = 0.7f; A.Shutters = 0.9f; A.Shop = 0.2f; A.Balcony = 0.2f; A.Symmetric = 0.3f;
		A.Upper = W({ { TEXT("Window"), 2.f }, { TEXT("WindowSmall"), 1.f } });
		A.Top = W({ { TEXT("WindowSmall"), 1.f }, { TEXT("Window"), 1.f } });
		A.Balconies = W({ { TEXT("Juliet"), 1.f }, { TEXT("Balcony"), 1.f } });
		A.Shops = W({ { TEXT("Shopfront"), 1.f }, { TEXT("ShopClosed"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 2.f }, { TEXT("Garage"), 0.5f }, { TEXT("Portone"), 0.3f } });
		Archetypes.Add(A);
	}
	for (const TCHAR* Hill : { TEXT("villatower"), TEXT("farm") })
	{
		FOCBuildingArchetype A = Plaster(Hill);
		A.Palette = 3; A.PitchedRoof = 0.95f; A.Gable = FCString::Strcmp(Hill, TEXT("farm")) == 0 ? 0.8f : 0.1f; A.Shutters = 0.9f;
		A.Shop = 0.f; A.Balcony = 0.35f; A.Symmetric = 0.7f;
		A.Crowns = W({ { TEXT("CorniceBracket"), 1.f }, { TEXT("Cornice"), 1.f } });
		A.Upper = W({ { TEXT("Window"), 2.f }, { TEXT("WindowArch"), 1.f }, { TEXT("WindowSmall"), 0.5f } });
		A.Balconies = W({ { TEXT("Balcony"), 1.f }, { TEXT("Juliet"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 1.f }, { TEXT("Portone"), 0.3f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("church"));
		A.Palette = 3; A.PitchedRoof = 1.f; A.Gable = 1.f; A.Shutters = 0.f; A.bQuoins = false; A.bPilasters = true; A.Shop = 0.f; A.Balcony = 0.f;
		A.Symmetric = 1.f; A.Door = TEXT("Church_Portal"); A.bWideDoor = true;
		A.Upper = W({ { TEXT("WindowArch"), 1.f } });
		A.GroundOther = W({ { TEXT("WindowBlind"), 1.f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("civic"));
		A.Palette = 3; A.PitchedRoof = 0.3f; A.Gable = 0.f; A.Shutters = 0.3f; A.bQuoins = false; A.bPilasters = true; A.bStringCourse = true;
		A.Shop = 0.f; A.Balcony = 0.f; A.Symmetric = 1.f; A.Door = TEXT("Civic_Entrance"); A.bWideDoor = true;
		A.Upper = W({ { TEXT("WindowArch"), 1.f }, { TEXT("Window"), 1.f } });
		A.UpperFirst = W({ { TEXT("WindowTall"), 1.f } });
		A.Top = W({ { TEXT("WindowSmall"), 1.f } });
		A.GroundOther = W({ { TEXT("GroundWindow"), 1.f } });
		Archetypes.Add(A);
	}
	{
		FOCBuildingArchetype A = Plaster(TEXT("station"));
		A.Palette = 3; A.Trim = 1; A.PitchedRoof = 0.f; A.Shutters = 0.f; A.bQuoins = false; A.bPilasters = true; A.bCanopy = true;
		A.Shop = 0.f; A.Balcony = 0.f; A.Symmetric = 1.f; A.Door = TEXT("Civic_Entrance"); A.bWideDoor = true;
		A.Upper = W({ { TEXT("WindowArch"), 1.f } });
		A.GroundOther = W({ { TEXT("WindowArch"), 1.f } });
		Archetypes.Add(A);
	}
}
