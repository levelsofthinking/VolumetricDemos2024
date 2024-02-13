// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Runtime/Engine/Classes/Materials/MaterialInstanceDynamic.h"
#include "RendererInterface.h"
#include "MeshMaterialShader.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include "Runtime/Launch/Resources/Version.h"

#include "HoloMeshModule.h"
#include "HoloMeshMaterial.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FHoloMeshVertexFactoryParameters, )
	SHADER_PARAMETER_SRV(Buffer<float>, PreviousPositionBuffer)
	SHADER_PARAMETER(float, PreviousPositionWeight)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

/**
* HoloMesh vertex factory
*/
class FHoloMeshVertexFactory : public FLocalVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FHoloMeshVertexFactory);

public:

	FHoloMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
		: FLocalVertexFactory(InFeatureLevel, "FHoloMeshVertexFactory")
	{
	}

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLocalVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
#else
	virtual void InitRHI() override;
#endif

	// Contains HoloMesh specific features to be passed into the vertex factory.
	TUniformBufferRef<FHoloMeshVertexFactoryParameters> HoloMeshUniformBuffer;

	FRHIUniformBuffer* GetHoloMeshUniformBuffer() const
	{
		return HoloMeshUniformBuffer.GetReference();
	}
};

/**
* HoloMesh vertex factory shader parameters.
*/
class FHoloMeshVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FHoloMeshVertexFactoryShaderParameters, NonVirtual);

public:
	void Bind(const FShaderParameterMap& ParameterMap);
	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType VertexStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const;
};
