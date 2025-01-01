// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#include "HoudiniInputZoneShape.h"

#include "ZoneGraphSettings.h"
#include "ZoneShapeComponent.h"

#include "HoudiniApi.h"
#include "HoudiniEngine.h"
#include "HoudiniEngineUtils.h"

#include "HoudiniMassCommon.h"


bool FHoudiniZoneShapeComponentInput::HapiDestroy(UHoudiniInput* Input) const  // Will then delete this, so we need NOT to reset node ids to -1
{
	if (NodeId >= 0)
	{
		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::DeleteNode(FHoudiniEngine::Get().GetSession(), NodeId));
		Input->NotifyMergedNodeDestroyed();
	}

	return true;
}

bool FHoudiniZoneShapeComponentInputBuilder::IsValidInput(const UActorComponent* Component)
{
	return IsValid(Component) && Component->IsA<UZoneShapeComponent>();
}

uint32 GetTypeHash(const FZoneLaneDesc& Lane)
{
	return GetTypeHash(TArray<uint8>((uint8*)&Lane, sizeof(FZoneLaneDesc)));
}

static const char* ConvertLaneToJsonStr(const FZoneLaneDesc& Lane, const UZoneGraphSettings* ZoneGraphSettings, TMap<FZoneLaneDesc, std::string>& InOutLaneDictStrMap)
{
	if (const std::string* FoundStrPtr = InOutLaneDictStrMap.Find(Lane))
		return FoundStrPtr->c_str();
	else {
		FString TagsStr = TEXT("[");
		for (const FZoneGraphTagInfo& Tag : ZoneGraphSettings->GetTagInfos())
		{
			if (Lane.Tags.Contains(Tag.Tag))
				TagsStr += TEXT("\"") + Tag.Name.ToString() + TEXT("\",");
		}
		TagsStr.RemoveFromEnd(TEXT(","));
		TagsStr += TEXT("]");

		return InOutLaneDictStrMap.Add(Lane, TCHAR_TO_UTF8(*FString::Printf(TEXT("{\"Width\":%f,\"Direction\":%d,\"Tags\":%s}"),
			Lane.Width * POSITION_SCALE_TO_HOUDINI, int32(Lane.Direction), *TagsStr))).c_str();
	}

	return "";
}

bool FHoudiniZoneShapeComponentInputBuilder::HapiUpload(UHoudiniInput* Input, const bool& bIsSingleComponent,  // Is there only one single valid component in the whole blueprint/actor
	const TArray<const UActorComponent*>& Components, const TArray<FTransform>& Transforms, const TArray<int32>& ComponentIndices,  // Components and Transforms are all of the components in blueprint/actor, and ComponentIndices are ref the valid indices from IsValidInput
	int32& InOutInstancerNodeId, TArray<TSharedPtr<FHoudiniComponentInput>>& InOutComponentInputs, TArray<FHoudiniComponentInputPoint>& InOutPoints)
{
	TSharedPtr<FHoudiniZoneShapeComponentInput> ZSCInput;
	if (InOutComponentInputs.IsValidIndex(0))
		ZSCInput = StaticCastSharedPtr<FHoudiniZoneShapeComponentInput>(InOutComponentInputs[0]);
	else
	{
		ZSCInput = MakeShared<FHoudiniZoneShapeComponentInput>();
		InOutComponentInputs.Add(ZSCInput);
	}

	int32& NodeId = ZSCInput->NodeId;
	const bool bCreateNewNode = (NodeId < 0);
	if (bCreateNewNode)
		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::CreateNode(FHoudiniEngine::Get().GetSession(), Input->GetGeoNodeId(), "null",
			TCHAR_TO_UTF8(*FString::Printf(TEXT("%s_zone_shape_%08X"), *(Components[ComponentIndices[0]]->GetOuter()->GetName()), FPlatformTime::Cycles())),
			false, &NodeId));

	HAPI_PartInfo PartInfo;
	FHoudiniApi::PartInfo_Init(&PartInfo);
	PartInfo.type = HAPI_PARTTYPE_CURVE;
	PartInfo.faceCount = ComponentIndices.Num();

	const UZoneGraphSettings* ZoneGraphSettings = GetDefault<UZoneGraphSettings>();

	TArray<int32> VertexCounts;
	TArray<int32> ZoneShapeTypes;
	TArray<float> Positions;
	TArray<float> Rotations;

	// s@unreal_zone_lane_profile_name
	TMap<FName, std::string> LaneProfileNameStrMap;
	TArray<const char*> PointLaneProfileNames;
	TArray<const char*> SplineLaneProfileNames;
	SplineLaneProfileNames.Reserve(PartInfo.faceCount);

	// d[]@unreal_zone_lane_profile
	TMap<FZoneLaneDesc, std::string> LaneDictStrMap;
	TArray<const char*> PointLanes;
	TArray<int32> PointLaneCounts;
	TArray<const char*> SplineLanes;
	TArray<int32> SplineLaneCounts;
	SplineLaneCounts.Reserve(PartInfo.faceCount);

	bool bHasPolygon = false;
	bool bHasSpline = false;
	for (const int32& CompIdx : ComponentIndices)
	{
		const UZoneShapeComponent* ZSC = Cast<UZoneShapeComponent>(Components[CompIdx]);
		const FTransform& Transform = Transforms[CompIdx];
		ZoneShapeTypes.Add((int32)ZSC->GetShapeType());
		VertexCounts.Add(ZSC->GetPoints().Num());

		const int32 NumPoints = ZSC->GetPoints().Num();
		if (ZSC->GetShapeType() == FZoneShapeType::Spline)
		{
			bHasSpline = true;

			// Spline LaneProfile
			FZoneLaneProfile LaneProfile;
			ZSC->GetSplineLaneProfile(LaneProfile);
			if (const std::string* FoundStrPtr = LaneProfileNameStrMap.Find(LaneProfile.Name))
				SplineLaneProfileNames.Add(FoundStrPtr->c_str());
			else
				SplineLaneProfileNames.Add(
					LaneProfileNameStrMap.Add(LaneProfile.Name, LaneProfile.Name.IsNone() ? "" : TCHAR_TO_UTF8(*LaneProfile.Name.ToString())).c_str());

			SplineLaneCounts.Add(LaneProfile.Lanes.Num());
			for (const FZoneLaneDesc& Lane : LaneProfile.Lanes)
				SplineLanes.Add(ConvertLaneToJsonStr(Lane, ZoneGraphSettings, LaneDictStrMap));

			// Point LaneProfiles
			for (int32 PointIdx = 0; PointIdx < NumPoints; ++PointIdx)
			{
				PointLaneProfileNames.Add("");
				PointLaneCounts.Add(0);
			}
		}
		else
		{
			bHasPolygon = true;

			// Point LaneProfiles
			FZoneLaneProfile SplineLaneProfile;
			SplineLaneProfile.ID = FGuid();
			TArray<FZoneLaneProfile> LaneProfiles;
			ZSC->GetPolygonLaneProfiles(LaneProfiles);
			for (const FZoneShapePoint& Point : ZSC->GetPoints())
			{
				const FZoneLaneProfile* LaneProfilePtr = nullptr;
				if (Point.LaneProfile == FZoneShapePoint::InheritLaneProfile)
				{
					if (SplineLaneProfile.ID == FGuid())
						ZSC->GetSplineLaneProfile(SplineLaneProfile);
					LaneProfilePtr = &SplineLaneProfile;
				}
				else if (LaneProfiles.IsValidIndex(Point.LaneProfile))
					LaneProfilePtr = &LaneProfiles[Point.LaneProfile];

				PointLaneProfileNames.Add((LaneProfilePtr && !LaneProfilePtr->Name.IsNone()) ? TCHAR_TO_UTF8(*LaneProfilePtr->Name.ToString()) : "");

				PointLaneCounts.Add(LaneProfilePtr ? LaneProfilePtr->Lanes.Num() : 0);
				if (LaneProfilePtr)
				{
					for (const FZoneLaneDesc& Lane : LaneProfilePtr->Lanes)
						PointLanes.Add(ConvertLaneToJsonStr(Lane, ZoneGraphSettings, LaneDictStrMap));
				}
			}

			// Spline LaneProfile
			SplineLaneProfileNames.Add("");
			SplineLaneCounts.Add(0);
		}


		PartInfo.pointCount += NumPoints;
		for (const FZoneShapePoint& Point : ZSC->GetPoints())
		{
			const FVector3f Pos = FVector3f(Transform.TransformPosition(Point.Position) * POSITION_SCALE_TO_HOUDINI);
			Positions.Add(Pos.X);
			Positions.Add(Pos.Z);
			Positions.Add(Pos.Y);

			const FQuat4f Rot = (FQuat4f)Transform.TransformRotation(Point.Rotation.Quaternion());
			Rotations.Add(Rot.X);
			Rotations.Add(Rot.Z);
			Rotations.Add(Rot.Y);
			Rotations.Add(-Rot.W);
		}
	}
	PartInfo.vertexCount = PartInfo.pointCount;

	HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetPartInfo(FHoudiniEngine::Get().GetSession(), NodeId, 0, &PartInfo));

	HAPI_AttributeInfo AttributeInfo;
	FHoudiniApi::AttributeInfo_Init(&AttributeInfo);
	AttributeInfo.exists = true;
	AttributeInfo.originalOwner = HAPI_ATTROWNER_INVALID;

	{
		// @P
		AttributeInfo.count = PartInfo.pointCount;
		AttributeInfo.tupleSize = 3;
		AttributeInfo.owner = HAPI_ATTROWNER_POINT;
		AttributeInfo.storage = HAPI_STORAGETYPE_FLOAT;

		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
			HAPI_ATTRIB_POSITION, &AttributeInfo));

		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
			HAPI_ATTRIB_POSITION, &AttributeInfo, Positions.GetData(), 0, AttributeInfo.count));
	}

	{
		HAPI_CurveInfo CurveInfo;
		FHoudiniApi::CurveInfo_Init(&CurveInfo);
		CurveInfo.curveType = HAPI_CURVETYPE_LINEAR;
		CurveInfo.curveCount = PartInfo.faceCount;
		CurveInfo.vertexCount = PartInfo.vertexCount;
		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetCurveInfo(FHoudiniEngine::Get().GetSession(), NodeId, 0, &CurveInfo));

		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetCurveCounts(
			FHoudiniEngine::Get().GetSession(), NodeId, 0, VertexCounts.GetData(), 0, PartInfo.faceCount));
	}

	{
		// p@rot
		AttributeInfo.count = PartInfo.pointCount;
		AttributeInfo.tupleSize = 4;
		AttributeInfo.owner = HAPI_ATTROWNER_POINT;
		AttributeInfo.storage = HAPI_STORAGETYPE_FLOAT;

		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
			HAPI_ATTRIB_ROT, &AttributeInfo));

		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
			HAPI_ATTRIB_ROT, &AttributeInfo, Rotations.GetData(), 0, AttributeInfo.count));
	}

	{
		// i@unreal_zone_shape_type
		AttributeInfo.count = PartInfo.faceCount;
		AttributeInfo.tupleSize = 1;
		AttributeInfo.owner = HAPI_ATTROWNER_PRIM;
		AttributeInfo.storage = HAPI_STORAGETYPE_INT;

		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
			HAPI_ATTRIB_UNREAL_ZONE_SHAPE_TYPE, &AttributeInfo));

		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeIntData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
			HAPI_ATTRIB_UNREAL_ZONE_SHAPE_TYPE, &AttributeInfo, ZoneShapeTypes.GetData(), 0, AttributeInfo.count));
	}

	static const char* SpareStr = "";

	auto HapiSetLaneProfileLambda = [&](const int32& ElemCount, const HAPI_AttributeOwner& Owner,
		TArray<const char*>& LaneProfileNames, TArray<const char*>& Lanes, const TArray<int32>& LaneCounts) -> bool
		{
			AttributeInfo.count = ElemCount;
			AttributeInfo.tupleSize = 1;
			AttributeInfo.owner = Owner;

			// s@unreal_zone_lane_profile_name
			AttributeInfo.storage = HAPI_STORAGETYPE_STRING;

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE_NAME, &AttributeInfo));

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeStringData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE_NAME, &AttributeInfo, LaneProfileNames.GetData(), 0, AttributeInfo.count));

			// s@unreal_zone_lane_profile
			AttributeInfo.storage = HAPI_STORAGETYPE_DICTIONARY_ARRAY;

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::AddAttribute(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE, &AttributeInfo));

			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::SetAttributeDictionaryArrayData(FHoudiniEngine::Get().GetSession(), NodeId, 0,
				HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE, &AttributeInfo,
				(Lanes.IsEmpty() ? &SpareStr : Lanes.GetData()), Lanes.Num(), LaneCounts.GetData(), 0, AttributeInfo.count));

			return true;
		};

	if (bHasPolygon)
		HOUDINI_FAIL_RETURN(HapiSetLaneProfileLambda(PartInfo.pointCount, HAPI_ATTROWNER_POINT, PointLaneProfileNames, PointLanes, PointLaneCounts));

	if (bHasSpline)
		HOUDINI_FAIL_RETURN(HapiSetLaneProfileLambda(PartInfo.faceCount, HAPI_ATTROWNER_PRIM, SplineLaneProfileNames, SplineLanes, SplineLaneCounts));

	HAPI_SESSION_FAIL_RETURN(FHoudiniApi::CommitGeo(FHoudiniEngine::Get().GetSession(), NodeId));
	
	if (bCreateNewNode)
		HOUDINI_FAIL_RETURN(Input->HapiConnectToMergeNode(NodeId));

	return true;
}

void FHoudiniZoneShapeComponentInputBuilder::AppendInfo(const TArray<const UActorComponent*>& Components, const TArray<FTransform>& Transforms, const TArray<int32>& ComponentIndices,  // See the comment upon
	const TSharedPtr<FJsonObject>& JsonObject)  // Append object info to JsonObject, keys are instance refs, values are JsonObjects that contain transoforms and meta data
{
	// TODO:
}
