// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloMeshComponent.h"
#include "HoloMeshMaskProcessor.h"
#include "HoloMeshBuffers.h"
#include "HoloMeshManager.h"

#include "Containers/ResourceArray.h"
#include "DynamicMeshBuilder.h"
#include "EngineGlobals.h"
#include "EngineModule.h"
#include "LocalVertexFactory.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PostProcess/SceneRenderTargets.h"
#include "RHI.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphEvent.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SceneManagement.h"
#include "SceneRendering.h"
#include "StaticMeshResources.h"
#include "VertexFactory.h"

#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "MeshPassProcessor.h"
#include "MeshPassProcessor.inl"

#include "HoloMeshModule.h"

DECLARE_CYCLE_STAT(TEXT("HoloMeshComponent Create Proxy"),	STAT_HoloMesh_CreateSceneProxy,		STATGROUP_HoloMesh);
DECLARE_CYCLE_STAT(TEXT("HoloMesh Draw Static"),			STAT_HoloMesh_DrawStatic,			STATGROUP_HoloMesh);
DECLARE_CYCLE_STAT(TEXT("HoloMesh Draw Dynamic"),			STAT_HoloMesh_DrawDynamic,			STATGROUP_HoloMesh);
DECLARE_CYCLE_STAT(TEXT("HoloMesh Render Mask"),			STAT_HoloMesh_RenderMask,			STATGROUP_HoloMesh);

void FHoloMesh::InitOrUpdate(ERHIFeatureLevel::Type InFeatureLevel)
{
	if (!bInitialized)
	{
		VertexFactory = new FHoloMeshVertexFactory(InFeatureLevel);
		bInitialized = true;
	}

	IndexBuffer->InitOrUpdate();
	VertexBuffers->InitOrUpdate(VertexFactory);
}

void FHoloMesh::Update()
{
	if (bInitialized)
	{
		VertexBuffers->UpdateData();
		IndexBuffer->UpdateData();
	}
}

void FHoloMesh::Update_RenderThread(FRHICommandListImmediate& RHICmdList, EHoloMeshUpdateFlags Flags)
{
	VertexBuffers->UpdateData_RenderThread(RHICmdList, Flags);

	if ((Flags & EHoloMeshUpdateFlags::Indices) != EHoloMeshUpdateFlags::None)
	{
		IndexBuffer->UpdateData_RenderThread(RHICmdList);
	}
}

void FHoloMesh::UpdateUniforms(float PreviousPositionWeight)
{
	if (!VertexFactory || VertexFactory->GetType() != &FHoloMeshVertexFactory::StaticType)
	{
		return;
	}

	FHoloMeshVertexFactory* HoloMeshVertexFactory = static_cast<FHoloMeshVertexFactory*>(VertexFactory);

	ENQUEUE_RENDER_COMMAND(HoloMeshUpdateUniformBuffer)(
		[HoloMeshVertexFactory, PreviousPositionWeight](FRHICommandListImmediate& RHICmdList)
		{
			FHoloMeshVertexFactoryParameters Parameters;
			Parameters.PreviousPositionBuffer = HoloMeshVertexFactory->GetPreSkinPositionSRV();
			Parameters.PreviousPositionWeight = PreviousPositionWeight;
			HoloMeshVertexFactory->HoloMeshUniformBuffer.UpdateUniformBufferImmediate(Parameters);
		});
}

void FHoloMesh::UpdateUniforms(FRDGBuilder& GraphBuilder, float PreviousPositionWeight)
{
	if (!VertexFactory || VertexFactory->GetType() != &FHoloMeshVertexFactory::StaticType)
	{
		return;
	}

	FHoloMeshVertexFactory* HoloMeshVertexFactory = static_cast<FHoloMeshVertexFactory*>(VertexFactory);

	GraphBuilder.AddPass(RDG_EVENT_NAME("UpdateHoloMeshUniforms"), ERDGPassFlags::None | ERDGPassFlags::NeverCull,
		[this, HoloMeshVertexFactory, PreviousPositionWeight](FRHICommandListImmediate& RHICmdList)
		{
			FHoloMeshVertexFactoryParameters Parameters;
			Parameters.PreviousPositionBuffer = HoloMeshVertexFactory->GetPreSkinPositionSRV();
			Parameters.PreviousPositionWeight = PreviousPositionWeight;
			HoloMeshVertexFactory->HoloMeshUniformBuffer.UpdateUniformBufferImmediate(Parameters);
		});
}

void FHoloMesh::UpdateFromSource(FHoloMesh* SourceHoloMesh)
{
	FHoloMeshVertexBuffers* oldVertexBuffers = nullptr;
	FHoloMeshIndexBuffer* oldIndexBuffer = nullptr;

	if (VertexBuffers != nullptr)
	{
		// If the number of vertices match then we can just swap the data structures
		// and run an update instead of allocating new GPU resources.
		if (SourceHoloMesh->VertexBuffers->GetNumVertices() == VertexBuffers->GetNumVertices())
		{
			VertexBuffers->SwapData(SourceHoloMesh->VertexBuffers);
			VertexBuffers->UpdateData();
		}
		else
		{
			oldVertexBuffers = VertexBuffers;
			VertexBuffers = SourceHoloMesh->VertexBuffers;
			SourceHoloMesh->VertexBuffers = nullptr;
		}
	}
	else
	{
		VertexBuffers = SourceHoloMesh->VertexBuffers;
		SourceHoloMesh->VertexBuffers = nullptr;
	}

	if (IndexBuffer != nullptr)
	{
		// If the number of indices match then we can just swap the data structures
		// and run an update instead of allocating new GPU resources.
		if (SourceHoloMesh->IndexBuffer->GetNumIndices() == IndexBuffer->GetNumIndices())
		{
			IndexBuffer->SwapData(SourceHoloMesh->IndexBuffer);
			IndexBuffer->UpdateData();
		}
		else
		{
			oldIndexBuffer = IndexBuffer;
			IndexBuffer = SourceHoloMesh->IndexBuffer;
			SourceHoloMesh->IndexBuffer = nullptr;
		}
	}
	else
	{
		IndexBuffer = SourceHoloMesh->IndexBuffer;
		SourceHoloMesh->IndexBuffer = nullptr;
	}

	// Delete old buffers.
	if (oldVertexBuffers != nullptr || oldIndexBuffer != nullptr)
	{
		ENQUEUE_RENDER_COMMAND(OMSPlayerFreeBuffers)(
		[oldVertexBuffers, oldIndexBuffer](FRHICommandListImmediate& RHICmdList)
		{
			if (oldVertexBuffers != nullptr)
			{
				oldVertexBuffers->ReleaseResource();
				delete oldVertexBuffers;
			}

			if (oldIndexBuffer != nullptr)
			{
				oldIndexBuffer->ReleaseResource();
				delete oldIndexBuffer;
			}
		});
	}
}

class FHoloMeshSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FHoloMeshSceneProxy(UHoloMeshComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, HoloMeshComponent(Component)
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
	{
		// Our motion vectors are controlled by the uniform value PreviousPositionWeight
		bAlwaysHasVelocity = true;

		HoloMesh = nullptr;
		FHoloMesh* SrcMesh = Component->GetHoloMesh();

		if (SrcMesh != nullptr
			&& SrcMesh->IndexBuffer->GetNumIndices() > 0
			&& SrcMesh->VertexBuffers->GetNumVertices() > 0)
		{
			// Initialize resources for rendering.
			SrcMesh->InitOrUpdate(GetScene().GetFeatureLevel());

			// Save ref to new mesh
			HoloMesh = SrcMesh;

			// HoloMesh specific LOD levels
			HoloMeshLOD = Component->GetHoloMeshLOD();

			// Responsive AA improves quality when rendering with temporal anti aliasing methods.
			// TAA/TSR do not run on mobile platforms so this feature is useless on those targets.
			ResponsiveAA = Component->GetResponsiveAAEnabled();
		}
	}

	virtual ~FHoloMeshSceneProxy()
	{
		if (HoloMeshComponent != nullptr)
		{
			HoloMeshComponent->RemoveSceneProxy(this);
		}
	}

	void OnOwnerDestroyed()
	{
		HoloMeshComponent = nullptr;
	}

	void PopulateMeshBatch(FMeshBatch& MeshBatch) const
	{
		int numTriangles = numTriangles = HoloMesh->IndexBuffer->GetNumIndices() / 3; 

		FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
		BatchElement.IndexBuffer = HoloMesh->IndexBuffer->GetIndexBufferRef();
		BatchElement.FirstIndex = 0;
		BatchElement.NumPrimitives = numTriangles;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = HoloMesh->VertexBuffers->GetNumVertices() - 1;
		BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
		MeshBatch.VertexFactory = HoloMesh->VertexFactory;
		MeshBatch.MaterialRenderProxy = HoloMesh->Material->GetRenderProxy();
		MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
		MeshBatch.bDisableBackfaceCulling = false;
		MeshBatch.CastShadow = true;
		MeshBatch.bUseForDepthPass = true;
		MeshBatch.bUseAsOccluder = false;
		MeshBatch.bUseForMaterial = true;
		MeshBatch.Type = PT_TriangleList;
		MeshBatch.DepthPriorityGroup = SDPG_World;
		MeshBatch.LODIndex = 0;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		MeshBatch.VisualizeLODIndex = HoloMeshLOD;
#endif
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if (HoloMesh == NULL || HoloMesh->VertexFactory == NULL)
		{
			return;
		}

		SCOPE_CYCLE_COUNTER(STAT_HoloMesh_DrawDynamic);

		// Vertex factory will not been initialized when the text string is empty or font is invalid.
		if (HoloMesh->VertexFactory->IsInitialized())
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				if (VisibilityMap & (1 << ViewIndex))
				{
					const FSceneView* View = Views[ViewIndex];

					// Draw the mesh.
					FMeshBatch& MeshBatch = Collector.AllocateMesh();
					PopulateMeshBatch(MeshBatch);
					Collector.AddMesh(ViewIndex, MeshBatch);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					RenderBounds(Collector.GetPDI(ViewIndex), View->Family->EngineShowFlags, GetBounds(), IsSelected());
#endif
				}
			}
		}
	}

	// Static Mesh Drawing
	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override
	{
		if (HoloMesh == NULL || HoloMesh->VertexFactory == NULL)
		{
			return;
		}

		SCOPE_CYCLE_COUNTER(STAT_HoloMesh_DrawStatic);

		if (HoloMesh->VertexFactory->IsInitialized())
		{
			PDI->ReserveMemoryForMeshes(1);

			// Draw the mesh.
			FMeshBatch MeshBatch;
			PopulateMeshBatch(MeshBatch);
			PDI->DrawMesh(MeshBatch, 1.0f);
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = IsShadowCast(View);

		// Default to static drawing
		Result.bStaticRelevance = true;
		Result.bDynamicRelevance = false;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		// Use dynamic drawing in editor.
		if (IsSelected())
		{
			Result.bStaticRelevance = false;
			Result.bDynamicRelevance = true;
		}
#endif

		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;
		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint(void) const
	{
		return(sizeof(*this) + GetAllocatedSize());
	}

	uint32 GetAllocatedSize(void) const
	{
		return(FPrimitiveSceneProxy::GetAllocatedSize());
	}

	void RenderMask(FPostOpaqueRenderParameters& Parameters)
	{
		if (HoloMesh == NULL || !HoloMesh->bInitialized)
		{
			return;
		}

		// Responsive AA is only applied for LOD 0.
		bool ApplyResponsiveAA = (ResponsiveAA && HoloMeshLOD < 1);
		if (!ApplyResponsiveAA)
		{
			return;
		}

		// Unless we're debugging something we don't need to render masks in editor worlds.
		const bool inEditor = !HoloMeshComponent->GetWorld() || !HoloMeshComponent->GetWorld()->IsGameWorld();
		if (inEditor)
		{
			return;
		}

		uint64 BatchElementMask = (uint64)EHoloMeshMaskBatchFlags::ResponsiveAA;

		FRHIUniformBuffer* UniformBufferRef = GetUniformBuffer();
		if (UniformBufferRef == nullptr)
		{
			return;
		}

		SCOPE_CYCLE_COUNTER(STAT_HoloMesh_RenderMask);

#if ENGINE_MAJOR_VERSION == 5
		FRDGBuilder& GraphBuilder = *Parameters.GraphBuilder;

		auto* PassParameters = GraphBuilder.AllocParameters<FHoloMeshShaderParameters>();
		PassParameters->View = Parameters.View->ViewUniformBuffer;
		PassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(Parameters.ColorTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(Parameters.DepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilWrite);

		FMeshBatch MeshBatch;
		PopulateMeshBatch(MeshBatch);

		FMeshBatchElement* BatchElement = &MeshBatch.Elements[0];
		BatchElement->PrimitiveUniformBuffer = UniformBufferRef;

		const FSceneView* SceneView = (FSceneView*)Parameters.View;
		const FIntRect ViewportRect = Parameters.ViewportRect;
		const FPrimitiveSceneProxy* HoloMeshProxy = this;

		#if ENGINE_MINOR_VERSION > 0

		AddDrawDynamicMeshPass(
			GraphBuilder,
			RDG_EVENT_NAME("HoloMesh.Mask"),
			PassParameters,
			*Parameters.View,
			ViewportRect,
			[MeshBatch, HoloMeshProxy, SceneView, BatchElementMask](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				FHoloMeshMaskProcessor MeshPassProcessor(SceneView->Family->Scene->GetRenderScene(), SceneView, DynamicMeshPassContext);
				MeshPassProcessor.AddMeshBatch(MeshBatch, BatchElementMask, HoloMeshProxy);
			}, true);

		#else

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HoloMesh.Mask"),
			PassParameters,
			ERDGPassFlags::Raster,
			[MeshBatch, SceneView, HoloMeshProxy, ViewportRect, BatchElementMask](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SetViewport(ViewportRect.Min.X, ViewportRect.Min.Y, 0.0f, ViewportRect.Max.X, ViewportRect.Max.Y, 1.0f);

				DrawDynamicMeshPass(*SceneView, RHICmdList,
					[SceneView, MeshBatch, HoloMeshProxy, BatchElementMask](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
					{
						FHoloMeshMaskProcessor MeshPassProcessor(SceneView->Family->Scene->GetRenderScene(), SceneView, DynamicMeshPassContext);
						MeshPassProcessor.AddMeshBatch(MeshBatch, BatchElementMask, HoloMeshProxy);
					});
			}
		);

		#endif
#else
		FRHICommandListImmediate& RHICmdList = *Parameters.RHICmdList;
		FRHIRenderPassInfo RPInfo(FSceneRenderTargets::Get(RHICmdList).GetSceneColor()->GetTargetableRHI(), ERenderTargetActions::Load_Store, Parameters.DepthTexture, EDepthStencilTargetActions::LoadDepthStencil_StoreStencilNotDepth);

		RHICmdList.BeginRenderPass(RPInfo, TEXT("HoloMesh.Mask"));
		{
			RHICmdList.SetViewport(0, 0, 0.0f, Parameters.ViewportRect.Width(), Parameters.ViewportRect.Height(), 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			FMeshBatch MeshBatch;
			PopulateMeshBatch(MeshBatch);
			FMeshBatchElement* BatchElement = &MeshBatch.Elements[0];
			BatchElement->PrimitiveUniformBuffer = UniformBufferRef;

			const FPrimitiveSceneProxy* HoloMeshProxy = this;
			const FSceneView* SceneView = (FSceneView*)Parameters.Uid;

			DrawDynamicMeshPass(*SceneView, RHICmdList,
				[MeshBatch, HoloMeshProxy, SceneView, BatchElementMask](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
				{
					FHoloMeshMaskProcessor MeshPassProcessor(SceneView->Family->Scene->GetRenderScene(), SceneView, DynamicMeshPassContext);
					MeshPassProcessor.AddMeshBatch(MeshBatch, BatchElementMask, HoloMeshProxy);
				});

			RHICmdList.EndRenderPass();
		}
#endif 
	}

	void SetHoloMeshLOD(int newLOD)
	{
		HoloMeshLOD = newLOD;
	}

private:
	UHoloMeshComponent* HoloMeshComponent;
	FHoloMesh* HoloMesh;
	int HoloMeshLOD = 0;
	FMaterialRelevance MaterialRelevance;

	// Responsive AA
	bool ResponsiveAA;
};

//////////////////////////////////////////////////////////////////////////


UHoloMeshComponent::UHoloMeshComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	bUseComplexAsSimpleCollision = true;

	HoloMeshLODScreenSizes = { 1.0f, 0.5f, 0.1f };
	HoloMeshForceLOD = -1;

	HoloMeshMaterial = nullptr;
	HoloMeshSkeleton = nullptr;
}

UHoloMeshComponent::~UHoloMeshComponent()
{
	FScopeLock Lock(&CriticalSection);

	for (int i = 0; i < SceneProxies.Num(); ++i)
	{
		SceneProxies[i]->OnOwnerDestroyed();
	}
	SceneProxies.Empty();
}

void UHoloMeshComponent::PostLoad()
{
	Super::PostLoad();

	if (ProcMeshBodySetup && IsTemplate())
	{
		ProcMeshBodySetup->SetFlags(RF_Public | RF_ArchetypeObject);
	}
}

FPrimitiveSceneProxy* UHoloMeshComponent::CreateSceneProxy()
{
	SCOPE_CYCLE_COUNTER(STAT_HoloMesh_CreateSceneProxy);

	FHoloMeshSceneProxy* newProxy = new FHoloMeshSceneProxy(this);

	FScopeLock Lock(&CriticalSection);
	SceneProxies.Add(newProxy);
	return newProxy;
}

void UHoloMeshComponent::RemoveSceneProxy(FPrimitiveSceneProxy* sceneProxy)
{
	FScopeLock Lock(&CriticalSection);
	TArray<FHoloMeshSceneProxy*> NewProxyList;
	for (int i = 0; i < SceneProxies.Num(); ++i)
	{
		if (SceneProxies[i] != sceneProxy)
		{
			NewProxyList.Add(SceneProxies[i]);
		}
	}
	SceneProxies = NewProxyList;
}

int32 UHoloMeshComponent::GetNumMaterials() const
{
	return (HoloMesh != nullptr ? 1 : 0);
}

UMaterialInterface* UHoloMeshComponent::GetMaterial(int32 ElementIndex) const
{
	return (UMaterialInterface*)HoloMesh[ReadIndex].Material;
}

FHoloMesh* UHoloMeshComponent::GetHoloMesh(bool write)
{
	return &HoloMesh[write ? WriteIndex : ReadIndex];
}

void UHoloMeshComponent::SwapHoloMesh()
{
	// Swap meshes.
	std::swap(ReadIndex, WriteIndex);

	// Swap materials.
	if (HoloMeshMaterial != nullptr)
	{
		HoloMeshMaterial->Swap();
	}

	DirtyHoloMesh();
}

void UHoloMeshComponent::DirtyHoloMesh()
{
	// Update bounds
	UpdateLocalBounds();

	// Mark render state dirty so the HoloMesh proxy will be recreated
	// with the new ReadIndex texture.
	MarkRenderStateDirty();
}

void UHoloMeshComponent::SetLODOptions(std::array<float, HOLOMESH_MAX_LODS> lodScreenSizes, int minimumLOD, int forceLOD)
{
	HoloMeshLODScreenSizes = lodScreenSizes;
	HoloMeshMinimumLOD = FMath::Clamp(minimumLOD, 0, HOLOMESH_MAX_LODS - 1);
	HoloMeshForceLOD = forceLOD;
}

int UHoloMeshComponent::ComputeHoloMeshLOD(FSceneView* SceneView)
{
	// If ForceLOD is valid we always return that instead of computing.
	if (HoloMeshForceLOD > -1)
	{
		return FMath::Clamp(HoloMeshForceLOD, HoloMeshMinimumLOD, HOLOMESH_MAX_LODS - 1);
	}

	// Note: this functions is a near duplicate of StaticMesh's LOD calculation.

	const FSceneView& LODView = GetLODView(*SceneView);
	const float ScreenRadiusSquared = ComputeBoundsScreenRadiusSquared(Bounds.Origin, Bounds.SphereRadius, LODView);
	
	// LODDistanceFactor is whats recommended to be used but it breaks LOD calculations 
	// when using a cinematic camera and 50mm field of view. 
	//const float ScreenSizeScale = LODView.LODDistanceFactor;
	const float ScreenSizeScale = 1.0f;

	// Walk backwards and return the first matching LOD
	for (int32 LODIndex = HOLOMESH_MAX_LODS - 1; LODIndex >= 0; --LODIndex)
	{
		float MeshScreenSize = HoloMeshLODScreenSizes[LODIndex] * ScreenSizeScale;

		if (FMath::Square(MeshScreenSize * 0.5f) > ScreenRadiusSquared)
		{
			return std::max(HoloMeshMinimumLOD, LODIndex);
		}
	}

	return HoloMeshMinimumLOD;
}

void UHoloMeshComponent::SetHoloMeshLOD(int newLOD)
{
	if (HoloMeshLOD == newLOD)
	{
		return;
	}

	HoloMeshLOD = newLOD;
	HoloMeshLODDirty = true;
}

void UHoloMeshComponent::SetHoloMeshSkeleton(USkeletalMeshComponent* skeletalMeshComponent)
{
	if (HoloMeshSkeleton != nullptr)
	{
		delete HoloMeshSkeleton;
		HoloMeshSkeleton = nullptr;
	}

	if (skeletalMeshComponent != nullptr)
	{
		HoloMeshSkeleton = new FHoloMeshSkeleton(skeletalMeshComponent);
	}
}

void UHoloMeshComponent::OnPostOpaqueRender(FPostOpaqueRenderParameters& Parameters)
{
	FSceneView* SceneView = nullptr;

#if ENGINE_MAJOR_VERSION == 5
	SceneView = (FSceneView*)Parameters.View;
#else
	SceneView = (FSceneView*)Parameters.Uid;
#endif

	if (SceneView != nullptr)
	{
		int computedLOD = ComputeHoloMeshLOD(SceneView);
		SetHoloMeshLOD(computedLOD);
	}

#if !PLATFORM_ANDROID
	FScopeLock Lock(&CriticalSection);
	for (int i = 0; i < SceneProxies.Num(); ++i)
	{
		SceneProxies[i]->RenderMask(Parameters);
	}
#endif
}

void UHoloMeshComponent::UpdateHoloMesh()
{
	UpdateLocalBounds(); // Update overall bounds
	UpdateCollision(); // Mark collision as dirty
	MarkRenderStateDirty(); // New section requires recreating scene proxy
}

void UHoloMeshComponent::Update_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest request)
{
}

void UHoloMeshComponent::EndFrame_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest request)
{
}

void UHoloMeshComponent::RequestCulled_RenderThread(FHoloMeshUpdateRequest request)
{
}

void UHoloMeshComponent::DoThreadedWork(int sequenceIndex, int frameIndex)
{
}

// -- Bounds --

void UHoloMeshComponent::UpdateLocalBounds()
{
	FBox LocalBox(ForceInit);

	bool validBounds = false;

	// Read Mesh
	LocalBox = HoloMesh[ReadIndex].LocalBox;
	if (LocalBox.IsValid && LocalBox.GetVolume() > 0.0f)
	{
		LocalBounds = FBoxSphereBounds(LocalBox);
		validBounds = true;
	}

	// Write Mesh
	if (!validBounds)
	{
		LocalBox = HoloMesh[WriteIndex].LocalBox;
		if (LocalBox.IsValid && LocalBox.GetVolume() > 0.0f)
		{
			LocalBounds = FBoxSphereBounds(LocalBox);
			validBounds = true;
		}
	}

	// If neither is valid then provide a reasonable default.
	if (!validBounds)
	{
		LocalBounds = FBoxSphereBounds(FVector(0.0f, 0.0f, 0.0f), FVector(12.5f, 12.5f, 100.0f), 25.0f);
	}

	// HACK: Bounding boxes are only for the keyframe.
	LocalBounds = LocalBounds.ExpandBy(25.0f);

	// Update global bounds
	UpdateBounds();
	// Need to send to render thread
	MarkRenderTransformDirty();
}

FBoxSphereBounds UHoloMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBoxSphereBounds Ret(LocalBounds.TransformBy(LocalToWorld));

	Ret.BoxExtent *= BoundsScale;
	Ret.SphereRadius *= BoundsScale;

	return Ret;
}

// -- Collision --

bool UHoloMeshComponent::GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData)
{
	int32 VertexBase = 0; // Base vertex index for current section

	// See if we should copy UVs
	bool bCopyUVs = UPhysicsSettings::Get()->bSupportUVFromHitResults; 
	if (bCopyUVs)
	{
		CollisionData->UVs.AddZeroed(1); // only one UV channel
	}

	FHoloMesh* Mesh = &HoloMesh[ReadIndex];

	auto PositionData = Mesh->VertexBuffers->GetPositionData();
	FPositionVertex* Positions = (FPositionVertex*)PositionData->GetDataPointer();

	auto TexCoordData = Mesh->VertexBuffers->GetTexCoordData();
	FVector2DHalf* TexCoords = (FVector2DHalf*)(TexCoordData->GetDataPointer());

	uint16* Indices = Mesh->IndexBuffer->GetIndexData16();

	// Do we have collision enabled?
	if (Mesh != nullptr && Mesh->bEnableCollision)
	{
		// Copy vert data
		for (uint32 VertIdx = 0; VertIdx < Mesh->VertexBuffers->GetNumVertices(); VertIdx++)
		{
			CollisionData->Vertices.Add(Positions[VertIdx].Position);

			// Copy UV if desired
			if (bCopyUVs)
			{
				CollisionData->UVs[0].Add(TexCoords[VertIdx]);
			}
		}

		// Copy triangle data
		const int32 NumTriangles = Mesh->IndexBuffer->GetNumIndices() / 3;
		for (int32 TriIdx = 0; TriIdx < NumTriangles; TriIdx++)
		{
			// Need to add base offset for indices
			FTriIndices Triangle;
			Triangle.v0 = Indices[(TriIdx * 3) + 0] + VertexBase;
			Triangle.v1 = Indices[(TriIdx * 3) + 1] + VertexBase;
			Triangle.v2 = Indices[(TriIdx * 3) + 2] + VertexBase;
			CollisionData->Indices.Add(Triangle);

			// Also store material info
			CollisionData->MaterialIndices.Add(0);
		}

		// Remember the base index that new verts will be added from in next section
		VertexBase = CollisionData->Vertices.Num();
	}

	CollisionData->bFlipNormals = true;
	CollisionData->bDeformableMesh = true;
	CollisionData->bFastCook = true;

	return true;
}

UBodySetup* UHoloMeshComponent::CreateBodySetupHelper()
{
	// The body setup in a template needs to be public since the property is Tnstanced and thus is the archetype of the instance meaning there is a direct reference
	UBodySetup* NewBodySetup = NewObject<UBodySetup>(this, NAME_None, (IsTemplate() ? RF_Public | RF_ArchetypeObject : RF_NoFlags));
	NewBodySetup->BodySetupGuid = FGuid::NewGuid();

	NewBodySetup->bGenerateMirroredCollision = false;
	NewBodySetup->bDoubleSidedGeometry = true;
	NewBodySetup->CollisionTraceFlag = bUseComplexAsSimpleCollision ? CTF_UseComplexAsSimple : CTF_UseDefault;

	return NewBodySetup;
}

void UHoloMeshComponent::CreateProcMeshBodySetup()
{
	if (ProcMeshBodySetup == nullptr)
	{
		ProcMeshBodySetup = CreateBodySetupHelper();
	}
}

void UHoloMeshComponent::UpdateCollision()
{
	//SCOPE_CYCLE_COUNTER(STAT_HoloMesh_UpdateCollision);

	UWorld* World = GetWorld();
	const bool bUseAsyncCook = World && World->IsGameWorld() && bUseAsyncCooking;

	if(bUseAsyncCook)
	{
		// Abort all previous ones still standing
		for (UBodySetup* OldBody : AsyncBodySetupQueue)
		{
			OldBody->AbortPhysicsMeshAsyncCreation();
		}

		AsyncBodySetupQueue.Add(CreateBodySetupHelper());
	}
	else
	{
		AsyncBodySetupQueue.Empty();	//If for some reason we modified the async at runtime, just clear any pending async body setups
		CreateProcMeshBodySetup();
	}
	
	UBodySetup* UseBodySetup = bUseAsyncCook ? AsyncBodySetupQueue.Last() : ProcMeshBodySetup;

	// Fill in simple collision convex elements
	UseBodySetup->AggGeom.ConvexElems = CollisionConvexElems;

	// Set trace flag
	UseBodySetup->CollisionTraceFlag = bUseComplexAsSimpleCollision ? CTF_UseComplexAsSimple : CTF_UseDefault;

	if(bUseAsyncCook)
	{
		UseBodySetup->CreatePhysicsMeshesAsync(FOnAsyncPhysicsCookFinished::CreateUObject(this, &UHoloMeshComponent::FinishPhysicsAsyncCook, UseBodySetup));
	}
	else
	{
		// New GUID as collision has changed
		UseBodySetup->BodySetupGuid = FGuid::NewGuid();
		// Also we want cooked data for this
		UseBodySetup->bHasCookedCollisionData = true;
		UseBodySetup->InvalidatePhysicsData();
		UseBodySetup->CreatePhysicsMeshes();
		RecreatePhysicsState();
	}
}

void UHoloMeshComponent::FinishPhysicsAsyncCook(bool bSuccess, UBodySetup* FinishedBodySetup)
{
	TArray<UBodySetup*> NewQueue;
	NewQueue.Reserve(AsyncBodySetupQueue.Num());

	int32 FoundIdx;
	if(AsyncBodySetupQueue.Find(FinishedBodySetup, FoundIdx))
	{
		if (bSuccess)
		{
			//The new body was found in the array meaning it's newer so use it
			ProcMeshBodySetup = FinishedBodySetup;
			RecreatePhysicsState();

			//remove any async body setups that were requested before this one
			for (int32 AsyncIdx = FoundIdx + 1; AsyncIdx < AsyncBodySetupQueue.Num(); ++AsyncIdx)
			{
				NewQueue.Add(AsyncBodySetupQueue[AsyncIdx]);
			}

			AsyncBodySetupQueue = NewQueue;
		}
		else
		{
			AsyncBodySetupQueue.RemoveAt(FoundIdx);
		}
	}
}

UBodySetup* UHoloMeshComponent::GetBodySetup()
{
	CreateProcMeshBodySetup();
	return ProcMeshBodySetup;
}

UMaterialInterface* UHoloMeshComponent::GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const
{
	UMaterialInterface* Result = nullptr;
	SectionIndex = 0;

	if (HoloMesh == nullptr)
	{
		return Result;
	}

	if (FaceIndex >= 0)
	{
		// Look for element that corresponds to the supplied face 
		int32 TotalFaceCount = 0;
		const int32 NumFaces = HoloMesh[ReadIndex].IndexBuffer->GetNumIndices() / 3;
		TotalFaceCount += NumFaces;

		if (FaceIndex < TotalFaceCount)
		{
			// Grab the material
			Result = GetMaterial(0);
			SectionIndex = 0;
			return Result;
		}
	}

	return Result;
}
