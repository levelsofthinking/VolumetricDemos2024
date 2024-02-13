// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Runtime/Engine/Classes/Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "GlobalShader.h"
#include "ShaderCompilerCore.h"
#include "RenderGraphUtils.h"
#include "RHI.h"
#include "RHIDefinitions.h"

#include <utility>

#include "oms.h"
#include "HoloMeshComponent.h"
#include "HoloMeshSkeleton.h"
#include "HoloSuitePlayerModule.h"
#include "HoloSuitePlayerSettings.h"

class HOLOSUITEPLAYER_API OMSSkeleton
{
public:

	OMSSkeleton(USkeletalMeshComponent* skeletalMeshComponent);
	~OMSSkeleton();

	void Reset();

	// Updates positions and rotations of skeleton bones.
	bool UpdateSkeleton(oms_sequence_t* sequence, int currentFrame, int sequenceFrame);

	// Retargets Mesh based on updated skeleton. Assumes UpdateSkeleton() has been executed previously.
	void UpdateRetargetMesh(FHoloMesh* writeMesh);

protected:
	FHoloMeshSkeleton* HoloMeshSkeleton;

	int lastRetargetFrame;
	TArray<int> boneMap;
};