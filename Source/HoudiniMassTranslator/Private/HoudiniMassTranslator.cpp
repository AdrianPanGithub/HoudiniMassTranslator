// Copyright (c) <2024> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#include "HoudiniMassTranslator.h"

#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ZoneGraphDelegates.h"

#include "HoudiniEngine.h"
#include "HoudiniInputZoneShape.h"
#include "HoudiniOutputZoneShape.h"
#include "HoudiniMassCommands.h"


#define LOCTEXT_NAMESPACE "FHoudiniMassTranslatorModule"

FHoudiniMassTranslator* FHoudiniMassTranslator::HoudiniMassTranslatorInstance = nullptr;

void FHoudiniMassTranslator::StartupModule()
{
	HoudiniMassTranslatorInstance = this;

	FHoudiniEngine& HoudiniEngine = FHoudiniEngine::IsLoaded() ? FHoudiniEngine::Get() :
		FModuleManager::LoadModuleChecked<FHoudiniEngine>("HoudiniEngine");
	
	ComponentInputBuilder = MakeShared<FHoudiniZoneShapeComponentInputBuilder>();
	HoudiniEngine.RegisterInputBuilder(ComponentInputBuilder);

	OutputBuilder = MakeShared<FHoudiniZoneShapeOutputBuilder>();
	HoudiniEngine.RegisterOutputBuilder(OutputBuilder);

	FHoudiniMassCommands::Register();

	Commands = MakeShareable(new FUICommandList);

	Commands->MapAction(FHoudiniMassCommands::Get().CleanupLaneProfiles,
		FExecuteAction::CreateStatic(&FHoudiniMassCommands::OnCleanupLaneProfiles),
		FCanExecuteAction());

	Commands->MapAction(FHoudiniMassCommands::Get().RemoveAllLaneProfiles,
		FExecuteAction::CreateStatic(&FHoudiniMassCommands::OnRemoveAllLaneProfiles),
		FCanExecuteAction());

	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	if (UToolMenu* BuildMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Build"))
	{
		FToolMenuSection& Section = BuildMenu->FindOrAddSection("LevelEditorNavigation");
		Section.AddMenuEntryWithCommandList(FHoudiniMassCommands::Get().CleanupLaneProfiles, Commands);
		Section.AddMenuEntryWithCommandList(FHoudiniMassCommands::Get().RemoveAllLaneProfiles, Commands);
	}

	UE::ZoneGraphDelegates::OnZoneGraphDataBuildDone.AddRaw(this, &FHoudiniMassTranslator::OnZoneGraphBuildDone);
	FEditorDelegates::BeginPIE.AddRaw(this, &FHoudiniMassTranslator::OnZoneGraphBuildCancel);
}

void FHoudiniMassTranslator::OnZoneShapeOutputFinish()
{
	if (!Notification.IsValid())
	{
		FNotificationInfo Info(LOCTEXT("HoudiniZoneShapeOutputFinish", "Houdini Zone Shape Output Finished\nPlease Build Zone Graph"));
		Info.bFireAndForget = false;
		Info.FadeInDuration = 0.05f;
		Info.ExpireDuration = 0.2f;
		Info.FadeOutDuration = 0.3f;
		Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("HoudiniZoneGraphBuild", "Build Zone Graph"), FText::GetEmpty(),
			FSimpleDelegate::CreateLambda([]() { UE::ZoneGraphDelegates::OnZoneGraphRequestRebuild.Broadcast(); }), SNotificationItem::CS_None));
		Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("HoudiniZoneGraphBuildCancel", "Cancel"), FText::GetEmpty(),
			FSimpleDelegate::CreateLambda([]() { FHoudiniMassTranslator::Get().OnZoneGraphBuildCancel(false); }), SNotificationItem::CS_None));
		Info.bUseSuccessFailIcons = true;
		Notification = FSlateNotificationManager::Get().AddNotification(Info);
		//Notification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FHoudiniMassTranslator::OnZoneGraphBuildDone(const FZoneGraphBuildData&)
{
	if (Notification.IsValid())
	{
		Notification.Pin()->SetCompletionState(SNotificationItem::CS_Success);
		Notification.Pin()->ExpireAndFadeout();
		Notification.Reset();
	}
}

void FHoudiniMassTranslator::OnZoneGraphBuildCancel(const bool)
{
	if (Notification.IsValid())
	{
		Notification.Pin()->SetText(LOCTEXT("HoudiniZoneGraphBuildCanceled", "Houdini Zone Graph Build Canceled"));
		Notification.Pin()->SetCompletionState(SNotificationItem::CS_Fail);
		Notification.Pin()->SetExpireDuration(0.5f);
		Notification.Pin()->SetFadeOutDuration(1.0f);
		Notification.Pin()->ExpireAndFadeout();
		Notification.Reset();
	}
}

void FHoudiniMassTranslator::ShutdownModule()
{
	if (FHoudiniEngine::IsLoaded())
	{
		FHoudiniEngine::Get().UnregisterInputBuilder(ComponentInputBuilder);
		FHoudiniEngine::Get().UnregisterOutputBuilder(OutputBuilder);
	}

	UE::ZoneGraphDelegates::OnZoneGraphDataBuildDone.RemoveAll(this);
	FEditorDelegates::BeginPIE.RemoveAll(this);

	HoudiniMassTranslatorInstance = nullptr;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FHoudiniMassTranslator, HoudiniMassTranslator)