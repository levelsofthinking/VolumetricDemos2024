// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HoloSuitePlayerSettings.generated.h"

/**
 *
 */
UCLASS(config = HoloSuitePlayerSettings)
class HOLOSUITEPLAYER_API UHoloSuitePlayerSettings : public UObject
{
	GENERATED_BODY()
public:
	UHoloSuitePlayerSettings(const FObjectInitializer& obj);

	// -- OMS Global Settings --

	// Fast scrubbing will skip reading the frame number from the video and estimate the 
	// frame number based on current time in playback. This is inaccurate but very fast and stable.
	UPROPERTY(Config, EditAnywhere, Category = "OMS | Sequencer")
		bool FastScrubbingInEditor;

	// -- AVV Global Settings --

	// Controls whether or not decoding should be disabled when players are detected out of frustum.
	UPROPERTY(Config, EditAnywhere, Category = "AVV | Decoding", meta = (DisplayName = "Frustum Culling"))
		bool FrustumCulling;

	// Forces the player to deliver the requested frame in the same engine frame it is requested. This should only be
	// enabled when frame perfect timing is required such as movie rendering or low locked frame rate.
	// FrameUpdateLimit will have no effect when this is enabled.
	UPROPERTY(Config, EditAnywhere, Category = "AVV | Decoding", meta = (DisplayName = "Immediate Mode"))
		bool ImmediateMode;

	// Sets a limit on how many milliseconds per frame can be used to update AVV players in total.
	// Setting this to zero will disable any throttling. Has no effect in Immediate Mode.
	UPROPERTY(Config, EditAnywhere, Category = "AVV | Decoding", meta = (EditCondition = "!ImmediateMode", DisplayName = "Frame Update Limit"))
		float FrameUpdateLimit;

	UPROPERTY(Config, EditAnywhere, Category = "AVV | Rendering")
		bool MotionVectors;

	// -- Default Player Settings --

	// Default value of Responsive AA when a HoloSuitePlayer is spawned.
	UPROPERTY(Config, EditAnywhere, Category = "Default Settings | HoloSuitePlayer")
		bool ResponsiveAA;

	// Default value of Receive Decals when a HoloSuitePlayer is spawned.
	UPROPERTY(Config, EditAnywhere, Category = "Default Settings | HoloSuitePlayer")
		bool ReceiveDecals;

	// Default value of LOD 0 Screen Size when a HoloSuitePlayer is spawned.
	UPROPERTY(Config, EditAnywhere, Category = "Default Settings | AVV", meta = (DisplayName = "LOD 0 Screen Size"))
		float LOD0ScreenSize;

	// Default value of LOD 1 Screen Size when a HoloSuitePlayer is spawned.
	UPROPERTY(Config, EditAnywhere, Category = "Default Settings | AVV", meta = (DisplayName = "LOD 1 Screen Size"))
		float LOD1ScreenSize;

	// Default value of LOD 2 Screen Size when a HoloSuitePlayer is spawned.
	UPROPERTY(Config, EditAnywhere, Category = "Default Settings | AVV", meta = (DisplayName = "LOD 2 Screen Size"))
		float LOD2ScreenSize;

	// Default value of MaxBufferedSequences when a HoloSuitePlayer is spawned.
	UPROPERTY(Config, EditAnywhere, Category = "Default Settings | OMS", meta = (DisplayName = "Max Buffered Sequences"))
		int MaxBufferedSequences;
};
