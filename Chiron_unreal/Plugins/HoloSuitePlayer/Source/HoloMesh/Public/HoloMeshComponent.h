// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "Runtime/Engine/Classes/Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/ConvexElem.h"
#include "RendererInterface.h"
#include "UObject/ObjectMacros.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

#include <array>
#include <vector>

#include "HoloMeshModule.h"
#include "HoloMeshBuffers.h"
#include "HoloMeshManager.h"
#include "HoloMeshMaterial.h"
#include "HoloMeshVertexFactory.h"
#include "HoloMeshSkeleton.h"

#include "HoloMeshComponent.generated.h"

class FPrimitiveSceneProxy;
struct FHoloMesh;

#define HOLOMESH_MAX_LODS 3
#define HOLOMESH_BUFFER_COUNT 2

DECLARE_DELEGATE_TwoParams(FHoloMeshUpdateDelegate, FRHICommandListImmediate& RHICmdList, FHoloMesh* Mesh);

// FHoloMesh is the rendering representation of a volumetric mesh.
// Contains vertex buffers, textures, etc for rendering the mesh.
USTRUCT()
struct HOLOMESH_API FHoloMesh
{
	GENERATED_BODY()

	UPROPERTY()
	UMaterialInstanceDynamic* Material;

	FHoloMeshVertexBuffers* VertexBuffers;
	FHoloMeshIndexBuffer* IndexBuffer;
	FHoloMeshVertexFactory* VertexFactory;

	FHoloMeshDataTexture SSDRBoneTexture;
	FHoloMeshDataTexture RetargetBoneTexture;
	FHoloMeshRenderTarget LumaTexture;
	FHoloMeshRenderTarget MaskTexture;
	FHoloMeshTexture BC4Texture;

	bool bVisible;
	FBox LocalBox;
	bool bEnableCollision;
	bool bInitialized;

	FHoloMesh()
		: Material(nullptr)
		, VertexFactory(nullptr)
		, bVisible(true)
		, bEnableCollision(false)
		, bInitialized(false)
	{
		VertexBuffers = new FHoloMeshVertexBuffers();
		IndexBuffer = new FHoloMeshIndexBuffer();
	}

	~FHoloMesh()
	{
		if (!IsInRenderingThread())
		{
			ENQUEUE_RENDER_COMMAND(FHoloMeshRelease)(
				[VertexBuffers = VertexBuffers, IndexBuffer = IndexBuffer, VertexFactory = VertexFactory](FRHICommandListImmediate& RHICmdList)
				{
					if (VertexBuffers != nullptr)
					{
						VertexBuffers->ReleaseResource();
						delete VertexBuffers;
					}

					if (IndexBuffer != nullptr)
					{
						IndexBuffer->ReleaseResource();
						delete IndexBuffer;
					}

					if (VertexFactory != nullptr)
					{
						VertexFactory->ReleaseResource();
						delete VertexFactory;
					}
				});

			VertexBuffers = nullptr;
			IndexBuffer = nullptr;
			VertexFactory = nullptr;
			return;
		}

		if (VertexBuffers != nullptr)
		{
			VertexBuffers->ReleaseResource();
			delete VertexBuffers;
		}

		if (IndexBuffer != nullptr)
		{
			IndexBuffer->ReleaseResource();
			delete IndexBuffer;
		}

		if (VertexFactory != nullptr)
		{
			VertexFactory->ReleaseResource();
			delete VertexFactory;
		}

		VertexBuffers = nullptr;
		IndexBuffer = nullptr;
		VertexFactory = nullptr;
	}

	bool IsInitialized()
	{
		return bInitialized &&
			(VertexBuffers != nullptr && VertexBuffers->IsInitialized()) &&
			(IndexBuffer != nullptr && IndexBuffer->IsInitialized());
	}

	void InitOrUpdate(ERHIFeatureLevel::Type InFeatureLevel);
	void Update();
	void Update_RenderThread(FRHICommandListImmediate& RHICmdList, EHoloMeshUpdateFlags Flags = EHoloMeshUpdateFlags::All);

	void UpdateUniforms(float PreviousPositionWeight); 
	void UpdateUniforms(FRDGBuilder& GraphBuilder, float PreviousPositionWeight);

	// If source vertex or index counts match only CPU side structures will be taken.
	// If they do not match the vertex and index buffer objects will be taken.
	// Data is not copied so it will be nulled in the provided Source.
	void UpdateFromSource(FHoloMesh* SourceHoloMesh);
};

class FHoloMeshSceneProxy;

// Component used for rendering Arcturus volumetric mesh data.
UCLASS()
class HOLOMESH_API UHoloMeshComponent : public UMeshComponent, public IInterface_CollisionDataProvider
{
	GENERATED_BODY()
public:

	UHoloMeshComponent(const FObjectInitializer& ObjectInitializer);
	~UHoloMeshComponent();

	virtual FHoloMesh* GetHoloMesh(bool write = false);
	virtual FHoloMesh* GetHoloMesh(int index) { return &HoloMesh[index]; }
	
	// Swap read and write indexes and mark mesh data as dirty.
	void SwapHoloMesh();

	// Mark mesh data as dirty so the render proxy will be recreated. Also recomputes bounds.
	void DirtyHoloMesh();

	// Updates HoloMesh representation including Physics. 
	void UpdateHoloMesh();

	// Will be populated by HoloMeshManager if this mesh is registered with it.
	FGuid RegisteredGUID;

	// Called by HoloMeshManager with a request to update rendering
	virtual void Update_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest request);
	virtual void EndFrame_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest request);
	virtual void RequestCulled_RenderThread(FHoloMeshUpdateRequest request);

	//~ Begin Interface_CollisionDataProvider Interface
	virtual bool GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData) override;
	virtual bool ContainsPhysicsTriMeshData(bool InUseAllTriData) const override { return false; }
	virtual bool WantsNegXTriMesh() override { return false; }
	//~ End Interface_CollisionDataProvider Interface

	/** 
	 *	Controls whether the complex (Per poly) geometry should be treated as 'simple' collision. 
	 *	Should be set to false if this component is going to be given simple collision and simulated.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="HoloSuite Mesh Component")
	bool bUseComplexAsSimpleCollision;

	/**
	*	Controls whether the physics cooking should be done off the game thread. This should be used when collision geometry doesn't have to be immediately up to date (For example streaming in far away objects)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HoloSuite Mesh Component")
	bool bUseAsyncCooking;

	/** Collision data */
	UPROPERTY(Instanced)
	class UBodySetup* ProcMeshBodySetup;

	UHoloMeshMaterial* GetHoloMaterial() { return HoloMeshMaterial; }

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual class UBodySetup* GetBodySetup() override;
	virtual UMaterialInterface* GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual int32 GetNumMaterials() const override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	//~ End UMeshComponent Interface.

	//~ Begin UObject Interface
	virtual void PostLoad() override;
	//~ End UObject Interface.

	// Rendering options.
	void SetRenderingOptions(bool _motionVectors, bool _responsiveAA, bool _receiveDecals)
	{	
		MotionVectors = _motionVectors;
		ResponsiveAA = _responsiveAA;
		bReceivesDecals = _receiveDecals;
	}

	const bool GetMotionVectorsEnabled() { return MotionVectors; }
	const bool GetResponsiveAAEnabled() { return ResponsiveAA; }

	// Configure Level of Detail (LOD) for this HoloMesh
	void SetLODOptions(std::array<float, HOLOMESH_MAX_LODS> lodScreenSizes, int minimumLOD = 0, int forceLOD = -1);
	int GetHoloMeshLOD() { return HoloMeshLOD; }
	void SetHoloMeshLOD(int newLOD);

	// Sets the SkeletalMeshComponent who's skeleton will be controlled by the skeleton data from the source AVV file.
	void SetHoloMeshSkeleton(USkeletalMeshComponent* skeletalMeshComponent);

	// Used for tracking/statistics purposes
	int GetContentFrame() { return ContentFrame; }
	void SetContentFrame(int frame) { ContentFrame = frame; }

	void RemoveSceneProxy(FPrimitiveSceneProxy* sceneProxy);

	void OnPostOpaqueRender(FPostOpaqueRenderParameters& Parameters);

	// Called by the manager to flush out any excess memory usage.
	virtual void FreeUnusedMemory() {}

	// Executed via a thread from HoloMeshManager's pool.
	virtual void DoThreadedWork(int sequenceIndex, int frameIndex);

private:
	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.

	/** Update LocalBounds member from the local box of each section */
	void UpdateLocalBounds();
	/** Ensure ProcMeshBodySetup is allocated and configured */
	void CreateProcMeshBodySetup();
	/** Mark collision data as dirty, and re-create on instance if necessary */
	void UpdateCollision();
	/** Once async physics cook is done, create needed state */
	void FinishPhysicsAsyncCook(bool bSuccess, UBodySetup* FinishedBodySetup);

	/** Helper to create new body setup objects */
	UBodySetup* CreateBodySetupHelper();

	/** Convex shapes used for simple collision */
	UPROPERTY()
	TArray<FKConvexElem> CollisionConvexElems;

	/** Local space bounds of mesh */
	UPROPERTY()
	FBoxSphereBounds LocalBounds;
	
	/** Queue for async body setups that are being cooked */
	UPROPERTY(transient)
	TArray<class UBodySetup*> AsyncBodySetupQueue;

protected:
	// Double buffered HoloMesh data
	FHoloMesh HoloMesh[HOLOMESH_BUFFER_COUNT];
	FHoloMeshSkeleton* HoloMeshSkeleton;

	UPROPERTY()
	UHoloMeshMaterial* HoloMeshMaterial;

	int ReadIndex = 0;
	int WriteIndex = 1;
	int ContentFrame = -1;

	// Rendering Options
	bool MotionVectors = false;
	bool ResponsiveAA = false;

	// Level of Detail
	int HoloMeshLOD = 0;
	std::array<float, HOLOMESH_MAX_LODS> HoloMeshLODScreenSizes;
	int HoloMeshMinimumLOD = 0;
	int HoloMeshForceLOD = -1;
	bool HoloMeshLODDirty = false;

	// Computes current LOD level given the above settings and a provided scene view.
	int ComputeHoloMeshLOD(FSceneView* SceneView);

	TArray<FHoloMeshSceneProxy*> SceneProxies;
	mutable FCriticalSection CriticalSection;

	friend class FHoloMeshSceneProxy;
};
