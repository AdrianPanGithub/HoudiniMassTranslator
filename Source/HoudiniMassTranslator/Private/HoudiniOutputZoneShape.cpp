// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#include "HoudiniOutputZoneShape.h"

#include "ZoneGraphSettings.h"
#include "ZoneShapeComponent.h"

#include "HoudiniApi.h"
#include "HoudiniEngine.h"
#include "HoudiniAttribute.h"
#include "HoudiniEngineUtils.h"
#include "HoudiniOutputUtils.h"

#include "HoudiniMassTranslator.h"
#include "HoudiniMassCommon.h"


bool FHoudiniZoneShapeOutputBuilder::HapiIsPartValid(const int32& NodeId, const HAPI_PartInfo& PartInfo, bool& bOutIsValid, bool& bOutShouldHoldByOutput)
{
	bOutShouldHoldByOutput = true;
	bOutIsValid = false;

	if (PartInfo.type == HAPI_PARTTYPE_CURVE)  // ZoneShape is curve-liked, so we should retrieve them from houdini curves
	{
		const int32& PartId = PartInfo.id;

		HAPI_AttributeInfo AttribInfo;
		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeInfo(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
			HAPI_ATTRIB_UNREAL_OUTPUT_ZONE_SHAPE, HAPI_ATTROWNER_DETAIL, &AttribInfo));

		if (AttribInfo.exists && !FHoudiniEngineUtils::IsArray(AttribInfo.storage) &&
			FHoudiniEngineUtils::ConvertStorageType(AttribInfo.storage) == EHoudiniStorageType::Int)  // Currently only support i@unreal_output_zone_shape = 1 on detail
		{
			int bIsZoneShape = 0;
			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeIntData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
				HAPI_ATTRIB_UNREAL_OUTPUT_ZONE_SHAPE, &AttribInfo, 1, &bIsZoneShape, 0, 1));

			bOutIsValid = bool(bIsZoneShape);
			return true;
		}
	}

	return true;
}


UZoneShapeComponent* FHoudiniZoneShapeOutput::Find(const AHoudiniNode* Node) const
{
	return Find_Internal<false>(Component, Node);
}

UZoneShapeComponent* FHoudiniZoneShapeOutput::CreateOrUpdate(AHoudiniNode* Node, const FString& InSplitValue, const bool& bInSplitActor)
{
	return CreateOrUpdate_Internal<false>(Component, Node, InSplitValue, bInSplitActor);
}

void FHoudiniZoneShapeOutput::Destroy(const AHoudiniNode* Node) const
{
	DestroyComponent(Node, Component, false);
	Component.Reset();
}

namespace HoudiniZoneShapeOutputUtils
{
	static FZoneGraphTagMask FindOrCreateZoneGraphTag(UZoneGraphSettings* ZoneGraphSettings, const FName& TagName, bool& bZoneGraphSettingsModified);

	static bool HapiGetOrCreateTags(const int32& NodeId, const int32& PartId, UZoneGraphSettings* ZoneGraphSettings,
		HAPI_AttributeOwner& InOutOwner, TArray<FZoneGraphTagMask>& OutTags, bool& bZoneGraphSettingsModified);

	FORCEINLINE static uint32 GetLaneProfileHash(const TArray<FZoneLaneDesc>& Lanes)
	{
		return GetTypeHash(TArray<uint8>((uint8*)Lanes.GetData(), Lanes.GetTypeSize() * Lanes.Num()));
	}

	static FString GetLaneProfileString(const TArray<FZoneLaneDesc>& Lanes);

	static bool HapiGetOrCreateLaneProfiles(const int32& NodeId, const int32& PartId, UZoneGraphSettings* ZoneGraphSettings,
		const HAPI_AttributeOwner& NameOwner, const HAPI_AttributeOwner& LanesOwner, TMap<uint32, int32>& InOutHashProfileIdxMap, TArray<int32>& OutLaneProfileIndices, bool& bZoneGraphSettingsModified);

	static bool HapiGetOrCreateLaneProfiles(const int32& NodeId, const int32& PartId, const TArray<std::string>& AttribNames, const int AttribCounts[HAPI_ATTROWNER_MAX],
		UZoneGraphSettings* ZoneGraphSettings, const bool bIsOnPoints,
		HAPI_AttributeOwner& OutLaneProfileOwner, TMap<uint32, int32>& InOutHashProfileIdxMap, TArray<int32>& OutLaneProfileIndices, bool& bZoneGraphSettingsModified);
}

FZoneGraphTagMask HoudiniZoneShapeOutputUtils::FindOrCreateZoneGraphTag(UZoneGraphSettings* ZoneGraphSettings, const FName& TagName, bool& bZoneGraphSettingsModified)
{
	TConstArrayView<FZoneGraphTagInfo> ConstTagInfos = ZoneGraphSettings->GetTagInfos();
	TArrayView<FZoneGraphTagInfo> TagInfos = *((TArrayView<FZoneGraphTagInfo>*)&ConstTagInfos);

	// First, try find the tag by name
	const int32 FoundTagIdx = TagInfos.IndexOfByPredicate([&TagName](const FZoneGraphTagInfo& TagInfo) { return TagInfo.Name == TagName; });
	if (TagInfos.IsValidIndex(FoundTagIdx))
		return TagInfos[FoundTagIdx].Tag;
	
	// Second, try find tag that is invalid (name is none) and set name, as a created tag
	for (FZoneGraphTagInfo& TagInfo : TagInfos)
	{
		if (!TagInfo.IsValid())
		{
			TagInfo.Name = TagName;
			bZoneGraphSettingsModified = true;
			return TagInfo.Tag;
		}
	}

	UE_LOG(LogHoudiniEngine, Error, TEXT("Cannot create zone graph tag: %s"), *TagName.ToString());
	return FZoneGraphTagMask(1);
}

bool HoudiniZoneShapeOutputUtils::HapiGetOrCreateTags(const int32& NodeId, const int32& PartId, UZoneGraphSettings* ZoneGraphSettings,
	HAPI_AttributeOwner& InOutOwner, TArray<FZoneGraphTagMask>& OutTags, bool& bZoneGraphSettingsModified)
{
	if (InOutOwner == HAPI_ATTROWNER_INVALID)
		return true;

	HAPI_AttributeInfo AttribInfo;
	HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeInfo(FHoudiniEngine::Get().GetSession(),
		NodeId, PartId, HAPI_ATTRIB_UNREAL_ZONE_SHAPE_TAGS, InOutOwner, &AttribInfo));

	TArray<int32> Counts;
	TArray<HAPI_StringHandle> SHs;
	if (AttribInfo.storage == HAPI_STORAGETYPE_STRING)
	{
		SHs.SetNumUninitialized(AttribInfo.count);
		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeStringData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
			HAPI_ATTRIB_UNREAL_ZONE_SHAPE_TAGS, &AttribInfo, SHs.GetData(), 0, AttribInfo.count));
	}
	else if (AttribInfo.storage == HAPI_STORAGETYPE_STRING_ARRAY)
	{
		Counts.SetNumUninitialized(AttribInfo.count);
		SHs.SetNumUninitialized(AttribInfo.totalArrayElements);
		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeStringArrayData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
			HAPI_ATTRIB_UNREAL_ZONE_SHAPE_TAGS, &AttribInfo, SHs.GetData(), AttribInfo.totalArrayElements, Counts.GetData(), 0, AttribInfo.count));
	}
	else
		return true;

	TMap<HAPI_StringHandle, FZoneGraphTagMask> SHTagMap;
	{
		TArray<HAPI_StringHandle> TagSHs = TSet<HAPI_StringHandle>(SHs).Array();
		TArray<std::string> TagNames;
		HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiConvertStringHandles(TagSHs, TagNames));
		for (int32 TagIdx = 0; TagIdx < TagSHs.Num(); ++TagIdx)
			SHTagMap.Add(TagSHs[TagIdx], TagNames[TagIdx].empty() ? FZoneGraphTagMask() : FindOrCreateZoneGraphTag(ZoneGraphSettings, TagNames[TagIdx].c_str(), bZoneGraphSettingsModified));
	}

	if (AttribInfo.storage == HAPI_STORAGETYPE_STRING)
	{
		OutTags.SetNumUninitialized(AttribInfo.count);
		for (int32 ElemIdx = 0; ElemIdx < AttribInfo.count; ++ElemIdx)
			OutTags[ElemIdx] = SHTagMap[SHs[ElemIdx]];
	}
	else if (AttribInfo.storage == HAPI_STORAGETYPE_STRING_ARRAY)
	{
		OutTags.SetNum(AttribInfo.count);
		int32 ArrayElemIdx = 0;
		for (int32 ElemIdx = 0; ElemIdx < AttribInfo.count; ++ElemIdx)
		{
			for (int32 ArrayIdx = 0; ArrayIdx < Counts[ElemIdx]; ++ArrayIdx)
			{
				OutTags[ElemIdx].Add(SHTagMap[SHs[ArrayElemIdx]]);
				++ArrayElemIdx;
			}
		}
	}

	return true;
}

FString HoudiniZoneShapeOutputUtils::GetLaneProfileString(const TArray<FZoneLaneDesc>& Lanes)
{
	FString ProfileStr;
	for (int32 LaneIdx = Lanes.Num() - 1; LaneIdx >= 0; --LaneIdx)
	{
		switch (Lanes[LaneIdx].Direction)
		{
		case EZoneLaneDirection::Forward: ProfileStr += TEXT("\u2191"); break;
		case EZoneLaneDirection::Backward: ProfileStr += TEXT("\u2193"); break;
		case EZoneLaneDirection::None: ProfileStr += TEXT("X"); break;
		}
	}

	return ProfileStr;
}

bool HoudiniZoneShapeOutputUtils::HapiGetOrCreateLaneProfiles(const int32& NodeId, const int32& PartId, UZoneGraphSettings* ZoneGraphSettings,
	const HAPI_AttributeOwner& NameOwner, const HAPI_AttributeOwner& LanesOwner, TMap<uint32, int32>& InOutHashProfileIdxMap, TArray<int32>& OutLaneProfileIndices, bool& bZoneGraphSettingsModified)
{
	HAPI_AttributeInfo AttribInfo;

	TArray<FName> LaneProfileNames;
	if (NameOwner != HAPI_ATTROWNER_INVALID)
	{
		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeInfo(FHoudiniEngine::Get().GetSession(),
			NodeId, PartId, HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE_NAME, NameOwner, &AttribInfo));

		if (AttribInfo.storage == HAPI_STORAGETYPE_STRING)  // If is string, then means this is the name of a lane profile name
		{
			TArray<HAPI_StringHandle> SHs;
			SHs.SetNumUninitialized(AttribInfo.count);
			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeStringData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
				HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE, &AttribInfo, SHs.GetData(), 0, AttribInfo.count));

			TMap<HAPI_StringHandle, FName> SHNameMap;
			{
				TArray<HAPI_StringHandle> UniqueSHs = TSet<HAPI_StringHandle>(SHs).Array();
				TArray<std::string> UniqueNames;
				FHoudiniEngineUtils::HapiConvertStringHandles(UniqueSHs, UniqueNames);
				for (int32 UniqueIdx = 0; UniqueIdx < UniqueSHs.Num(); ++UniqueIdx)
					SHNameMap.Add(UniqueSHs[UniqueIdx], UniqueNames[UniqueIdx].c_str());
			}
			LaneProfileNames.SetNum(AttribInfo.count);
			for (int32 ElemIdx = 0; ElemIdx < AttribInfo.count; ++ElemIdx)
				LaneProfileNames[ElemIdx] = SHNameMap[SHs[ElemIdx]];
		}
	}

	if (LanesOwner != HAPI_ATTROWNER_INVALID)
	{
		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeInfo(FHoudiniEngine::Get().GetSession(),
			NodeId, PartId, HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE, LanesOwner, &AttribInfo));

		if (AttribInfo.storage == HAPI_STORAGETYPE_DICTIONARY_ARRAY)  // Means we should find or create a lane profile
		{
			TArray<int32> Counts;
			Counts.SetNumUninitialized(AttribInfo.count);
			TArray<HAPI_StringHandle> SHs;
			SHs.SetNumUninitialized(AttribInfo.totalArrayElements);
			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeDictionaryArrayData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
				HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE, &AttribInfo, SHs.GetData(), AttribInfo.totalArrayElements, Counts.GetData(), 0, AttribInfo.count));

			// HAPI BUG: GetAttributeDictionaryArrayData will get all sh unique, we could only find unique strs in unreal
			TArray<FString> LaneDictStrs;
			HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiConvertStringHandles(SHs, LaneDictStrs));
			TMap<FString, FZoneLaneDesc> SHLaneMap;
			for (int32 ArrayIdx = 0; ArrayIdx < SHs.Num(); ++ArrayIdx)
			{
				if (SHLaneMap.Contains(LaneDictStrs[ArrayIdx]))
					continue;

				FZoneLaneDesc Lane = FZoneLaneDesc();
				TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(LaneDictStrs[ArrayIdx]);
				TSharedPtr<FJsonObject> JsonLane;
				if (FJsonSerializer::Deserialize(JsonReader, JsonLane))
				{
					float LaneOffset = 0.0;
					if (JsonLane->TryGetNumberField(TEXT("Width"), LaneOffset))
						Lane.Width = LaneOffset * POSITION_SCALE_TO_UNREAL_F;

					int32 LaneDirInt = 0;
					if (JsonLane->TryGetNumberField(TEXT("Direction"), LaneDirInt))
						Lane.Direction = EZoneLaneDirection(LaneDirInt);
					else
					{
						FString LaneDirStr;
						if (JsonLane->TryGetStringField(TEXT("Direction"), LaneDirStr))
						{
							if (LaneDirStr == TEXT("None"))
								Lane.Direction = EZoneLaneDirection::None;
							else if (LaneDirStr == TEXT("Backward"))
								Lane.Direction = EZoneLaneDirection::Backward;
						}
					}

					const TArray<TSharedPtr<FJsonValue>>* JsonTagNames;
					if (JsonLane->TryGetArrayField(TEXT("Tags"), JsonTagNames))
					{
						Lane.Tags = FZoneGraphTagMask(0);
						for (const TSharedPtr<FJsonValue>& JsonTagName : *JsonTagNames)
						{
							FString TagName;
							if (JsonTagName->TryGetString(TagName))
								Lane.Tags.Add(FindOrCreateZoneGraphTag(ZoneGraphSettings, *TagName, bZoneGraphSettingsModified));
						}
						if (Lane.Tags == FZoneGraphTagMask(0))
							Lane.Tags = FZoneGraphTagMask(1);
					}
					else
					{
						FString TagName;
						if (JsonLane->TryGetStringField(TEXT("Tag"), TagName))
							Lane.Tags = FindOrCreateZoneGraphTag(ZoneGraphSettings, *TagName, bZoneGraphSettingsModified);
					}
				}

				SHLaneMap.Add(LaneDictStrs[ArrayIdx], Lane);
			}

			OutLaneProfileIndices.SetNumUninitialized(AttribInfo.count);
			int32 AccumulatedCount = 0;
			for (int32 ElemIdx = 0; ElemIdx < AttribInfo.count; ++ElemIdx)
			{
				const FName LaneProfileName = LaneProfileNames.IsEmpty() ? NAME_None : LaneProfileNames[NameOwner == HAPI_ATTROWNER_DETAIL ? 0 : ElemIdx];
				const int32& Count = Counts[ElemIdx];
				if (Count <= 0)  // Fallback to try to find lane profile by name
				{
					OutLaneProfileIndices[ElemIdx] = LaneProfileNames.IsEmpty() ? -1 : ZoneGraphSettings->GetLaneProfiles().IndexOfByPredicate(
						[LaneProfileName](const FZoneLaneProfile& LaneProfile) { return LaneProfile.Name == LaneProfileName; });
					continue;
				}

				TArray<FZoneLaneDesc> Lanes;
				for (int32 ArrayIdx = AccumulatedCount; ArrayIdx < AccumulatedCount + Count; ++ArrayIdx)
					Lanes.Add(SHLaneMap[LaneDictStrs[ArrayIdx]]);

				const uint32 HashValue = GetLaneProfileHash(Lanes);
				if (const int32* FoundProfileIdxPtr = InOutHashProfileIdxMap.Find(HashValue))
					OutLaneProfileIndices[ElemIdx] = *FoundProfileIdxPtr;
				else  // Create a new lane profile
				{
					FZoneLaneProfile NewLaneProfile;
					NewLaneProfile.Name = LaneProfileName.IsNone() ?
						FName(HOUDINI_LANE_PROFILE_PREFIX + GetLaneProfileString(Lanes), FMath::Abs(int32(HashValue))) : LaneProfileName;
					NewLaneProfile.Lanes = Lanes;

					const int32 NewProfileIdx = ((TArray<FZoneLaneProfile>*) & ZoneGraphSettings->GetLaneProfiles())->Add(NewLaneProfile);
					bZoneGraphSettingsModified = true;

					InOutHashProfileIdxMap.Add(HashValue, NewProfileIdx);
					OutLaneProfileIndices[ElemIdx] = NewProfileIdx;
				}
				AccumulatedCount += Count;
			}
		}
	}

	if (OutLaneProfileIndices.IsEmpty() && !LaneProfileNames.IsEmpty())  // Fallback to try to find lane profile by name
	{
		OutLaneProfileIndices.SetNumUninitialized(LaneProfileNames.Num());
		TMap<FName, int32> NameIdxMap;
		for (int32 ElemIdx = 0; ElemIdx < LaneProfileNames.Num(); ++ElemIdx)
		{
			const FName LaneProfileName = LaneProfileNames[ElemIdx];
			if (LaneProfileName.IsNone())
			{
				OutLaneProfileIndices[ElemIdx] = -1;
				continue;
			}

			if (const int32* IdxPtr = NameIdxMap.Find(LaneProfileName))
				OutLaneProfileIndices[ElemIdx] = *IdxPtr;
			else
			{
				const int32 Idx = ZoneGraphSettings->GetLaneProfiles().IndexOfByPredicate(
					[LaneProfileName](const FZoneLaneProfile& LaneProfile) { return LaneProfile.Name == LaneProfileName; });
				NameIdxMap.Add(LaneProfileName, Idx);
				OutLaneProfileIndices[ElemIdx] = Idx;
			}
		}
	}

	return true;
}

bool HoudiniZoneShapeOutputUtils::HapiGetOrCreateLaneProfiles(const int32& NodeId, const int32& PartId, const TArray<std::string>& AttribNames, const int AttribCounts[HAPI_ATTROWNER_MAX],
	UZoneGraphSettings* ZoneGraphSettings, const bool bIsOnPoints,
	HAPI_AttributeOwner& OutLaneProfileOwner, TMap<uint32, int32>& InOutHashProfileIdxMap, TArray<int32>& OutLaneProfileIndices, bool& bZoneGraphSettingsModified)
{
	OutLaneProfileOwner = HAPI_ATTROWNER_INVALID;

	const HAPI_AttributeOwner Owner0 = bIsOnPoints ? HAPI_ATTROWNER_VERTEX : HAPI_ATTROWNER_PRIM;
	const HAPI_AttributeOwner Owner1 = bIsOnPoints ? HAPI_ATTROWNER_POINT : HAPI_ATTROWNER_DETAIL;

	HAPI_AttributeOwner LaneProfileNameOwner = HAPI_ATTROWNER_INVALID;
	if (FHoudiniEngineUtils::IsAttributeExists(AttribNames, AttribCounts,
		HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE_NAME, Owner0))
		LaneProfileNameOwner = Owner0;
	else if (FHoudiniEngineUtils::IsAttributeExists(AttribNames, AttribCounts,
		HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE_NAME, Owner1))
		LaneProfileNameOwner = Owner1;

	if (FHoudiniEngineUtils::IsAttributeExists(AttribNames, AttribCounts,
		HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE, Owner0))
		OutLaneProfileOwner = Owner0;
	else if (FHoudiniEngineUtils::IsAttributeExists(AttribNames, AttribCounts,
		HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE, Owner1))
		OutLaneProfileOwner = Owner1;

	if ((LaneProfileNameOwner != HAPI_ATTROWNER_INVALID) || (OutLaneProfileOwner != HAPI_ATTROWNER_INVALID))
	{
		HOUDINI_FAIL_RETURN(HapiGetOrCreateLaneProfiles(NodeId, PartId,
			ZoneGraphSettings, LaneProfileNameOwner, OutLaneProfileOwner, InOutHashProfileIdxMap, OutLaneProfileIndices, bZoneGraphSettingsModified));

		if (OutLaneProfileOwner == HAPI_ATTROWNER_INVALID)
			OutLaneProfileOwner = LaneProfileNameOwner;
	}

	return true;
}

using namespace HoudiniZoneShapeOutputUtils;


bool UHoudiniOutputZoneShape::HapiUpdate(const HAPI_GeoInfo& GeoInfo, const TArray<HAPI_PartInfo>& PartInfos)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HoudiniOutputZoneShape);

	const AHoudiniNode* Node = GetNode();


	struct FHoudiniCurveIndicesHolder
	{
		FHoudiniCurveIndicesHolder(const int8& UpdateMode, const FString& InSplitValue) :
			PartialOutputMode(UpdateMode), SplitValue(InSplitValue) {}

		int8 PartialOutputMode = 0;
		FString SplitValue;

		TArray<int32> CurveIndices;
	};

	struct FHoudiniCurvesPart
	{
		FHoudiniCurvesPart(const HAPI_PartInfo& PartInfo) : Info(PartInfo) {}

		HAPI_PartInfo Info;
		TArray<std::string> AttribNames;
		TArray<int32> VertexIndices;  // Accumulate append CurveCounts
		TMap<int32, FHoudiniCurveIndicesHolder> SplitCurvesMap;
	};


	const int32& NodeId = GeoInfo.nodeId;


	// -------- Retrieve all part data --------
	TArray<FHoudiniCurvesPart> Parts;
	for (const HAPI_PartInfo& PartInfo : PartInfos)
		Parts.Add(FHoudiniCurvesPart(PartInfo));

	bool bPartialUpdate = false;
	
	for (FHoudiniCurvesPart& Part : Parts)
	{
		const HAPI_PartInfo& PartInfo = Part.Info;
		const HAPI_PartId& PartId = PartInfo.id;


		// -------- Retrieve attrib and group names --------
		HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetAttributeNames(NodeId, PartId, PartInfo.attributeCounts, Part.AttribNames));
		const TArray<std::string>& AttribNames = Part.AttribNames;


		// -------- Retrieve split values and partial output modes if exists --------
		TArray<int32> SplitKeys;  // Maybe int or HAPI_StringHandle
		HAPI_AttributeOwner SplitAttribOwner = HAPI_ATTROWNER_PRIM;  // Prefer on prim
		TMap<HAPI_StringHandle, FString> SplitValueMap;
		FHoudiniOutputUtils::HapiGetSplitValues(NodeId, PartId, AttribNames, PartInfo.attributeCounts,
			SplitKeys, SplitValueMap, SplitAttribOwner);
		const bool bHasSplitValues = !SplitKeys.IsEmpty();

		HAPI_AttributeOwner PartialOutputModeOwner = bHasSplitValues ? FHoudiniEngineUtils::QueryAttributeOwner(AttribNames,
			PartInfo.attributeCounts, HAPI_ATTRIB_PARTIAL_OUTPUT_MODE) : HAPI_ATTROWNER_INVALID;
		TArray<int8> PartialOutputModes;
		HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetEnumAttributeData(NodeId, PartId,
			HAPI_ATTRIB_PARTIAL_OUTPUT_MODE, PartialOutputModes, PartialOutputModeOwner));


		// -------- Retrieve vertex list --------
		TArray<int32> CurveCounts;
		CurveCounts.SetNumUninitialized(PartInfo.faceCount);
		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetCurveCounts(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
			CurveCounts.GetData(), 0, PartInfo.faceCount));


		// -------- Split curves --------
		TMap<int32, FHoudiniCurveIndicesHolder>& SplitMap = Part.SplitCurvesMap;
		FHoudiniCurveIndicesHolder AllCurves(HAPI_PARTIAL_OUTPUT_MODE_REPLACE, FString());
		int32 VertexIdx = 0;  // The first vertex/point index of this curve, will accumulate each CurveCount
		for (int32 CurveIdx = 0; CurveIdx < PartInfo.faceCount; ++CurveIdx)
		{
			// Same as HoudiniOutputMesh
			// Judge PartialOutputMode, if remove && previous NOT set, then we will NOT parse the GroupIdx
			const int32 SplitKey = bHasSplitValues ?
				SplitKeys[FHoudiniOutputUtils::CurveAttributeEntryIdx(SplitAttribOwner, VertexIdx, CurveIdx)] : 0;
			FHoudiniCurveIndicesHolder* FoundHolderPtr = bHasSplitValues ? SplitMap.Find(SplitKey) : nullptr;

			const int8 PartialOutputMode = FMath::Clamp(PartialOutputModes.IsEmpty() ? HAPI_PARTIAL_OUTPUT_MODE_REPLACE :
				PartialOutputModes[FHoudiniOutputUtils::CurveAttributeEntryIdx(PartialOutputModeOwner, VertexIdx, CurveIdx)],
				HAPI_PARTIAL_OUTPUT_MODE_REPLACE, HAPI_PARTIAL_OUTPUT_MODE_REMOVE);

			if (PartialOutputMode == HAPI_PARTIAL_OUTPUT_MODE_MODIFY)
				bPartialUpdate = true;
			else if (PartialOutputMode == HAPI_PARTIAL_OUTPUT_MODE_REMOVE)  // If has PartialOutputModes, then must also HasSplitValues
			{
				bPartialUpdate = true;
				if (FoundHolderPtr)  // If previous triangles has identified PartialOutputMode, We should NOT change it
				{
					if (FoundHolderPtr->PartialOutputMode == HAPI_PARTIAL_OUTPUT_MODE_REMOVE)
						continue;
				}
				else
				{
					SplitMap.Add(SplitKey, FHoudiniCurveIndicesHolder(HAPI_PARTIAL_OUTPUT_MODE_REMOVE, GET_SPLIT_VALUE_STR));
					continue;
				}
			}

			if (bHasSplitValues)
			{
				if (!FoundHolderPtr)
					FoundHolderPtr = &SplitMap.Add(SplitKey, FHoudiniCurveIndicesHolder(PartialOutputMode, GET_SPLIT_VALUE_STR));
				FoundHolderPtr->CurveIndices.Add(CurveIdx);
			}
			else
				AllCurves.CurveIndices.Add(CurveIdx);

			VertexIdx += CurveCounts[CurveIdx];
			Part.VertexIndices.Add(VertexIdx);
		}

		if (!bHasSplitValues)
			SplitMap.Add(0, AllCurves);  // We just add AllLodTrianglesMap to SplitMeshMap
	}

	TDoubleLinkedList<FHoudiniZoneShapeOutput*> OldZoneShapeOutputs;
	TArray<FHoudiniZoneShapeOutput> NewZoneShapeOutputs;

	if (bPartialUpdate)
	{
		TSet<FString> ModifySplitValues;
		TSet<FString> RemoveSplitValues;
		for (FHoudiniCurvesPart& Part : Parts)
		{
			for (TMap<int32, FHoudiniCurveIndicesHolder>::TIterator SplitIter(Part.SplitCurvesMap); SplitIter; ++SplitIter)
			{
				if (SplitIter->Value.PartialOutputMode >= HAPI_PARTIAL_OUTPUT_MODE_REMOVE)
				{
					RemoveSplitValues.FindOrAdd(SplitIter->Value.SplitValue);
					SplitIter.RemoveCurrent();
				}
				else
					ModifySplitValues.FindOrAdd(SplitIter->Value.SplitValue);
			}
		}

		FHoudiniOutputUtils::UpdateSplittableOutputHolders(ModifySplitValues, RemoveSplitValues, ZoneShapeOutputs,
			[Node](const FHoudiniZoneShapeOutput& OldZSOutput) { return IsValid(OldZSOutput.Find(Node)); }, OldZoneShapeOutputs, NewZoneShapeOutputs);
	}
	else  // Collect valid old output holders for reuse
		FHoudiniOutputUtils::UpdateOutputHolders(ZoneShapeOutputs,
			[Node](const FHoudiniZoneShapeOutput& OldZSOutput) { return IsValid(OldZSOutput.Find(Node)); }, OldZoneShapeOutputs);

	FHoudiniEngine::Get().FinishHoudiniMainTaskMessage();  // Avoid RHI crash

	UZoneGraphSettings* ZoneGraphSettings = GetMutableDefault<UZoneGraphSettings>();
	
	TMap<uint32, int32> HashProfileIdxMap;  // Find all unique lane profiles
	for (int32 ProfileIdx = 0; ProfileIdx < ZoneGraphSettings->GetLaneProfiles().Num(); ++ProfileIdx)
		HashProfileIdxMap.FindOrAdd(GetLaneProfileHash(ZoneGraphSettings->GetLaneProfiles()[ProfileIdx].Lanes), ProfileIdx);

	FArrayProperty* ShapeConnectorsProp = CastField<FArrayProperty>(UZoneShapeComponent::StaticClass()->FindPropertyByName("ShapeConnectors"));
	FArrayProperty* ConnectedShapesProp = CastField<FArrayProperty>(UZoneShapeComponent::StaticClass()->FindPropertyByName("ConnectedShapes"));

	bool bZoneGraphSettingsModified = false;
	TMap<AActor*, TArray<FString>> ActorPropertyNamesMap;  // Use to avoid Set the same property in same SplitActor twice
	HAPI_AttributeInfo AttribInfo;
	TArray<UZoneShapeComponent*> ChangedZSCs;
	for (const FHoudiniCurvesPart& Part : Parts)
	{
		if (Part.SplitCurvesMap.IsEmpty())
			continue;

		const HAPI_PartInfo& PartInfo = Part.Info;
		const HAPI_PartId& PartId = PartInfo.id;
		const TArray<std::string>& AttribNames = Part.AttribNames;

		// -------- Transforms --------
		TArray<float> PositionData;
		PositionData.SetNumUninitialized(PartInfo.pointCount * 3);

		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeInfo(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
			HAPI_ATTRIB_POSITION, HAPI_ATTROWNER_POINT, &AttribInfo));

		HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
			HAPI_ATTRIB_POSITION, &AttribInfo, -1, PositionData.GetData(), 0, PartInfo.pointCount));

		HAPI_AttributeOwner RotOwner = FHoudiniEngineUtils::QueryAttributeOwner(AttribNames, PartInfo.attributeCounts, HAPI_ATTRIB_ROT);
		TArray<FRotator> Rots;
		if (RotOwner != HAPI_ATTROWNER_INVALID)
		{
			HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeInfo(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
				HAPI_ATTRIB_ROT, RotOwner, &AttribInfo));

			if (((AttribInfo.storage == HAPI_STORAGETYPE_FLOAT) || (AttribInfo.storage == HAPI_STORAGETYPE_FLOAT64)) &&
				((AttribInfo.tupleSize == 3) || (AttribInfo.tupleSize == 4)))
			{
				TArray<float> RotData;
				RotData.SetNumUninitialized(AttribInfo.count * AttribInfo.tupleSize);

				HAPI_SESSION_FAIL_RETURN(FHoudiniApi::GetAttributeFloatData(FHoudiniEngine::Get().GetSession(), NodeId, PartId,
					HAPI_ATTRIB_ROT, &AttribInfo, -1, RotData.GetData(), 0, AttribInfo.count));

				Rots.SetNumUninitialized(AttribInfo.count);
				if (AttribInfo.tupleSize == 4)
				{
					for (int32 ElemIdx = 0; ElemIdx < AttribInfo.count; ++ElemIdx)
						Rots[ElemIdx] = FQuat(RotData[ElemIdx * 4], RotData[ElemIdx * 4 + 2], RotData[ElemIdx * 4 + 1], -RotData[ElemIdx * 4 + 3]).Rotator();
				}
				else
				{
					for (int32 ElemIdx = 0; ElemIdx < AttribInfo.count; ++ElemIdx)
						Rots[ElemIdx] = FRotator(FMath::RadiansToDegrees(RotData[ElemIdx * 3]), FMath::RadiansToDegrees(RotData[ElemIdx * 3 + 2]), FMath::RadiansToDegrees(RotData[ElemIdx * 3 + 1]));
				}
			}
			else
				RotOwner = HAPI_ATTROWNER_INVALID;
		}

		HAPI_AttributeOwner ZoneShapeTypeOwner = FHoudiniEngineUtils::QueryAttributeOwner(AttribNames, PartInfo.attributeCounts, HAPI_ATTRIB_UNREAL_ZONE_SHAPE_TYPE);
		TArray<int8> ZoneShapeTypes;
		HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetEnumAttributeData(NodeId, PartId,
			HAPI_ATTRIB_UNREAL_ZONE_SHAPE_TYPE, [](const FUtf8StringView& AttribValue)
			{
				if ((UE::String::FindFirst(AttribValue, "polygon", ESearchCase::IgnoreCase) != INDEX_NONE))
					return 1;
				return 0;
			}, ZoneShapeTypes, ZoneShapeTypeOwner));

		// Lane Profile
		TArray<int32> PointLaneProfileIndices;
		HAPI_AttributeOwner LaneProfileOwner = HAPI_ATTROWNER_INVALID;  // For Curve, maybe on prim or detail
		TArray<int32> LaneProfileIndices;  // For Curve, maybe on prim or detail
		{
			// We should check whether vertex or point has lane profile attrib
			HAPI_AttributeOwner PointLaneProfileOwner;
			HOUDINI_FAIL_RETURN(HapiGetOrCreateLaneProfiles(NodeId, PartId, AttribNames, PartInfo.attributeCounts,
				ZoneGraphSettings, true, PointLaneProfileOwner, HashProfileIdxMap, PointLaneProfileIndices, bZoneGraphSettingsModified));

			// We should also check whether prim or detail has lane profile attrib
			HOUDINI_FAIL_RETURN(HapiGetOrCreateLaneProfiles(NodeId, PartId, AttribNames, PartInfo.attributeCounts,
				ZoneGraphSettings, false, LaneProfileOwner, HashProfileIdxMap, LaneProfileIndices, bZoneGraphSettingsModified));
		}

		// Zone Shape Tags
		HAPI_AttributeOwner ZoneGraphTagOwner = FHoudiniEngineUtils::QueryAttributeOwner(AttribNames, PartInfo.attributeCounts, HAPI_ATTRIB_UNREAL_ZONE_SHAPE_TAGS);
		TArray<FZoneGraphTagMask> ZoneGraphTags;
		HOUDINI_FAIL_RETURN(HapiGetOrCreateTags(NodeId, PartId, ZoneGraphSettings, ZoneGraphTagOwner, ZoneGraphTags, bZoneGraphSettingsModified));

		// Common
		HAPI_AttributeOwner SplitActorsOwner = FHoudiniEngineUtils::QueryAttributeOwner(AttribNames, PartInfo.attributeCounts, HAPI_ATTRIB_UNREAL_SPLIT_ACTORS);
		TArray<int8> bSplitActors;
		HOUDINI_FAIL_RETURN(FHoudiniEngineUtils::HapiGetEnumAttributeData(NodeId, PartId,
			HAPI_ATTRIB_UNREAL_SPLIT_ACTORS, bSplitActors, SplitActorsOwner));

		TArray<TSharedPtr<FHoudiniAttribute>> PropAttribs;
		HOUDINI_FAIL_RETURN(FHoudiniAttribute::HapiRetrieveAttributes(NodeId, PartId, AttribNames, PartInfo.attributeCounts,
			HAPI_ATTRIB_PREFIX_UNREAL_UPROPERTY, PropAttribs));

		const TArray<int32>& VertexIndices = Part.VertexIndices;
		for (const auto& SplitCurves : Part.SplitCurvesMap)
		{
			const FString& SplitValue = SplitCurves.Value.SplitValue;

			for (const int32& CurveIdx : SplitCurves.Value.CurveIndices)
			{
				const int32 MainVertexIdx = (CurveIdx == 0) ? 0 : VertexIndices[CurveIdx - 1];

				bool bSplitActor = false;
				if (!bSplitActors.IsEmpty())
					bSplitActor = bSplitActors[FHoudiniOutputUtils::CurveAttributeEntryIdx(SplitActorsOwner, MainVertexIdx, CurveIdx)] >= 1;

				FHoudiniZoneShapeOutput NewZSOutput;
				if (FHoudiniZoneShapeOutput* FoundZSOutput = FHoudiniOutputUtils::FindOutputHolder(OldZoneShapeOutputs,
					[&](FHoudiniZoneShapeOutput* OldZSOutput) { return OldZSOutput->CanReuse(SplitValue, bSplitActor); }))
					NewZSOutput = *FoundZSOutput;

				UZoneShapeComponent* ZSC = NewZSOutput.CreateOrUpdate(GetNode(), SplitValue, bSplitActor);

				// We should judge ZoneShapeType first, if is Polygon, then points should have LaneProfile
				if (!ZoneShapeTypes.IsEmpty())
					ZSC->SetShapeType((FZoneShapeType)ZoneShapeTypes[FHoudiniOutputUtils::CurveAttributeEntryIdx(ZoneShapeTypeOwner, MainVertexIdx, CurveIdx)]);

				if (!ZoneGraphTags.IsEmpty())
					ZSC->SetTags(ZoneGraphTags[FHoudiniOutputUtils::CurveAttributeEntryIdx(ZoneShapeTypeOwner, MainVertexIdx, CurveIdx)]);

				if (!LaneProfileIndices.IsEmpty())
				{
					const int32 LaneProfileIdx = LaneProfileIndices[FHoudiniOutputUtils::CurveAttributeEntryIdx(LaneProfileOwner, MainVertexIdx, CurveIdx)];
					if (ZoneGraphSettings->GetLaneProfiles().IsValidIndex(LaneProfileIdx))
						ZSC->SetCommonLaneProfile(ZoneGraphSettings->GetLaneProfiles()[LaneProfileIdx]);
				}

				TArray<FZoneShapePoint>& Points = ZSC->GetMutablePoints();
				
				const int32 StartVertexIdx = ((CurveIdx == 0) ? 0 : VertexIndices[CurveIdx - 1]);
				const int32 NumCurvePoints = VertexIndices[CurveIdx] - StartVertexIdx;
				Points.SetNum(NumCurvePoints);

				ZSC->ClearPerPointLaneProfiles();

				for (int32 PointIdx = 0; PointIdx < NumCurvePoints; ++PointIdx)
				{
					FZoneShapePoint& Point = Points[PointIdx];
					Point = FZoneShapePoint();  // Reset
					const int32 GlobalPointIdx = PointIdx + StartVertexIdx;
					Point.Position = FVector(PositionData[GlobalPointIdx * 3], PositionData[GlobalPointIdx * 3 + 2], PositionData[GlobalPointIdx * 3 + 1]) * POSITION_SCALE_TO_UNREAL;
					if (!Rots.IsEmpty())
						Point.Rotation = Rots[FHoudiniOutputUtils::CurveAttributeEntryIdx(RotOwner, GlobalPointIdx, CurveIdx)];

					Point.LaneProfile = FZoneShapePoint::InheritLaneProfile;
					if (!PointLaneProfileIndices.IsEmpty() && ZSC->GetShapeType() == FZoneShapeType::Polygon)
					{
						Point.Type = FZoneShapePointType::LaneProfile;
						const int32& LaneProfileIdx = PointLaneProfileIndices[GlobalPointIdx];
						if (ZoneGraphSettings->GetLaneProfiles().IsValidIndex(LaneProfileIdx))
							Point.LaneProfile = ZSC->AddUniquePerPointLaneProfile(ZoneGraphSettings->GetLaneProfiles()[LaneProfileIdx]);
					}

					for (const TSharedPtr<FHoudiniAttribute>& PropAttrib : PropAttribs)
					{
						const HAPI_AttributeOwner& PropAttribOwner = PropAttrib->GetOwner();
						if ((PropAttribOwner == HAPI_ATTROWNER_VERTEX) || (PropAttribOwner == HAPI_ATTROWNER_POINT))
							PropAttrib->SetStructPropertyValues(&Point, FZoneShapePoint::StaticStruct(),
								FHoudiniOutputUtils::CurveAttributeEntryIdx(PropAttribOwner, GlobalPointIdx, CurveIdx));
					}
				}

				// Set UProperties
				for (const TSharedPtr<FHoudiniAttribute>& PropAttrib : PropAttribs)
				{
					const HAPI_AttributeOwner& PropAttribOwner = PropAttrib->GetOwner();
					if ((PropAttribOwner == HAPI_ATTROWNER_PRIM) || (PropAttribOwner == HAPI_ATTROWNER_DETAIL))
						PropAttrib->SetObjectPropertyValues(ZSC, FHoudiniOutputUtils::CurveAttributeEntryIdx(PropAttribOwner, MainVertexIdx, CurveIdx));
				}
				SET_SPLIT_ACTOR_UPROPERTIES(NewZSOutput, FHoudiniOutputUtils::CurveAttributeEntryIdx(PropAttribOwner, MainVertexIdx, CurveIdx), false);
				
				// Avoid Crash when ZSC create scene proxy
				if (ShapeConnectorsProp && ConnectedShapesProp)
				{
					FScriptArrayHelper_InContainer ArrayHelper(ShapeConnectorsProp, ZSC);
					ArrayHelper.EmptyValues();
					ArrayHelper = FScriptArrayHelper_InContainer(ConnectedShapesProp, ZSC);
					ArrayHelper.EmptyValues();
				}

				ChangedZSCs.Add(ZSC);

				NewZoneShapeOutputs.Add(NewZSOutput);
			}
		}
	}

	// -------- Post-processing --------
	if (bZoneGraphSettingsModified)
		ZoneGraphSettings->TryUpdateDefaultConfigFile();

	// Destroy old outputs, like this->Destroy()
	for (const FHoudiniZoneShapeOutput* OldZSOutput : OldZoneShapeOutputs)
		OldZSOutput->Destroy(Node);
	OldZoneShapeOutputs.Empty();

	// Update output holders
	ZoneShapeOutputs = NewZoneShapeOutputs;

	// We should update shapes after useless ZSCs has been destroyed
	for (UZoneShapeComponent* ZSC : ChangedZSCs)
	{
		ZSC->UpdateShape();
		ZSC->Modify();
	}

	if (!ChangedZSCs.IsEmpty())
		AsyncTask(ENamedThreads::GameThread, [] { FHoudiniMassTranslator::Get().OnZoneShapeOutputFinish(); });  // After all outputs finished

	return true;
}

void UHoudiniOutputZoneShape::Destroy() const
{
	for (const FHoudiniZoneShapeOutput& OldZoneShapeOutput : ZoneShapeOutputs)
		OldZoneShapeOutput.Destroy(GetNode());
}

void UHoudiniOutputZoneShape::CollectActorSplitValues(TSet<FString>& InOutSplitValues, TSet<FString>& InOutEditableSplitValues) const
{
	for (const FHoudiniZoneShapeOutput& ZoneShapeOutput : ZoneShapeOutputs)
	{
		if (ZoneShapeOutput.IsSplitActor())
			InOutSplitValues.FindOrAdd(ZoneShapeOutput.GetSplitValue());
	}
}
