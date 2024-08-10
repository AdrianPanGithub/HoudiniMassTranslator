// Copyright (c) <2024> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#pragma once

#include "HoudiniOutput.h"

#include "HoudiniOutputZoneShape.generated.h"


class UZoneShapeComponent;

USTRUCT()
struct HOUDINIMASSTRANSLATOR_API FHoudiniZoneShapeOutput : public FHoudiniComponentOutput
{
	GENERATED_BODY()

protected:
	mutable TWeakObjectPtr<UZoneShapeComponent> Component;

public:
	UZoneShapeComponent* Find(const AHoudiniNode* Node) const;

	UZoneShapeComponent* CreateOrUpdate(AHoudiniNode* Node, const FString& InSplitValue, const bool& bSplitToActors);

	void Destroy(const AHoudiniNode* Node) const;
};

UCLASS()
class HOUDINIMASSTRANSLATOR_API UHoudiniOutputZoneShape : public UHoudiniOutput
{
	GENERATED_BODY()

protected:
	UPROPERTY()
	TArray<FHoudiniZoneShapeOutput> ZoneShapeOutputs;

public:
	virtual bool HapiUpdate(const HAPI_GeoInfo& GeoInfo, const TArray<HAPI_PartInfo>& PartInfos) override;

	virtual void Destroy() const override;

	virtual void CollectActorSplitValues(TSet<FString>& InOutSplitValues, TSet<FString>& InOutEditableSplitValues) const override;
};


class HOUDINIMASSTRANSLATOR_API FHoudiniZoneShapeOutputBuilder : public IHoudiniOutputBuilder
{
public:
	virtual bool HapiIsPartValid(const int32& NodeId, const HAPI_PartInfo& PartInfo, bool& bOutIsValid, bool& bOutShouldHoldByOutput) override;

	virtual TSubclassOf<UHoudiniOutput> GetClass() const override { return UHoudiniOutputZoneShape::StaticClass(); }
};