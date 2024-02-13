// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloMeshMaskProcessor.h"

#include "RenderGraphEvent.h"
#include "RenderGraphBuilder.h"
#include "PostProcess/PostProcessing.h"
#include "PrimitiveSceneProxy.h"
#include "RHI.h"
#include "ScenePrivate.h"
#if (!PLATFORM_ANDROID)
#include "MeshPassProcessor.inl"
#endif

// Set to true to render yellow overlay to visualize responsive AA mask.
static TAutoConsoleVariable<bool> CVarEnableHoloMeshResponsiveAADebug(
	TEXT("r.HoloMesh.ResponsiveAA.Debug"),
	false,
	TEXT("Renders the Responsive AA mask as a yellow overlay for debugging purposes."),
	ECVF_Default);

IMPLEMENT_MATERIAL_SHADER_TYPE(, FHoloMeshMask_VS, TEXT("/HoloMesh/HoloMeshMask_VS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FHoloMeshMask_PS, TEXT("/HoloMesh/HoloMeshMask_PS.usf"), TEXT("Main"), SF_Pixel);

FHoloMeshMaskProcessor::FHoloMeshMaskProcessor(const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
{
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());

	// From 5.1 onward ViewUniformBuffer is not used/needed.
#if !((ENGINE_MAJOR_VERSION >= 5) && (ENGINE_MINOR_VERSION >= 1))
	PassDrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);
#endif
}

void FHoloMeshMaskProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (MeshBatch.bUseForMaterial)
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);

		if (Material)
		{
			if (Material->GetRenderingThreadShaderMap())
			{
				const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);

				#if (ENGINE_MAJOR_VERSION >= 5) && (ENGINE_MINOR_VERSION >= 1)
				ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(*Material, OverrideSettings);
				ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(*Material, OverrideSettings);
				#else
				ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material, OverrideSettings);
				ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, *Material, OverrideSettings);
				#endif

				if (MeshBatch.bUseForMaterial
					&& (!PrimitiveSceneProxy || PrimitiveSceneProxy->ShouldRenderInMainPass()))
				{
					Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, MeshFillMode, MeshCullMode);
				}
			}
		}
	}
}

void FHoloMeshMaskProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
	FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();
	const FPrimitiveSceneInfo* RESTRICT PrimitiveSceneInfo = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetPrimitiveSceneInfo() : nullptr;

	if (VertexFactory == NULL || VertexFactoryType == NULL || PrimitiveSceneInfo == NULL)
	{
		return;
	}

#if ENGINE_MAJOR_VERSION == 5 ||(ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
	FMaterialShaderTypes ShaderTypes;
	ShaderTypes.AddShaderType<FHoloMeshMask_VS>();
	ShaderTypes.AddShaderType<FHoloMeshMask_PS>();
	if (!MaterialResource.HasShaders(ShaderTypes, VertexFactoryType))
	{
		// If materials are not compiled yet we can't render.
		return;
	}
#endif

	TMeshProcessorShaders<FHoloMeshMask_VS, FMeshMaterialShader, FMeshMaterialShader, FHoloMeshMask_PS> HoloMeshPassShaders;
	HoloMeshPassShaders.VertexShader = MaterialResource.GetShader<FHoloMeshMask_VS>(VertexFactory->GetType());
	HoloMeshPassShaders.PixelShader = MaterialResource.GetShader<FHoloMeshMask_PS>(VertexFactory->GetType());

	if (!HoloMeshPassShaders.VertexShader.IsValid() || !HoloMeshPassShaders.PixelShader.IsValid())
	{
		return;
	}

	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);
	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(HoloMeshPassShaders.VertexShader, HoloMeshPassShaders.PixelShader);

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);

	if ((BatchElementMask & (uint64)EHoloMeshMaskBatchFlags::ResponsiveAA) != 0)
	{
		// Responsive AA is a bit flag in the stencil buffer which excludes or reduces
		// pixels contributions to temporal anti aliasing methods. It's primarily used
		// for particles and hair and is useful for us to avoid artifacts.

		// Our current approach to utilize this is to draw the mesh again after the 
		// opaque pass with a custom mesh processor that writes the stencil value we need.
		// Color and depth writing are disabled. 
		// This may not be the most optimal path and should be explored more in the future.

		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual,
			true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
			false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
			0x00, STENCIL_TEMPORAL_RESPONSIVE_AA_MASK>::GetRHI());
		DrawRenderState.SetStencilRef(STENCIL_TEMPORAL_RESPONSIVE_AA_MASK);

		bool bDebugResponsiveAA = CVarEnableHoloMeshResponsiveAADebug.GetValueOnRenderThread();
		if (bDebugResponsiveAA)
		{
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RG, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI());
		}
		else
		{
			// This blend state disables all color writes
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI());
		}
	}
	else 
	{
		if ((BatchElementMask & (uint64)EHoloMeshMaskBatchFlags::DebugRed) != 0)
		{
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RED, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI());
		}

		if ((BatchElementMask & (uint64)EHoloMeshMaskBatchFlags::DebugGreen) != 0)
		{
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_GREEN, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI());
		}

		if ((BatchElementMask & (uint64)EHoloMeshMaskBatchFlags::DebugBlue) != 0)
		{
			DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_BLUE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI());
		}
	}

#if !PLATFORM_ANDROID
	const uint64 DefaultBatchElementMask = ~0ull;

	BuildMeshDrawCommands(
		MeshBatch,
		DefaultBatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		DrawRenderState,
		HoloMeshPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);
#endif
}