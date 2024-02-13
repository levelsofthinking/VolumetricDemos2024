// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "RHIGPUReadback.h"
#include "Engine/Texture2D.h"
#include "HardwareInfo.h"

#include <atomic>
#include <thread>
#include <mutex>

#include "OMS/oms.h"
#include "OMS/OMSFile.h"
#include "HoloMeshComponent.h"
#include "HoloMeshUtilities.h"
#include "HoloSuitePlayerModule.h"

#include "OMSDecoder.generated.h"

/**
 * OMSDecoder submits threaded work to HoloMeshManager which decodes the next requested sequence. 
 * It also manages a list of decoded sequences and will buffer ahead of the player.
 */

// Controls how many texture frame readbacks to cache. 
// Recommended at least 3 due to unreal having 2 frames in flight and us using one.
#define OMS_TEXTURE_FRAME_COUNT 3

class FDecodedOMSSequence
{
public:
    int sequenceIndex = -1;
    FHoloMesh* holoMesh = nullptr;
    oms_sequence_t* sequence = nullptr;

    ~FDecodedOMSSequence()
    {
        ENQUEUE_RENDER_COMMAND(DeleteHoloMesh)([HoloMesh = holoMesh]
        (FRHICommandListImmediate& RHICmdList)
        {
            delete HoloMesh;
        });
        holoMesh = nullptr;

        if (sequence != nullptr)
        {
            oms_free_sequence(sequence);
            free(sequence);
            sequence = nullptr;
        }
    }
};
typedef TSharedPtr<FDecodedOMSSequence> FDecodedOMSSequenceRef;

struct FDecodedOMSTextureFrame
{
    int FrameNumber;
    UTexture* SourceTexture;
    UTexture* Texture;
    FIntPoint TextureSize;
    EPixelFormat TextureFormat;

    TSharedPtr<FRHIGPUBufferReadback> FrameNumberReadback;
    int FrameNumberReadbackTimeout;

    FDecodedOMSTextureFrame()
        : FrameNumber(-1),
          SourceTexture(nullptr),
          Texture(nullptr),
          TextureSize(0, 0),
          TextureFormat(EPixelFormat::PF_B8G8R8A8), 
          FrameNumberReadbackTimeout(0)
    {}

    ~FDecodedOMSTextureFrame()
    {
        if (Texture != nullptr)
        {
            // Allow garbage collection to clean up the old texture.
            Texture->RemoveFromRoot();
            Texture = nullptr;
        }
    }
};

class UOMSPlayerComponent;

UCLASS()
class HOLOSUITEPLAYER_API UOMSDecoder : public UHoloMeshComponent
{
public:
    GENERATED_BODY()

public:

    UOMSDecoder(const FObjectInitializer& ObjectInitializer);
    ~UOMSDecoder() override;

    void Configure(UOMSPlayerComponent* NewPlayerComponent, bool NewUseCPUDecoder, int NewMaxBufferedSequences);

    bool GetUseCPUDecoder() { return bUseCPUDecoder; }
    int GetMaxBufferedSequences() { return MaxBufferedSequences; }

    bool OpenOMS(UOMSFile* NewOMSFile, UMaterialInterface* NewMeshMaterial);
    void Close();
    void Update();

    // Returns the total number of frames in the OMS file.
    int GetFrameCount();

    // Returns a pair of sequenceIndex, frameIndex for a given a content frame number.
    // Will return (-1, -1) if the requested frame number is invalid.
    std::pair<int, int> GetFrameFromLookupTable(int contentFrameNumber);

    // Requests the decoder 
    void RequestSequence(int index);

    // Returns the sequence if its been decoded.
    // If waitForSequence is enabled the function will block until the sequence is decoded.
    FDecodedOMSSequenceRef GetSequence(int index, bool waitForSequence);

    // Called by HoloMeshManager when a work request is executed. Executes
    // on a worker thread, not game or render thread.
    void DoThreadedWork(int sequenceIndex, int frameIndex) override;

    // Determines if compute shaders are supported and, consequently which decoding methods can be used.
    static bool CheckComputeSupport();

    // Begins a frame decode. This is not a blocking operation, 
    // utilize IsNewFrameReady() to check for completion.
    void DecodeFrameNumber();

    // Called by HoloMeshManager to perform render thread. Currently used to
    // copy the source texture and decode frame number.
    void Update_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest UpdateRequest) override;
    void RequestCulled_RenderThread(FHoloMeshUpdateRequest request) override;

    void FreeUnusedMemory() override;

    // Returns frame number and texture pair.
    // Previously returned frame textures are invalid after this call.
    std::pair<uint32_t, UTexture*> GetFrame();

    // Returns true if the decoder has a new frame ready.
    bool IsNewFrameReady() { return bNewTextureFrameReady; }

    // Returns a new frame number if one is available.
    int GetNewFrameNumber();

    void LoadMeshMaterial(UMaterialInterface* NewMeshMaterial);

    void UpdateMeshMaterial(bool write, bool frameTexture, bool boneTexture, bool retarget, bool ssdr, float ssdrEnabled);

private:

    enum class EMeshDecoderState
    {
        Idle,
        Waiting,
        Error
    };
    std::atomic<EMeshDecoderState> MeshDecoderState = { EMeshDecoderState::Idle };

    enum class ETextureDecoderState
    {
        Idle,
        Reading,
        Waiting,
        Error
    };
    std::atomic<ETextureDecoderState> TextureDecoderState = { ETextureDecoderState::Idle };

    UOMSFile* OMSFile;

    // Header metadata of the OMS source.
    oms_header_t* OMSHeader;

    // Table used to look for the sequence index and offset for each frame.
    TArray<std::pair<int, int>> frameLookupTable;

    // Number of sequences whose static meshes the worker can generate and queue. 
    int MaxBufferedSequences;

    // Default maximum amount for NumBufferedSequences.
    int DefaultMaxBufferedSequences;

    // Index of the last sequence that was decoded.
    std::atomic<int> lastDecodedSequence = { 0 };

    // Index of the next sequence to be decoded or -1 if buffer is full.
    std::atomic<int> nextDecodedSequence = { 0 };

    // Decoded Queue will be populated by threaded work, FlushDecodedQueue will transfer
    // decoded sequences into the decodedSequences array to be managed.
    TQueue<FDecodedOMSSequenceRef> decodedQueue;

    // Anything in the free queue will be freed on the worker thread on its next pass.
    // This is a performance optimization so we don't pay anything on game thread.
    TQueue<FDecodedOMSSequenceRef> freeQueue;

    // Stores sequences that are ready for consumption by the player.
    TArray<FDecodedOMSSequenceRef> decodedSequences;

    // Clears all data including the decoded queue and list.
    void ClearData();

    void AdvanceNextSequence();
    void FlushDecodedQueue();
    void ValidateMaxBufferedSequences();

    // -- Texture Decoding --

    UOMSPlayerComponent* ActorComponent;

    bool bFrameDecoderSelected;
    bool bUseFastScrubbing;
    bool bUseCPUDecoder;

    std::atomic<bool> bNewTextureFrameReady{ false };
    TStaticArray<FDecodedOMSTextureFrame, OMS_TEXTURE_FRAME_COUNT> DecodedTextureFrames;

    // The index into DecodedTextureFrame which is currently being used by the mesh.
    int ReadFrameIdx;

    // The index into DecodedTextureFrame which is currently being written to by compute shader.
    int WriteFrameIdx;

    void FastScrubbingTextureDecode();
    void ReadbackTextureDecode(UMaterialInterface* sourceMaterial);
    void ComputeTextureDecode(UMaterialInterface* sourceMaterial);
};