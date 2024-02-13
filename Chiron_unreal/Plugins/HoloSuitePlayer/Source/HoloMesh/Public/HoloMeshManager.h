// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Misc/IQueuedWork.h"
#include "SceneViewExtension.h"
#include "RHI.h"
#include "RHIResources.h"
#include "Tickable.h"
#include "HoloMeshUtilities.h"

#include "HoloMeshManager.generated.h"

class FHoloMeshSceneViewExtension;
class UHoloMeshComponent;

USTRUCT()
struct FRegisteredHoloMesh
{
    GENERATED_BODY()

    UPROPERTY()
    UHoloMeshComponent* component = nullptr;

    UPROPERTY()
    AActor* owner = nullptr;

    int LOD = 0;
    bool visible = false;
    bool editorMesh = false;
    int framesSinceUpdate = 0;
    int lastContentFrame = -1;

    TMovingAverage<double, 30> averageUpdateTime;

    bool IsValid()
    {
        return component != nullptr && owner != nullptr;
    }
};

struct FHoloMeshUpdateRequest
{
    FGuid RegisteredGUID;

    int HoloMeshIndex = -1;
    int SegmentIndex = 0;
    int FrameIndex = 0;

    uint32 RequestedEngineFrame = 0;

    bool operator==(const struct FHoloMeshUpdateRequest& rhs) const
    {
        return (RegisteredGUID == rhs.RegisteredGUID);
    }
};

class FHoloMeshWorkRequest : public IQueuedWork
{
public:
    FGuid RegisteredGUID;

    int SegmentIndex = 0;
    int FrameIndex = 0;

    virtual void DoThreadedWork();
    virtual void Abandon();
};

class HOLOMESH_API HoloMeshManager : public FRenderResource, public FTickableGameObject
{
public:
    HoloMeshManager();

    void Initialize();

    void Configure(float _frameUpdateLimit, bool _frustumCulling, bool _immediateMode)
    {
        frameUpdateLimit = _frameUpdateLimit;
        bFrustumCulling = _frustumCulling;
        bImmediateMode = _immediateMode;
    }

    FGuid Register(UHoloMeshComponent* component, AActor* owner);
    FRegisteredHoloMesh* GetRegisteredMesh(FGuid registeredGUID);
    void Unregister(FGuid registeredGUID);

    // Memory Management
    FHoloMemoryBlockRef AllocBlock(SIZE_T SizeInBytes);
    void FreeBlock(FHoloMemoryBlockRef Block);
    void FreeUnusedMemory();

    // Render Update Requests
    void AddUpdateRequest(FGuid holoMeshGUID, int holoMeshIndex, int segmentIndex, int frameIndex);
    void ClearRequests(FGuid holoMeshGUID);
    void ProcessRequests(FRDGBuilder& GraphBuilder);
    void ProcessEndFrameRequests(FRDGBuilder& GraphBuilder);

    // Threaded Work
    TReusableObjectPool<FHoloMeshWorkRequest, 16368>* WorkRequestPool;
    void AddWorkRequest(FGuid holoMeshGUID, int segmentIndex, int frameIndex);
    void FinishWorkRequest(FHoloMeshWorkRequest* request);

    // Update manager stats and view frustum related information.
    void UpdateStats(FSceneView* sceneView);

    void BeginRendering();
    void EndRendering();
    void BeginPIE();
    void EndPIE();

    void OnPostOpaque_RenderThread(FPostOpaqueRenderParameters& Parameters);
    
    float GetFPS() { return managerStats.averageFPS; }
    float GetLastBreakTime() { return managerStats.lastBreakTime; }
    float GetFrameUpdateLimit() { return frameUpdateLimit; }
    int GetVisibleMeshCount() { return managerStats.visibleMeshes; }
    float GetAverageIOTime() { return managerStats.ioAverageTime.GetAverage(); }

    // For memory statistics tracking purposes.
    void AddMeshBytes(size_t meshBytes)             { managerStats.totalMeshBytes += meshBytes; }
    void RemoveMeshBytes(size_t meshBytes)          { managerStats.totalMeshBytes -= meshBytes; }
    void AddTextureBytes(size_t textureSize)        { managerStats.totalTextureBytes += textureSize; }
    void RemoveTextureBytes(size_t textureSize)     { managerStats.totalTextureBytes -= textureSize; }
    void AddContainerBytes(size_t containerSize)    { managerStats.totalContainerBytes += containerSize; }
    void RemoveContainerBytes(size_t containerSize) { managerStats.totalContainerBytes -= containerSize; }

    void AddIOResult(size_t sizeInBytes, float fillTimeMS);
    void AddUploadBytes(size_t bufferSize) { managerStats.totalUploadBytes += bufferSize; }

    /** FTickableGameObject implementation */
    virtual void Tick(float DeltaSeconds) override;
    virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(HoloMeshManager, STATGROUP_Tickables); }
    virtual bool IsTickableWhenPaused() const override { return true; }
    virtual bool IsTickableInEditor() const { return true; }

    void DisableTickUpdates() { bUseTickUpdates = false; }

#if ENGINE_MAJOR_VERSION == 5
    void BeginFrame(FRDGBuilder& GraphBuilder);
    void EndFrame(FRDGBuilder& GraphBuilder);
#endif

private:
    struct HoloMeshManagerStats
    {
        TMovingAverage<double, 30> updateTimeAverage;
        float lastBreakTime = 0.0f;
        
        int visibleMeshes = 0;
        int lodCounts[3] = {0,0,0};

        float averageFPS = 0.0f;
        int frameCount = 0;
        double fpsStartTime = 0.0;

        int updateCount = 0;
        int maxFramesSinceUpdate = 0;
        
        std::atomic<size_t> totalMeshBytes      = { 0 };
        std::atomic<size_t> totalTextureBytes   = { 0 };
        std::atomic<size_t> totalContainerBytes = { 0 };
        std::atomic<size_t> totalUploadBytes    = { 0 };

        size_t uploadBytesPerSecond = 0;
        size_t lastUploadBytes = 0;

        // IO
        size_t ioBytesPerSecond = 0;
        size_t ioLastBytes = 0;
        std::atomic<size_t> totalIOBytes = { 0 };
        TMovingAverage<float, 30> ioAverageTime;
    } managerStats;
    
    int queuePosition;
    bool bInitialized = false;
    bool bUseTickUpdates = false;
    bool bFrustumCulling = true;
    bool bImmediateMode = false;
    bool bPlayingInEditor = false;
    float frameUpdateLimit = 0.0f; 
    uint32 lastFrameNumber = 0;
    double lastMemoryCleanUpTime = 0.0;

    FDelegateHandle PostOpaqueRenderHandle;
    TSharedPtr<class FHoloMeshSceneViewExtension, ESPMode::ThreadSafe> PostProcessSceneViewExtension;

    mutable FCriticalSection CriticalSection;
    TMap<FGuid, FRegisteredHoloMesh> RegisteredMeshes;
    TArray<FHoloMeshUpdateRequest> UpdateRequestQueue;
    TArray<FHoloMeshUpdateRequest> EndFrameRequestQueue;

    FHoloMemoryPool* MemoryPool;
    FQueuedThreadPool* ThreadPool;
};

extern HOLOMESH_API TGlobalResource<HoloMeshManager> GHoloMeshManager;

UCLASS(meta = (ScriptName = "HoloMeshManager"))
class UHoloMeshManagerBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "HoloMeshManager")
    static float GetFPS();

    UFUNCTION(BlueprintPure, Category = "HoloMeshManager")
    static float GetLastBreakTime();

    UFUNCTION(BlueprintPure, Category = "HoloMeshManager")
    static int GetVisibleMeshCount();

    UFUNCTION(BlueprintPure, Category = "HoloMeshManager")
    static float GetAverageIOTime();
};

// Used to hook into PrePostProcessPass_RenderThread.
class FHoloMeshSceneViewExtension : public FSceneViewExtensionBase
{
public:
    FHoloMeshSceneViewExtension(const FAutoRegister& AutoRegister);

    //~ Begin FSceneViewExtensionBase Interface
    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {};
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};
    virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;
    virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override {};
    virtual void PostRenderBasePass_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override {};
    virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override {};
    //~ End FSceneViewExtensionBase Interface
};