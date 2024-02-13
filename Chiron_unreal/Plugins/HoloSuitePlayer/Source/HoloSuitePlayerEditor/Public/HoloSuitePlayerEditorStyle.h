// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IPluginManager.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateImageBrush.h"

/**
 * Implements the visual style of the HoloSuite Player.
 */
class FHoloSuitePlayerEditorStyle final : public FSlateStyleSet
{
public:

	FHoloSuitePlayerEditorStyle() : FSlateStyleSet("HoloSuitePlayerEditorStyle")
	{
        SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
        SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

        // Note, these sizes are in Slate Units. Slate Units do NOT have to map to pixels.
        const FVector2D Icon16x16(16.0f, 16.0f);
        const FVector2D Icon64x64(64.0f, 64.0f);
        const FVector2D Icon128x128(128.0f, 128.0f);

        static FString IconsDir = IPluginManager::Get().FindPlugin(TEXT("HoloSuitePlayer"))->GetBaseDir() / TEXT("Resources/");

        // Asset icons

        Set("ClassIcon.OMSFile", new FSlateImageBrush(IconsDir + TEXT("HoloSuite_OMS_Icon128.png"), Icon16x16));
        Set("ClassThumbnail.OMSFile", new FSlateImageBrush(IconsDir + TEXT("HoloSuite_OMS_Icon128.png"), Icon64x64));

        Set("ClassIcon.AVVFile", new FSlateImageBrush(IconsDir + TEXT("HoloSuite_AVV_Icon128.png"), Icon16x16));
        Set("ClassThumbnail.AVVFile", new FSlateImageBrush(IconsDir + TEXT("HoloSuite_AVV_Icon128.png"), Icon64x64));

        // Actor icons

        Set("ClassIcon.HoloSuitePlayer", new FSlateImageBrush(IconsDir + TEXT("HoloSuite_Player_Icon16.png"), Icon16x16));
        Set("ClassThumbnail.HoloSuitePlayer", new FSlateImageBrush(IconsDir + TEXT("HoloSuite_Player_Icon128.png"), Icon128x128));

        // Sequencer icons

        Set("Sequencer.Tracks.VolumetricVideo", new FSlateImageBrush(IconsDir + TEXT("HoloSuite_Player_Icon16.png"), Icon16x16));

		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	virtual ~FHoloSuitePlayerEditorStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}

	static TSharedRef<FHoloSuitePlayerEditorStyle> Get()
	{
		if (!Singleton.IsValid())
		{
			Singleton = MakeShareable(new FHoloSuitePlayerEditorStyle);
		}
		return Singleton.ToSharedRef();
	}

private:

	static TSharedPtr<FHoloSuitePlayerEditorStyle> Singleton;
};
