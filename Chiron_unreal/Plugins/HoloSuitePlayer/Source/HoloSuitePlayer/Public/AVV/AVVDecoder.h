// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "AVVReader.h"
#include "AVVShaders.h"
#include "AVVUtilities.h"
#include "HoloMeshManager.h"
#include "HoloMeshUtilities.h"
#include "HoloMeshComponent.h"
#include "HoloSuitePlayerModule.h"

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderGraphResources.h"
#include "Runtime/Engine/Classes/Engine/TextureRenderTarget2D.h"
#include "ClearQuad.h"
#include "RenderResource.h"
#include "RenderGraphUtils.h"
#include "Stats/Stats.h"

#include <deque>

#include "AVVDecoder.generated.h"

UCLASS()
class HOLOSUITEPLAYER_API UAVVDecoder : public UHoloMeshComponent
{
    GENERATED_BODY()

public:	

    UAVVDecoder(const FObjectInitializer& ObjectInitializer);
    virtual ~UAVVDecoder();

    int FrameCount;

    void Configure(bool immediateMode);
    void SetCachingDirection(bool reversedCaching) { bReversedCaching = reversedCaching; }

    virtual bool OpenAVV(UAVVFile* AVVFile, UMaterialInterface* NewMeshMaterial);
    virtual void Close();

    // When force is set to true frame will update even if it's currently on the requested frame.
    virtual void SetFrame(int frameIndex, bool force = false);
    virtual void Update(float DeltaTime);

    // Called by HoloMeshManager when a work request is executed. Executes
    // on a worker thread, not game or render thread.
    virtual void DoThreadedWork(int sequenceIndex, int frameIndex) override;

    // Pull read data from AVVReader and push into data cache.
    void UpdateDataCache();
    bool DecodePending(bool requestIfMissing = false, bool requestNextFrame = false);

    // Called by the manager to flush out any excess memory usage.
    virtual void FreeUnusedMemory() override;

    virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;

    // Will update mesh material and recreate render proxy.
    void SetMeshMaterial(UMaterialInterface* meshMaterial);

    // Checks if source AVV file contains skeleton data.
    bool HasSkeletonData();

    // Decoding functions that are shared between both CPU and Compute decoders.
    void UpdateTextureBlockMap(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment);
    void DecodeFrameAnimation(FRDGBuilder& GraphBuilder, AVVEncodedFrame* frame, FHoloMesh* meshOut);
    void DecodeFrameTexture(FRDGBuilder& GraphBuilder, AVVEncodedFrame* frame, FHoloMesh* meshOut);

protected:
    bool bInitialized;
    bool bImmediateMode;
    bool bUseBC4HardwareDecoding;

    FAVVReader avvReader;

    // Tracks the state of the data as it moves from CPU to GPU
    enum class EDecoderState
    {
        Idle,
        WaitingCPU,
        FinishedCPU,
        WaitingGPU,
        FinishedGPU,
        Error
    };
    std::atomic<EDecoderState> DecoderState = { EDecoderState::Idle };

    // Segments and frames go from Requested -> Pending -> Current.
    // With Current being fully decoded and being displayed.
    struct DecodingState
    {
        int FrameNumber  = -1;

        void Reset()
        {
            FrameNumber  = -1;
        }
    };

    DecodingState RequestedState;
    DecodingState PendingState;
    DecodingState CurrentState;

    bool bReversedCaching = false;
    FAVVDataCache DataCache;

    std::atomic<int> DecodedSegmentIndex = { -1 };
    int DecodedSegmentVertexCount = 0;
    AVVEncodedTextureInfo DecodedSegmentTextureInfo = {};

    // Data Containers
    TRefCountPtr<FRDGPooledBuffer> AnimDataBuffer;
    TRefCountPtr<FRDGPooledBuffer> DecodedVertexBuffer;
    TRefCountPtr<FRDGPooledBuffer> TextureBlockMapBuffer;

    virtual void InitDecoder(UMaterialInterface* NewMeshMaterial);

    // Used for immediate mode decoding, will execute all steps to decoding a frame immediately.
    // This includes blocking on the segment data request if necessary.
    void SetFrameImmediate(int frameNumber);

    // Update Bounding Box (Game Thread)
    void UpdateBoundingBox(AVVEncodedSegment* segment, FHoloMesh* meshOut);

    bool ShouldRequestTexture();
    void ApplyTextures(FHoloMesh* Mesh, AVVEncodedSegment* segment = nullptr);
    void ClearTextures(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment, FHoloMesh* meshOut);
    void UploadData(FRDGBuilder& GraphBuilder, FRDGBufferRef Buffer, void* DataPtr, uint32_t SizeInBytes, AVVEncodedSegment* SourceSegment = nullptr, AVVEncodedFrame* SourceFrame = nullptr);
};