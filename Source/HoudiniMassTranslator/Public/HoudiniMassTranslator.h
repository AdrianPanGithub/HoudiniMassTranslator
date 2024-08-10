// Copyright (c) <2024> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"


struct FZoneGraphBuildData;
class SNotificationItem;

class FHoudiniZoneShapeComponentInputBuilder;
class FHoudiniZoneShapeOutputBuilder;

class FHoudiniMassTranslator : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	FORCEINLINE static FHoudiniMassTranslator& Get() { return *HoudiniMassTranslatorInstance; }

	void OnZoneShapeOutputFinish();

protected:
	static FHoudiniMassTranslator* HoudiniMassTranslatorInstance;

	TSharedPtr<FHoudiniZoneShapeComponentInputBuilder> ComponentInputBuilder;

	TSharedPtr<FHoudiniZoneShapeOutputBuilder> OutputBuilder;

	TSharedPtr<FUICommandList> Commands;
	
	TWeakPtr<SNotificationItem> Notification;

	void OnZoneGraphBuildDone(const FZoneGraphBuildData&);

	void OnZoneGraphBuildCancel(const bool);
};
