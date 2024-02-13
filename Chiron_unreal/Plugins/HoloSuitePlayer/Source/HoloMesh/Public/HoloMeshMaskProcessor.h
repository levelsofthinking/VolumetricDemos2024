// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "Runtime/Launch/Resources/Version.h"
#include "ShaderParameterStruct.h"
#include "SceneView.h"
#include "MeshMaterialShader.h"
#include "MeshPassProcessor.h"

#if ENGINE_MAJOR_VERSION == 5
#include "InstanceCulling/InstanceCullingContext.h"
#endif

#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
const int32 UE_510_OR_NEWER = 1;
#else
const int32 UE_510_OR_NEWER = 0;
#endif

BEGIN_SHADER_PARAMETER_STRUCT(FHoloMeshShaderParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_REF(FSceneTextureUniformParameters, SceneTextures)

	#if ENGINE_MAJOR_VERSION == 5
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FInstanceCullingGlobalUniforms, InstanceCulling)
	#endif

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

enum class EHoloMeshMaskBatchFlags
{
	ResponsiveAA	= (1u << 1),
	DebugRed		= (1u << 2),
	DebugGreen		= (1u << 3),
	DebugBlue		= (1u << 4),
};

class FHoloMeshMask_VS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FHoloMeshMask_VS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FMeshMaterialShader::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_510_OR_NEWER"), UE_510_OR_NEWER);
	}

	FHoloMeshMask_VS() = default;
	FHoloMeshMask_VS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}
};

class FHoloMeshMask_PS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FHoloMeshMask_PS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FMeshMaterialShader::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	FHoloMeshMask_PS() = default;
	FHoloMeshMask_PS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}
};

class FHoloMeshMaskProcessor : public FMeshPassProcessor
{
public:
	FHoloMeshMaskProcessor(const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		FMeshPassDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	FMeshPassProcessorRenderState PassDrawRenderState;
};