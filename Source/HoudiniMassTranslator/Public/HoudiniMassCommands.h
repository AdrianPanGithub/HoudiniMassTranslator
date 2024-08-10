// Copyright (c) <2024> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"


class FHoudiniMassCommands : public TCommands<FHoudiniMassCommands>
{
public:
	FHoudiniMassCommands();

	TSharedPtr<FUICommandInfo> CleanupLaneProfiles;

	TSharedPtr<FUICommandInfo> RemoveAllLaneProfiles;

	// TCommand<> interface
	virtual void RegisterCommands() override;
	
	static void OnCleanupLaneProfiles();

	static void OnRemoveAllLaneProfiles();
};