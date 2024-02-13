// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Runtime/Engine/Classes/Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GlobalShader.h"
#include "ShaderCompilerCore.h"
#include "RenderGraphUtils.h"
#include "RHI.h"
#include "RHIDefinitions.h"

// UE4/UE5 compatibility macros.
#if (ENGINE_MAJOR_VERSION == 5)
#define OMS_GET_RESOURCE(X) X->GetResource()
#define OMS_GET_TEXREF(TEX) (TEX->GetResource() && TEX->GetResource()->TextureRHI) ? TEX->GetResource()->TextureRHI->GetTexture2D() : nullptr;
#else
#define OMS_GET_RESOURCE(X) X->Resource
#define OMS_GET_TEXREF(TEX) (TEX->Resource && TEX->Resource->TextureRHI) ? TEX->Resource->TextureRHI->GetTexture2D() : nullptr;
#endif

class HOLOSUITEPLAYER_API OMSUtilities
{
public:
	static int DecodeBinaryPixels(unsigned char* pixelBlock);

	// Attempts to find a media player texture inside the supplied material.
	static UTexture* GetMediaPlayerTexture(UMaterialInterface* sourceMaterial);

	static bool IsMobileHDREnabled();
};