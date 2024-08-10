// Copyright (c) <2024> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#include "HoudiniMassCommands.h"

#include "ZoneGraphSettings.h"
#include "ZoneShapeComponent.h"

#include "HoudiniMassCommon.h"


#define LOCTEXT_NAMESPACE "HoudiniMassTranslator"

FHoudiniMassCommands::FHoudiniMassCommands()
	: TCommands<FHoudiniMassCommands>(TEXT("HoudiniMassTranslator"), NSLOCTEXT("Contexts", "HoudiniMassTranslator", "Houdini Mass Translator Plugin"), NAME_None, FAppStyle::GetAppStyleSetName())
{
}

void FHoudiniMassCommands::RegisterCommands()
{
	UI_COMMAND(CleanupLaneProfiles, "Clean Up Houdini Lane Profiles", "Clean Up Useless Houdini Engine Generated Lane Profiles (Starts With \"LP_HE_\")", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(RemoveAllLaneProfiles, "Remove All Houdini Lane Profiles", "Remove All Houdini Engine Generated Lane Profiles (Starts With \"LP_HE_\")", EUserInterfaceActionType::Button, FInputChord());
}

void FHoudiniMassCommands::OnCleanupLaneProfiles()
{
	UZoneGraphSettings* ZoneGraphSettings = GetMutableDefault<UZoneGraphSettings>();
	TSet<FGuid> UsedLaneProfileIDs;
	for (FThreadSafeObjectIterator Iter(UZoneShapeComponent::StaticClass()); Iter; ++Iter)
	{
		UZoneShapeComponent* ZSC = Cast<UZoneShapeComponent>(*Iter);
		if (!IsValid(ZSC))
			continue;

		if (ZSC->GetShapeType() == FZoneShapeType::Spline)
		{
			FZoneLaneProfile LaneProfile;
			ZSC->GetSplineLaneProfile(LaneProfile);
			UsedLaneProfileIDs.Add(LaneProfile.ID);
		}
		else
		{
			for (const FZoneShapePoint& Point : ZSC->GetMutablePoints())
			{
				if (Point.LaneProfile == FZoneShapePoint::InheritLaneProfile)
				{
					FZoneLaneProfile LaneProfile;
					ZSC->GetSplineLaneProfile(LaneProfile);
					UsedLaneProfileIDs.Add(LaneProfile.ID);
				}
				else if (ZSC->GetPerPointLaneProfiles().IsValidIndex(Point.LaneProfile))
					UsedLaneProfileIDs.Add(ZSC->GetPerPointLaneProfiles()[Point.LaneProfile].ID);
			}
		}
	}

	((TArray<FZoneLaneProfile>*)&ZoneGraphSettings->GetLaneProfiles())->RemoveAll([UsedLaneProfileIDs](const FZoneLaneProfile& LaneProfile)
		{
			FString Name = LaneProfile.Name.ToString();
			if (Name.StartsWith(HOUDINI_LANE_PROFILE_PREFIX) && Name.Len() >= 7)
				return !UsedLaneProfileIDs.Contains(LaneProfile.ID);

			return false;
		});
	ZoneGraphSettings->TryUpdateDefaultConfigFile();
}

void FHoudiniMassCommands::OnRemoveAllLaneProfiles()
{
	UZoneGraphSettings* ZoneGraphSettings = GetMutableDefault<UZoneGraphSettings>();
	((TArray<FZoneLaneProfile>*) & ZoneGraphSettings->GetLaneProfiles())->RemoveAll([](const FZoneLaneProfile& LaneProfile)
		{
			return LaneProfile.Name.ToString().StartsWith(HOUDINI_LANE_PROFILE_PREFIX);
		});
	ZoneGraphSettings->TryUpdateDefaultConfigFile();
}

#undef LOCTEXT_NAMESPACE
