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

class FDecodeFrameNumberCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FDecodeFrameNumberCS)
    SHADER_USE_PARAMETER_STRUCT(FDecodeFrameNumberCS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
    SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, InputTexture)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, FrameNumberBuffer)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
#if (ENGINE_MAJOR_VERSION >= 5) && (ENGINE_MINOR_VERSION >= 1)
        return true;
#else
        return RHISupportsComputeShaders(Parameters.Platform);
#endif
    }

    static inline void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(ECompilerFlags::CFLAG_AllowTypedUAVLoads);
    }
};
IMPLEMENT_GLOBAL_SHADER(FDecodeFrameNumberCS, "/HoloSuitePlayer/OMS/DecodeFrameNumberCS.usf", "MainCS", SF_Compute);
