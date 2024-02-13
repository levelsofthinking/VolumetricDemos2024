#include "HoloMeshVertexFactory.h"
#include "LocalVertexFactory.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FHoloMeshVertexFactoryParameters, "HoloMeshParameters");

#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
void FHoloMeshVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	FLocalVertexFactory::InitRHI(RHICmdList);

	FHoloMeshVertexFactoryParameters Parameters;
	Parameters.PreviousPositionBuffer = GNullVertexBuffer.VertexBufferSRV;
	Parameters.PreviousPositionWeight = 0.0f;
	HoloMeshUniformBuffer = TUniformBufferRef<FHoloMeshVertexFactoryParameters>::CreateUniformBufferImmediate(Parameters, UniformBuffer_MultiFrame);

	check(IsValidRef(GetDeclaration()));
}
#else
void FHoloMeshVertexFactory::InitRHI()
{
	FLocalVertexFactory::InitRHI();

	FHoloMeshVertexFactoryParameters Parameters;
	Parameters.PreviousPositionBuffer = GNullVertexBuffer.VertexBufferSRV;
	Parameters.PreviousPositionWeight = 0.0f;
	HoloMeshUniformBuffer = TUniformBufferRef<FHoloMeshVertexFactoryParameters>::CreateUniformBufferImmediate(Parameters, UniformBuffer_MultiFrame);

	check(IsValidRef(GetDeclaration()));
}
#endif

void FHoloMeshVertexFactoryShaderParameters::Bind(const FShaderParameterMap& ParameterMap)
{
	// No special parameters to bind for now.
}

void FHoloMeshVertexFactoryShaderParameters::GetElementShaderBindings(
	const FSceneInterface* Scene,
	const FSceneView* View,
	const FMeshMaterialShader* Shader,
	const EVertexInputStreamType VertexStreamType,
	ERHIFeatureLevel::Type FeatureLevel,
	const FVertexFactory* VertexFactory,
	const FMeshBatchElement& BatchElement,
	class FMeshDrawSingleShaderBindings& ShaderBindings,
	FVertexInputStreamArray& VertexStreams) const
{
	// Decode VertexFactoryUserData as VertexFactoryUniformBuffer
	FRHIUniformBuffer* VertexFactoryUniformBuffer = static_cast<FRHIUniformBuffer*>(BatchElement.VertexFactoryUserData);

	// Ensure the vertex factory matches this parameter object and cast relevant objects
	check(VertexFactory->GetType() == &FHoloMeshVertexFactory::StaticType);
	const FHoloMeshVertexFactory* HoloMeshVertexFactory = static_cast<const FHoloMeshVertexFactory*>(VertexFactory);

	if (HoloMeshVertexFactory->SupportsManualVertexFetch(FeatureLevel) || UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel))
	{
		if (!VertexFactoryUniformBuffer)
		{
			// No batch element override
			VertexFactoryUniformBuffer = HoloMeshVertexFactory->GetUniformBuffer();
		}

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLocalVertexFactoryUniformShaderParameters>(), VertexFactoryUniformBuffer);
	}

	if (BatchElement.bUserDataIsColorVertexBuffer)
	{
		FColorVertexBuffer* OverrideColorVertexBuffer = (FColorVertexBuffer*)BatchElement.UserData;
		check(OverrideColorVertexBuffer);

		if (!HoloMeshVertexFactory->SupportsManualVertexFetch(FeatureLevel))
		{
			HoloMeshVertexFactory->GetColorOverrideStream(OverrideColorVertexBuffer, VertexStreams);
		}
	}

	ShaderBindings.Add(Shader->GetUniformBufferParameter<FHoloMeshVertexFactoryParameters>(), HoloMeshVertexFactory->GetHoloMeshUniformBuffer());
}

IMPLEMENT_TYPE_LAYOUT(FHoloMeshVertexFactoryShaderParameters);

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHoloMeshVertexFactory, SF_Vertex, FHoloMeshVertexFactoryShaderParameters);

#if (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)

IMPLEMENT_VERTEX_FACTORY_TYPE(FHoloMeshVertexFactory, "/HoloMesh/HoloMeshVertexFactoryUE427.ush", true, true, true, true, true);

#elif (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0)

IMPLEMENT_VERTEX_FACTORY_TYPE(FHoloMeshVertexFactory, "/HoloMesh/HoloMeshVertexFactoryUE500.ush",
	  EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsStaticLighting
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
	| EVertexFactoryFlags::SupportsPositionOnly
);

#else

IMPLEMENT_VERTEX_FACTORY_TYPE(FHoloMeshVertexFactory, "/HoloMesh/HoloMeshVertexFactory.ush",
	EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsStaticLighting
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
	| EVertexFactoryFlags::SupportsPositionOnly
);

#endif