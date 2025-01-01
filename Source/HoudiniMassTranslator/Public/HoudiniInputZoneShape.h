// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#pragma once

#include "HoudiniInput.h"


class HOUDINIMASSTRANSLATOR_API FHoudiniZoneShapeComponentInput : public FHoudiniComponentInput
{
public:
	int32 NodeId = -1;

	virtual void Invalidate() const override {}  // Will then delete this, so we need NOT to reset node ids to -1

	virtual bool HapiDestroy(UHoudiniInput* Input) const override;  // Will then delete this, so we need NOT to reset node ids to -1
};

class HOUDINIMASSTRANSLATOR_API FHoudiniZoneShapeComponentInputBuilder : public IHoudiniComponentInputBuilder
{
public:
	virtual bool IsValidInput(const UActorComponent* Component) override;

	virtual bool HapiUpload(UHoudiniInput* Input, const bool& bIsSingleComponent,  // Is there only one single valid component in the whole blueprint/actor
		const TArray<const UActorComponent*>& Components, const TArray<FTransform>& Transforms, const TArray<int32>& ComponentIndices,  // Components and Transforms are all of the components in blueprint/actor, and ComponentIndices are ref the valid indices from IsValidInput
		int32& InOutInstancerNodeId, TArray<TSharedPtr<FHoudiniComponentInput>>& InOutComponentInputs, TArray<FHoudiniComponentInputPoint>& InOutPoints) override;

	virtual void AppendInfo(const TArray<const UActorComponent*>& Components, const TArray<FTransform>& Transforms, const TArray<int32>& ComponentIndices,  // See the comment upon
		const TSharedPtr<FJsonObject>& JsonObject) override;  // Append object info to JsonObject, keys are instance refs, values are JsonObjects that contain transoforms and meta data
};
