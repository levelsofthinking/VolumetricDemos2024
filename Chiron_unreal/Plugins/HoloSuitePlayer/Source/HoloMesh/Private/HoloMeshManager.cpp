// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloMeshManager.h"
#include "HoloMeshComponent.h"
#include "HoloMeshModule.h"
#include "HoloMeshUtilities.h"

#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Math/UnitConversion.h"
#include "RenderGraphBuilder.h"

TGlobalResource<HoloMeshManager> GHoloMeshManager;

DECLARE_CYCLE_STAT(TEXT("HoloMesh Manager Execute"),		STAT_HoloMesh_Manager_Execute,		STATGROUP_HoloMesh);
DECLARE_CYCLE_STAT(TEXT("HoloMesh Manager Update LODs"),	STAT_HoloMesh_Manager_UpdateStats,	STATGROUP_HoloMesh);

#define HOLOMESH_MANAGER_DEBUG 0

// Set to true to display real time HoloMesh stats
static TAutoConsoleVariable<bool> CVarEnableHoloMeshStats(
	TEXT("r.HoloMesh.Stats"),
	false,
	TEXT("Displays render statistics for HoloMeshes."),
	ECVF_Default);

HoloMeshManager::HoloMeshManager()
	: MemoryPool(nullptr), ThreadPool(nullptr)
{
    bInitialized = false;
	queuePosition = 0;

	managerStats.fpsStartTime = FPlatformTime::Seconds();
}

void HoloMeshManager::Initialize()
{
	if (!bInitialized)
	{
		bInitialized = true;
		
#if ENGINE_MAJOR_VERSION == 5
		bUseTickUpdates = false;
#else
		bUseTickUpdates = true;
#endif

		// Allocate thread pool.
		ThreadPool = FQueuedThreadPool::Allocate();
		int32 NumThreadsInThreadPool = FPlatformMisc::NumberOfWorkerThreadsToSpawn();
		verify(ThreadPool->Create(NumThreadsInThreadPool, 32768U, TPri_Normal, TEXT("HoloMeshThreadPool")));

		// Allocate memory pools
		MemoryPool = new FHoloMemoryPool();
		WorkRequestPool = new TReusableObjectPool<FHoloMeshWorkRequest, 16368>();

		BeginRendering();
	}
}

void HoloMeshManager::BeginRendering()
{
	if (PostOpaqueRenderHandle.IsValid())
	{
		return;
	}

	// Post Opaque is used for updating stats and triggering mask renders such as ResponsiveAA.
	const FName RendererModuleName("Renderer");
	IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
	if (RendererModule)
	{
		PostOpaqueRenderHandle = RendererModule->RegisterPostOpaqueRenderDelegate(FPostOpaqueRenderDelegate::CreateRaw(this, &HoloMeshManager::OnPostOpaque_RenderThread));
	}

	// SceneViewExtension is used to trigger HoloMesh updating.
	PostProcessSceneViewExtension = FSceneViewExtensions::NewExtension<FHoloMeshSceneViewExtension>();

#if ENGINE_MAJOR_VERSION == 5
	static bool bInit = false;
	if (!bInit)
	{
		GEngine->GetPreRenderDelegateEx().AddRaw(this, &HoloMeshManager::BeginFrame);
		GEngine->GetPostRenderDelegateEx().AddRaw(this, &HoloMeshManager::EndFrame);

		bInit = true;
	}
#endif
}

void HoloMeshManager::EndRendering()
{
	if (!PostOpaqueRenderHandle.IsValid())
	{
		return;
	}

	const FName RendererModuleName("Renderer");
	IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
	if (RendererModule)
	{
		RendererModule->RemovePostOpaqueRenderDelegate(PostOpaqueRenderHandle);
	}

	PostOpaqueRenderHandle.Reset();
}

void HoloMeshManager::BeginPIE()
{
	bPlayingInEditor = true;

	// Returns any unused cached data from editor instances to the memory pool
	// so that the newly spawned players can use it.
	FreeUnusedMemory();
}

void HoloMeshManager::EndPIE()
{
	bPlayingInEditor = false;
}

FGuid HoloMeshManager::Register(UHoloMeshComponent* component, AActor* owner)
{
    if (!bInitialized)
    {
        Initialize();
    }

	bool editorMesh = false;
	if (owner != nullptr && owner->GetWorld() != NULL)
	{
		int worldType = owner->GetWorld()->WorldType;
		if (worldType == 2 || worldType == 4 || worldType == 7)
		{
			editorMesh = true;
		}
	}
	else
	{
		editorMesh = true;
	}

	FScopeLock Lock(&CriticalSection);

	for (auto& kv : RegisteredMeshes)
	{
		FRegisteredHoloMesh& item = kv.Value;
		if (item.component == component && item.owner == owner)
		{
			return kv.Key;
		}
	}

	managerStats.lastBreakTime = 0;

	FRegisteredHoloMesh newEntry;
	newEntry.component = component;
	newEntry.owner = owner;
	newEntry.visible = true;
	newEntry.LOD = 0;
	newEntry.editorMesh = editorMesh;

	FGuid newGUID = FGuid::NewGuid();
	component->RegisteredGUID = newGUID;
	RegisteredMeshes.Add(newGUID, newEntry);

#if HOLOMESH_MANAGER_DEBUG
	UE_LOG(LogHoloMesh, Display, TEXT("Registered HoloMesh: %s (Editor: %d) (Total: %d)"), *newGUID.ToString(), newEntry.editorMesh, RegisteredMeshes.Num());
#endif
	return newGUID;
}

FRegisteredHoloMesh* HoloMeshManager::GetRegisteredMesh(FGuid registeredGUID)
{
	if (!bInitialized)
	{
		return nullptr;
	}

	FScopeLock Lock(&CriticalSection);

	if (RegisteredMeshes.Contains(registeredGUID))
	{
		return &RegisteredMeshes[registeredGUID];
	}

	return nullptr;
}

void HoloMeshManager::Unregister(FGuid registeredGUID)
{
	if (!bInitialized)
	{
		return;
	}

	FScopeLock Lock(&CriticalSection);

	if (RegisteredMeshes.Contains(registeredGUID))
	{
		ClearRequests(registeredGUID);
		RegisteredMeshes.Remove(registeredGUID);

#if HOLOMESH_MANAGER_DEBUG
		UE_LOG(LogHoloMesh, Display, TEXT("Unregistered HoloMesh: %s (Total: %d)"), *registeredGUID.ToString(), RegisteredMeshes.Num());
#endif
	}
}

FHoloMemoryBlockRef HoloMeshManager::AllocBlock(SIZE_T SizeInBytes)
{
	if (MemoryPool == nullptr)
	{
		return nullptr;
	}

#if HOLOMESH_MANAGER_DEBUG
	double executeStart = FPlatformTime::Seconds() * 1000.0;
#endif

	FHoloMemoryBlockRef Block = MemoryPool->Allocate(SizeInBytes);

#if HOLOMESH_MANAGER_DEBUG
	double executeTime = (FPlatformTime::Seconds() * 1000.0) - executeStart;
	if (executeTime > 0.1)
	{
		UE_LOG(LogHoloMesh, Display, TEXT("AllocBlock Took: %f ms"), executeTime);
	}
#endif
	
	AddContainerBytes(Block->Size);
	return Block;
}

void HoloMeshManager::FreeBlock(FHoloMemoryBlockRef Block)
{
	if (MemoryPool == nullptr || !Block.IsValid())
	{
		return;
	}

	RemoveContainerBytes(Block->Size);

#if HOLOMESH_MANAGER_DEBUG
	double executeStart = FPlatformTime::Seconds() * 1000.0;
#endif

	MemoryPool->Deallocate(Block);

#if HOLOMESH_MANAGER_DEBUG
	double executeTime = (FPlatformTime::Seconds() * 1000.0) - executeStart;
	if (executeTime > 0.1)
	{
		UE_LOG(LogHoloMesh, Display, TEXT("FreeBlockTook: %f ms"), executeTime);
	}
#endif
}

void HoloMeshManager::FreeUnusedMemory()
{
	FScopeLock Lock(&CriticalSection);

	for (auto& kv : RegisteredMeshes)
	{
		FRegisteredHoloMesh& item = kv.Value;

		if (item.editorMesh && item.IsValid())
		{
			item.component->FreeUnusedMemory();
		}
	}

	if (MemoryPool != nullptr)
	{
		MemoryPool->Empty();
	}
}

void HoloMeshManager::AddUpdateRequest(FGuid holoMeshGUID, int holoMeshIndex, int segmentIndex, int frameIndex)
{
	if (!holoMeshGUID.IsValid())
	{
		UE_LOG(LogHoloMesh, Error, TEXT("Rejecting update request for invalid GUID: %s on frame %d."), *holoMeshGUID.ToString(), GFrameNumber);
		return;
	}

	FScopeLock Lock(&CriticalSection);

	// This command is called from game thread, render thread will bump the frame number
	// by one when the frame starts.
	uint32 requestedFrameNumber = GFrameNumber;

	for (auto& QueueItem : UpdateRequestQueue)
	{
		if (bImmediateMode)
		{
			// In immediate mode we only overwrite if the request is for the same frame number.
			if (QueueItem.RegisteredGUID == holoMeshGUID && QueueItem.RequestedEngineFrame == requestedFrameNumber)
			{
				QueueItem.HoloMeshIndex = holoMeshIndex;
				QueueItem.SegmentIndex = segmentIndex;
				QueueItem.FrameIndex = frameIndex;
				return;
			}
		}
		else 
		{
			if (QueueItem.RegisteredGUID == holoMeshGUID)
			{
				QueueItem.HoloMeshIndex = holoMeshIndex;
				QueueItem.SegmentIndex = segmentIndex;
				QueueItem.FrameIndex = frameIndex;
				return;
			}
		}
	}

	FHoloMeshUpdateRequest request;
	request.RegisteredGUID = holoMeshGUID;
	request.HoloMeshIndex = holoMeshIndex;
	request.SegmentIndex = segmentIndex;
	request.FrameIndex = frameIndex;
	request.RequestedEngineFrame = requestedFrameNumber;
	UpdateRequestQueue.Add(request);
}

void HoloMeshManager::AddWorkRequest(FGuid holoMeshGUID, int segmentIndex, int frameIndex)
{
	if (!bInitialized)
	{
		UE_LOG(LogHoloMesh, Error, TEXT("Rejecting work request before HoloMeshManager is initialized: %s"), *holoMeshGUID.ToString());
		return;
	}

	if (!holoMeshGUID.IsValid())
	{
		UE_LOG(LogHoloMesh, Error, TEXT("Rejecting work request for invalid GUID: %s on frame %d."), *holoMeshGUID.ToString(), GFrameNumber);
		return;
	}

	FScopeLock Lock(&CriticalSection);

	if (!RegisteredMeshes.Contains(holoMeshGUID))
	{
		UE_LOG(LogHoloMesh, Error, TEXT("Rejecting work request for invalid GUID: %s on frame %d."), *holoMeshGUID.ToString(), GFrameNumber);
		return;
	}

	FRegisteredHoloMesh& item = RegisteredMeshes[holoMeshGUID];
	if (!item.IsValid())
	{
		return;
	}

	FHoloMeshWorkRequest* HoloMeshWork = WorkRequestPool->Next();
	HoloMeshWork->RegisteredGUID = holoMeshGUID;
	HoloMeshWork->SegmentIndex = segmentIndex;
	HoloMeshWork->FrameIndex = frameIndex;
	ThreadPool->AddQueuedWork(HoloMeshWork);
}

void HoloMeshManager::FinishWorkRequest(FHoloMeshWorkRequest* request)
{
	if (WorkRequestPool != nullptr)
	{
		WorkRequestPool->Return(request);
	}
}

void HoloMeshManager::ClearRequests(FGuid holoMeshGUID)
{
	if (!bInitialized)
	{
		return;
	}

	FScopeLock Lock(&CriticalSection);

	TArray<FHoloMeshUpdateRequest> ItemsToKeep;
	for (auto& QueueItem : UpdateRequestQueue)
	{
		if (QueueItem.RegisteredGUID != holoMeshGUID)
		{
			ItemsToKeep.Add(QueueItem);
		}
	}
	UpdateRequestQueue = ItemsToKeep;
}

void HoloMeshManager::ProcessRequests(FRDGBuilder& GraphBuilder)
{
	FScopeLock Lock(&CriticalSection);

	// Update frame number
	if (UpdateRequestQueue.Num() == 0)
	{
		return;
	}

	// We use the first frame number in queue to determine what frame we're currently rendering.
	uint32_t RenderFrameNumber = UpdateRequestQueue[0].RequestedEngineFrame;

#if (WITH_EDITOR)
	// Process all In-Editor requests immediately without throttling.
	if (GIsEditor && !bPlayingInEditor)
	{
		TArray<FHoloMeshUpdateRequest> DeferredUpdates;

		for (auto& UpdateRequest : UpdateRequestQueue)
		{
			if (!RegisteredMeshes.Contains(UpdateRequest.RegisteredGUID))
			{
				continue;
			}

			FRegisteredHoloMesh& item = RegisteredMeshes[UpdateRequest.RegisteredGUID];

			if (!item.IsValid() || !item.editorMesh)
			{
				continue;
			}

			if (UpdateRequest.RequestedEngineFrame > RenderFrameNumber)
			{
				DeferredUpdates.Add(UpdateRequest);
				continue;
			}

			item.component->Update_RenderThread(GraphBuilder, UpdateRequest);
			EndFrameRequestQueue.Add(UpdateRequest);
		}

		UpdateRequestQueue.Empty();
		UpdateRequestQueue = DeferredUpdates;
		return;
	}
#endif

	// Immediate Mode
	// Process all requests at once.
	if (bImmediateMode)
	{
		double executeStart = FPlatformTime::Seconds() * 1000.0;
		managerStats.updateCount = 0;

		TArray<FHoloMeshUpdateRequest> DeferredUpdates;
		for (auto& UpdateRequest : UpdateRequestQueue)
		{
			if (!RegisteredMeshes.Contains(UpdateRequest.RegisteredGUID))
			{
				continue;
			}

			FRegisteredHoloMesh& item = RegisteredMeshes[UpdateRequest.RegisteredGUID];

			if (!item.IsValid())
			{
				continue;
			}
			
			if (UpdateRequest.RequestedEngineFrame < RenderFrameNumber)
			{
				// If we have a stale frame we only drop it if we have something newer in queue
				// otherwise theres no value in dropping it.
				bool foundNewerRequest = false;
				for (auto& UpdateRequestCompare : UpdateRequestQueue)
				{
					if (UpdateRequestCompare.RegisteredGUID == UpdateRequest.RegisteredGUID && UpdateRequestCompare.RequestedEngineFrame > UpdateRequest.RequestedEngineFrame)
					{
						foundNewerRequest = true;
						break;
					}
				}

				if (foundNewerRequest)
				{
					UE_LOG(LogHoloMesh, Warning, TEXT("Dropping stale update request %d %d"), UpdateRequest.RequestedEngineFrame, RenderFrameNumber);
					continue;
				}
			}

			// If update is intended for a future frame then we put it back in the queue.
			if (UpdateRequest.RequestedEngineFrame > RenderFrameNumber)
			{
				DeferredUpdates.Add(UpdateRequest);
				continue;
			}

			double updateStart = FPlatformTime::Seconds() * 1000.0;
			item.component->Update_RenderThread(GraphBuilder, UpdateRequest);

			item.averageUpdateTime.Add((FPlatformTime::Seconds() * 1000.0) - updateStart);
			managerStats.updateCount++;

			EndFrameRequestQueue.Add(UpdateRequest);
		}

		managerStats.updateTimeAverage.Add((FPlatformTime::Seconds() * 1000.0) - executeStart);

		UpdateRequestQueue.Empty();
		UpdateRequestQueue = DeferredUpdates;
		return;
	}

	// Priority Queue System
	// Update most important players first then defer the rest for another frame.
	{
		double executeStart = FPlatformTime::Seconds() * 1000.0;
		int maxFramesSinceUpdate = 0;

		// Build Priority Queue
		TPriorityQueue<FHoloMeshUpdateRequest> updateQueue;
		for (auto& UpdateRequest : UpdateRequestQueue)
		{
			if (!RegisteredMeshes.Contains(UpdateRequest.RegisteredGUID))
			{
				continue;
			}

			FRegisteredHoloMesh& item = RegisteredMeshes[UpdateRequest.RegisteredGUID];

			if ((bPlayingInEditor && item.editorMesh) || !item.IsValid())
			{
				continue;
			}

			// Note: LOD 0 will always update even when not in frustrum.
			if ((item.LOD > 0 && !item.visible) || item.owner->IsHidden())
			{
				item.component->RequestCulled_RenderThread(UpdateRequest);
				continue;
			}

			float LODmultiplier = (HOLOMESH_MAX_LODS - item.LOD);
			float priority = LODmultiplier * (item.framesSinceUpdate + 1);

			updateQueue.Push(UpdateRequest, priority);
			maxFramesSinceUpdate = FMath::Max(item.framesSinceUpdate, maxFramesSinceUpdate);
		}

		managerStats.maxFramesSinceUpdate = maxFramesSinceUpdate;
		managerStats.updateCount = 0;

		// Process as many requests as possible depending on FrameUpdateLimit.
		bool limitReached = false;
		TArray<FHoloMeshUpdateRequest> deferredUpdates;

		while (!updateQueue.IsEmpty())
		{
			FHoloMeshUpdateRequest UpdateRequest = updateQueue.Pop();

			if (!RegisteredMeshes.Contains(UpdateRequest.RegisteredGUID))
			{
				continue;
			}

			FRegisteredHoloMesh& item = RegisteredMeshes[UpdateRequest.RegisteredGUID];

			if (!item.IsValid())
			{
				continue;
			}

			if (!limitReached)
			{
				double updateStart = FPlatformTime::Seconds() * 1000.0;
				item.component->Update_RenderThread(GraphBuilder, UpdateRequest);
				item.averageUpdateTime.Add((FPlatformTime::Seconds() * 1000.0) - updateStart);

				EndFrameRequestQueue.Add(UpdateRequest);

				item.framesSinceUpdate = 0;
				managerStats.updateCount++;
			}
			else
			{
				deferredUpdates.Add(UpdateRequest);
				item.framesSinceUpdate++;
			}

			// Throttle update time if desired.
			if (!limitReached && frameUpdateLimit > 0.0f)
			{
				double totalTime = (FPlatformTime::Seconds() * 1000.0) - executeStart;
				if (totalTime > frameUpdateLimit)
				{
					managerStats.lastBreakTime = totalTime;
					limitReached = true;
				}
			}
		}

		managerStats.updateTimeAverage.Add((FPlatformTime::Seconds() * 1000.0) - executeStart);

		UpdateRequestQueue.Empty();
		UpdateRequestQueue = deferredUpdates;
	}
}

void HoloMeshManager::ProcessEndFrameRequests(FRDGBuilder& GraphBuilder)
{
	FScopeLock Lock(&CriticalSection);

	for (auto& UpdateRequest : EndFrameRequestQueue)
	{
		if (!RegisteredMeshes.Contains(UpdateRequest.RegisteredGUID))
		{
			continue;
		}

		FRegisteredHoloMesh& item = RegisteredMeshes[UpdateRequest.RegisteredGUID];

		if (!item.IsValid())
		{
			continue;
		}

		item.component->EndFrame_RenderThread(GraphBuilder, UpdateRequest);
	}

	EndFrameRequestQueue.Empty();
}

void HoloMeshManager::OnPostOpaque_RenderThread(FPostOpaqueRenderParameters& Parameters)
{
	SCOPE_CYCLE_COUNTER(STAT_HoloMesh_Manager_Execute);

	FScopeLock Lock(&CriticalSection);

	// Pass the event onto any registered compononents to use for drawing masks
	// such as ResponsiveAA.
	for (auto& kv : RegisteredMeshes)
	{
		FRegisteredHoloMesh& item = kv.Value;

		if (!item.IsValid())
		{
			continue;
		}

		if (item.owner->IsHidden())
		{
			continue;
		}

		if (bPlayingInEditor && item.editorMesh)
		{
			continue;
		}

		item.component->OnPostOpaqueRender(Parameters);
	}

#if ENGINE_MAJOR_VERSION == 5
	UpdateStats((FSceneView*)Parameters.View);
#else
	UpdateStats((FSceneView*)Parameters.Uid);
#endif
}

void HoloMeshManager::UpdateStats(FSceneView* sceneView)
{
	SCOPE_CYCLE_COUNTER(STAT_HoloMesh_Manager_UpdateStats);

	// Compute FPS
	managerStats.frameCount++;
	if (FPlatformTime::Seconds() - managerStats.fpsStartTime >= 1.0f)
	{
		managerStats.averageFPS = managerStats.frameCount / (FPlatformTime::Seconds() - managerStats.fpsStartTime);

		bool isStereoRender = GEngine->XRSystem.IsValid() && GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
		if (isStereoRender)
		{
			// VR will double the render calls.
			managerStats.averageFPS = managerStats.averageFPS / 2.0f;
		}

		managerStats.frameCount = 0;
		managerStats.fpsStartTime = FPlatformTime::Seconds();

		managerStats.uploadBytesPerSecond = managerStats.totalUploadBytes.load() - managerStats.lastUploadBytes;
		managerStats.lastUploadBytes = managerStats.totalUploadBytes.load();

		managerStats.ioBytesPerSecond = managerStats.totalIOBytes.load() - managerStats.ioLastBytes;
		managerStats.ioLastBytes = managerStats.totalIOBytes.load();
	}

	managerStats.visibleMeshes = 0;
	managerStats.lodCounts[0] = 0;
	managerStats.lodCounts[1] = 0;
	managerStats.lodCounts[2] = 0;

	TArray<TKeyValuePair<FString, double>> UpdateAverages;

	if (sceneView != nullptr)
	{
		FVector cameraPosition;
		cameraPosition.X = sceneView->ViewLocation.X;
		cameraPosition.Y = sceneView->ViewLocation.Y;
		cameraPosition.Z = sceneView->ViewLocation.Z;

		for (auto& kv : RegisteredMeshes)
		{
			FRegisteredHoloMesh& item = kv.Value;
			if (!item.IsValid())
			{
				continue;
			}

			if (bPlayingInEditor && item.editorMesh)
			{
				continue;
			}

			UHoloMeshComponent* component = item.component;
			AActor* owner = item.owner;

			if (bFrustumCulling)
			{
				FVector cameraRelativePosition = (owner->GetActorLocation() + component->Bounds.Origin) - cameraPosition;
				item.visible = sceneView->ViewFrustum.IntersectBox(cameraRelativePosition, component->Bounds.BoxExtent);
				if (item.visible)
				{
					managerStats.visibleMeshes++;
				}
			}
			else 
			{
				item.visible = true;
				managerStats.visibleMeshes = -1;
			}

			UpdateAverages.Add(TKeyValuePair<FString, double>(item.owner->GetName(), item.averageUpdateTime.GetAverage()));
			
			item.LOD = component->GetHoloMeshLOD();
			if (item.LOD >= 0 && item.LOD < HOLOMESH_MAX_LODS)
			{
				managerStats.lodCounts[item.LOD]++;
			}
		}
	}

	const int32 ArcturusDebugMessageKey = 65 + 82 + 67 + 84 + 85 + 82 + 85 + 83;
	float dbgTime = 1.0f;

	bool displayStats = CVarEnableHoloMeshStats.GetValueOnRenderThread();
	if (displayStats && GEngine)
	{
		int meshMB      = FUnitConversion::Convert(managerStats.totalMeshBytes.load(), EUnit::Bytes, EUnit::Megabytes);
		int textureMB   = FUnitConversion::Convert(managerStats.totalTextureBytes.load(), EUnit::Bytes, EUnit::Megabytes);
		int containerMB = FUnitConversion::Convert(managerStats.totalContainerBytes.load(), EUnit::Bytes, EUnit::Megabytes);
		int blockPoolMB = FUnitConversion::Convert(FHoloMemoryBlock::TotalAllocatedBytes.load(), EUnit::Bytes, EUnit::Megabytes);
		
		GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 100, dbgTime, FColor::Green, FString::Printf(TEXT("HoloMesh Manager")), true, FVector2D(1.f, 1.f));
		GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 101, dbgTime, FColor::Green, FString::Printf(TEXT("  FPS: %.2f | Update Avg: %.2f ms Max: %.2f ms"), managerStats.averageFPS, managerStats.updateTimeAverage.GetAverage(), managerStats.updateTimeAverage.GetMax()), true, FVector2D(1.f, 1.f));
		GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 102, dbgTime, FColor::Green, FString::Printf(TEXT("  Visible: %d | LOD 0: %d | LOD 1: %d | LOD 2: %d"), managerStats.visibleMeshes, managerStats.lodCounts[0], managerStats.lodCounts[1], managerStats.lodCounts[2]), true, FVector2D(1.f, 1.f));
		GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 103, dbgTime, FColor::Green, FString::Printf(TEXT("  Meshes: %d mb | Textures: %d mb | Containers: %d/%d mb"), meshMB, textureMB, containerMB, blockPoolMB), true, FVector2D(1.f, 1.f));

		if (bImmediateMode)
		{
			// In immediate mode we show I/O misses as those are blocking operations.
			GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 104, dbgTime, FColor::Green, FString::Printf(TEXT("  I/O Misses: %zu | Avg: %.4f ms | Max: %.4f ms"), 0, managerStats.ioAverageTime.GetAverage(), managerStats.ioAverageTime.GetMax()), true, FVector2D(1.f, 1.f));
		}
		else 
		{
			GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 104, dbgTime, FColor::Green, FString::Printf(TEXT("  Last Break: %f | Update Count: %d | Update Limit: %.2f | Max Stale Frames: %d"), managerStats.lastBreakTime, managerStats.updateCount, frameUpdateLimit, managerStats.maxFramesSinceUpdate), true, FVector2D(1.f, 1.f));
		}

		int ioMBPS = FUnitConversion::Convert(managerStats.ioBytesPerSecond, EUnit::Bytes, EUnit::Megabytes);
		GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 105, dbgTime, FColor::Green, FString::Printf(TEXT("  I/O Read: %d mb/s | I/O Avg: %.4f ms | I/O Max: %.4f ms"), ioMBPS, managerStats.ioAverageTime.GetAverage(), managerStats.ioAverageTime.GetMax()), true, FVector2D(1.f, 1.f));

		int uploadMBPS = FUnitConversion::Convert(managerStats.uploadBytesPerSecond, EUnit::Bytes, EUnit::Megabytes);
		GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 106, dbgTime, FColor::Green, FString::Printf(TEXT("  GPU Upload: %d mb/s"), uploadMBPS), true, FVector2D(1.f, 1.f));

		// Player Update Times
		{
			// Sort update average times.
			UpdateAverages.Sort([](const TKeyValuePair<FString, double>& ip1, const TKeyValuePair<FString, double>& ip2)
				{
					return  ip1.Value > ip2.Value;
				});

			// Print top 5 player update averages
			GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 107, dbgTime, FColor::Orange, FString::Printf(TEXT("Player Update Times")), true, FVector2D(1.f, 1.f));
			for (int i = 0; i < 5; ++i)
			{
				if (i >= UpdateAverages.Num())
				{
					continue;
				}

				GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 108 + i, dbgTime, FColor::Orange, FString::Printf(TEXT("  %d. %s: %.4f ms"), i + 1, *UpdateAverages[i].Key, UpdateAverages[i].Value), true, FVector2D(1.f, 1.f));
			}
		}

		// Memory Pool Stats
		{
			GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 120, dbgTime, FColor::Emerald, FString::Printf(TEXT("Memory Pool")), true, FVector2D(1.f, 1.f));

			int row = 0;

			TArray<std::pair<SIZE_T, uint32_t>> poolContents = MemoryPool->PeekPoolContents();
			for (auto& poolRow : poolContents)
			{
				int poolRowSizeMB = FUnitConversion::Convert(poolRow.first, EUnit::Bytes, EUnit::Kilobytes);
				GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 121 + row, dbgTime, FColor::Emerald, FString::Printf(TEXT("  Size: %d KB, Count: %d"), poolRowSizeMB, poolRow.second), true, FVector2D(1.f, 1.f));
				row++;
			}
		}
	}
}

void HoloMeshManager::AddIOResult(size_t sizeInBytes, float fillTimeMS)
{
	managerStats.totalIOBytes += sizeInBytes;
	managerStats.ioAverageTime.Add(fillTimeMS);
}

void HoloMeshManager::Tick(float DeltaSeconds)
{
	if (GFrameNumber == lastFrameNumber)
	{
		return;
	}

	lastFrameNumber = GFrameNumber;

	if (MemoryPool != nullptr && FPlatformTime::Seconds() - lastMemoryCleanUpTime > 0.25)
	{
		AsyncTask(ENamedThreads::AnyThread, [this]
			{
				MemoryPool->CleanUp();
			});

		lastMemoryCleanUpTime = FPlatformTime::Seconds();
	}
}

#if ENGINE_MAJOR_VERSION == 5
void HoloMeshManager::BeginFrame(FRDGBuilder& GraphBuilder)
{
	ProcessRequests(GraphBuilder);
}

void HoloMeshManager::EndFrame(FRDGBuilder& GraphBuilder)
{
	ProcessEndFrameRequests(GraphBuilder);
}
#endif

// - Scene View Extension -

FHoloMeshSceneViewExtension::FHoloMeshSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FHoloMeshSceneViewExtension::PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
#if ENGINE_MAJOR_VERSION == 4
	FRDGBuilder GraphBuilder(RHICmdList);

	// Since we have no end of frame event in this mode we execute the 
	// end frame requests at the beginning of each frame.
	GHoloMeshManager.ProcessEndFrameRequests(GraphBuilder);

	GHoloMeshManager.ProcessRequests(GraphBuilder);
	GraphBuilder.Execute();
#endif
}

// - Blueprint Library -

float UHoloMeshManagerBlueprintLibrary::GetFPS()
{
	return GHoloMeshManager.GetFPS();
}

float UHoloMeshManagerBlueprintLibrary::GetLastBreakTime()
{
	return GHoloMeshManager.GetLastBreakTime();
}

int UHoloMeshManagerBlueprintLibrary::GetVisibleMeshCount()
{
	return GHoloMeshManager.GetVisibleMeshCount();
}

float UHoloMeshManagerBlueprintLibrary::GetAverageIOTime()
{
	return GHoloMeshManager.GetAverageIOTime();
}

void FHoloMeshWorkRequest::DoThreadedWork()
{
	FRegisteredHoloMesh* registeredMesh = GHoloMeshManager.GetRegisteredMesh(RegisteredGUID);
	if (registeredMesh && registeredMesh->IsValid())
	{
		registeredMesh->component->DoThreadedWork(SegmentIndex, FrameIndex);
	}
	GHoloMeshManager.FinishWorkRequest(this);
}

void FHoloMeshWorkRequest::Abandon()
{
	UE_LOG(LogHoloMesh, Warning, TEXT("HoloMesh Threaded Work Abandoned."));
	GHoloMeshManager.FinishWorkRequest(this);
}