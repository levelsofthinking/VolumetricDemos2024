// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloSuitePlayerSettings.h"
#include "OMS/OMSFile.h"
#include "Misc/Paths.h"

UHoloSuitePlayerSettings::UHoloSuitePlayerSettings(const FObjectInitializer& obj)
{
	// -- OMS Settings --

	FastScrubbingInEditor = false;

	// -- AVV Settings --

	FrameUpdateLimit = 3.0f;
	FrustumCulling   = true;
	ImmediateMode    = false;
	MotionVectors    = true;

	// -- Default Settings --

	// Default Shared
	ResponsiveAA   = false;
	ReceiveDecals  = true;

	// Default AVV
	LOD0ScreenSize = 1.0f;
	LOD1ScreenSize = 0.5f;
	LOD2ScreenSize = 0.25f;

	// Default OMS
	MaxBufferedSequences = 20;
}