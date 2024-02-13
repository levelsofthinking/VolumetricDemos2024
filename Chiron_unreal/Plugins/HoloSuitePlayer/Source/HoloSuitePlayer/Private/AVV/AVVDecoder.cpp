// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "AVV/AVVDecoder.h"
#include "RenderGraphUtils.h"

IMPLEMENT_GLOBAL_SHADER(FAVVDecodeTextureBlock_BC4_CS, "/HoloSuitePlayer/AVV/AVVLumaDecodeCS.usf", "DecodeTextureBlockBC4", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVCopyTextureBlock_BC4_CS,   "/HoloSuitePlayer/AVV/AVVLumaDecodeCS.usf", "CopyTextureBlockBC4",   SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVDecodeFrameAnim_None_CS,   "/HoloSuitePlayer/AVV/AVVAnimDecodeCS.usf", "DecodeFrameAnimNone",   SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVDecodeFrameAnim_SSDR_CS,   "/HoloSuitePlayer/AVV/AVVAnimDecodeCS.usf", "DecodeFrameAnimSSDR",   SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVDecodeFrameAnim_Delta_CS,  "/HoloSuitePlayer/AVV/AVVAnimDecodeCS.usf", "DecodeFrameAnimDelta",  SF_Compute);

DECLARE_CYCLE_STAT(TEXT("AVVDecoder.OpenAVV"),          STAT_AVVDecoder_OpenAVV,            STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoder.Close"),            STAT_AVVDecoder_Close,              STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoder.SetFrame"),         STAT_AVVDecoder_SetFrame,           STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoder.Update"),           STAT_AVVDecoder_Update,             STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoder.DecodeAnimation"),  STAT_AVVDecoder_DecodeAnimation,    STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoder.ClearTextures"),    STAT_AVVDecoder_ClearTextures,      STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoder.DecodeTexture"),    STAT_AVVDecoder_DecodeTexture,      STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoder.DecodePending"),    STAT_AVVDecoder_DecodePending,      STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoder.UpdateDataCache"),  STAT_AVVDecoder_UpdateDataCache,    STATGROUP_HoloSuitePlayer);

DECLARE_GPU_STAT_NAMED(GPU_AVVDecoder_DecodeAnimation,  TEXT("AVVDecoder.GPUDecodeAnimation"));
DECLARE_GPU_STAT_NAMED(GPU_AVVDecoder_ClearTextures,    TEXT("AVVDecoder.GPUClearTextures"));
DECLARE_GPU_STAT_NAMED(GPU_AVVDecoder_DecodeTexture,    TEXT("AVVDecoder.GPUDecodeTexture"));

// Sets default values
UAVVDecoder::UAVVDecoder(const FObjectInitializer& ObjectInitializer) : UHoloMeshComponent(ObjectInitializer)
{
    bInitialized = false;
    bImmediateMode = false;
    DecoderState = EDecoderState::Idle;
    TextureBlockMapBuffer = nullptr;

    RequestedState.Reset();
    PendingState.Reset();
    CurrentState.Reset();

#if PLATFORM_ANDROID
    bUseBC4HardwareDecoding = false;
#else
    // PF_BC4 native textures only available on desktop.
    bUseBC4HardwareDecoding = true;
#endif
}

UAVVDecoder::~UAVVDecoder()
{
    Close();

    if (HoloMeshMaterial != nullptr)
    {
        HoloMeshMaterial->RemoveFromRoot();
    }
}

void UAVVDecoder::Configure(bool immediateMode)
{
    bImmediateMode = immediateMode;

    if (bImmediateMode)
    {
        UE_LOG(LogHoloSuitePlayer, Display, TEXT("AVV Decoder running in immediate mode."));
    }
}

bool UAVVDecoder::OpenAVV(UAVVFile* AVVFile, UMaterialInterface* NewMeshMaterial)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoder_OpenAVV);

    Close();

    bool openSuccess = avvReader.Open(AVVFile);
    if (!openSuccess)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("Failed to load AVV File."));
        return false;
    }

    InitDecoder(NewMeshMaterial);

    GHoloMeshManager.Register(this, GetOwner());

    return true;
}

void UAVVDecoder::InitDecoder(UMaterialInterface* NewMeshMaterial)
{
    FrameCount = avvReader.FrameCount;
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("Loaded AVV Version: %s"), *avvReader.VersionString);
}

void UAVVDecoder::Close()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoder_Close);

    GHoloMeshManager.ClearRequests(RegisteredGUID);
    GHoloMeshManager.Unregister(RegisteredGUID);
    RegisteredGUID.Invalidate();
    DataCache.Empty();
}

void UAVVDecoder::SetFrame(int frameNumber, bool force)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoder_SetFrame);

    if (bImmediateMode)
    {
        SetFrameImmediate(frameNumber);
        return;
    }

    int requestedSegment = avvReader.GetSegmentIndex(frameNumber);

    if (force)
    {
        CurrentState.Reset();
        RequestedState.FrameNumber  = frameNumber;
        return;
    }

    if (CurrentState.FrameNumber != frameNumber)
    {
        RequestedState.FrameNumber = frameNumber;
    }
}

void UAVVDecoder::SetFrameImmediate(int frameNumber)
{
    int requestedSegment = avvReader.GetSegmentIndex(frameNumber);
    bool dataReady = (DataCache.HasSegment(requestedSegment) || DecodedSegmentIndex == requestedSegment) && DataCache.HasFrame(frameNumber);

    if (!dataReady)
    {
        if (!DataCache.HasSegment(requestedSegment))
        {
            // Request segment + frame + texture.
            avvReader.AddRequest(requestedSegment, frameNumber, ShouldRequestTexture(), true);
        }
        else if (!DataCache.HasFrame(frameNumber))
        {
            // Only request the frame data + texture.
            avvReader.AddRequest(-1, frameNumber, ShouldRequestTexture(), true);
        }

        PendingState.FrameNumber = frameNumber;
        UpdateDataCache();
    }

    dataReady = (DataCache.HasSegment(requestedSegment) || (DecodedSegmentIndex == requestedSegment)) && DataCache.HasFrame(frameNumber);
    if (dataReady)
    {
        GHoloMeshManager.AddUpdateRequest(RegisteredGUID, 0, requestedSegment, frameNumber);
    }
    else 
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("SetFrameImmediate failed to read frame %d."), frameNumber);
    }
}

UMaterialInterface* UAVVDecoder::GetMaterial(int32 ElementIndex) const
{
    return HoloMesh[ReadIndex].Material;
}

void UAVVDecoder::SetMeshMaterial(UMaterialInterface* meshMaterial)
{
    if (HoloMeshMaterial != nullptr)
    {
        HoloMeshMaterial->RemoveFromRoot();
    }

    HoloMeshMaterial = UHoloMeshMaterial::Create(meshMaterial, GetTransientPackage());
    HoloMeshMaterial->AddToRoot();

    for (uint32_t i = 0; i < HOLOMESH_BUFFER_COUNT; ++i)
    {
        FHoloMesh* Mesh = &HoloMesh[i];

        Mesh->Material = HoloMeshMaterial->GetMaterialByIndex(i);
        SetMaterial(i, Mesh->Material);
        ApplyTextures(Mesh);
    }
    
    MarkRenderStateDirty();
}

bool UAVVDecoder::HasSkeletonData()
{
    if (avvReader.MetaSkeleton.boneCount <= 0)
    {
        return false;
    }
    return true;
}

// Called every frame
void UAVVDecoder::Update(float DeltaTime)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoder_Update);

    if (HoloMeshLODDirty)
    {
        MarkRenderStateDirty();
        HoloMeshLODDirty = false;
    }

    // Update AVV reader via worker thread.
    GHoloMeshManager.AddWorkRequest(RegisteredGUID, -1, -1);

    if (bImmediateMode)
    {
        return;
    }

    if (DecoderState == EDecoderState::Idle)
    {
        // Check for newly requested frame
        if (RequestedState.FrameNumber > -1 &&
                 RequestedState.FrameNumber != CurrentState.FrameNumber &&
                 RequestedState.FrameNumber != PendingState.FrameNumber)
        {
            PendingState = RequestedState;
            RequestedState.Reset();
            DecodePending(true, true);
        }
    }

    if (DecoderState == EDecoderState::WaitingCPU)
    {
        DecodePending(true, false);
    }

    if (DecoderState == EDecoderState::FinishedCPU)
    {
        // Update skeletal mesh.
        if (HoloMeshSkeleton && DataCache.HasFrame(CurrentState.FrameNumber))
        {
            AVVEncodedFrame* frame = DataCache.GetFrame(CurrentState.FrameNumber);
            if (frame)
            {
                HoloMeshSkeleton->UpdateSkeleton(frame->skeleton.AVVToHoloSkeleton());
            }
        }
    }

    if (DecoderState == EDecoderState::Error)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("Error occured decoding segment."));
        DecoderState = EDecoderState::Idle;
    }
}

void UAVVDecoder::DoThreadedWork(int sequenceIndex, int frameIndex)
{
    if (!RegisteredGUID.IsValid())
    {
        return;
    }

    avvReader.Update();
}

void UAVVDecoder::UpdateDataCache()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoder_UpdateDataCache);

    // Free any stale data thats older than our current segment/frame number.
    int SegmentIndex = avvReader.GetSegmentIndex(CurrentState.FrameNumber);
    DataCache.FreeStaleData(SegmentIndex, CurrentState.FrameNumber);

    // Cache the data from the finisher reader requests.
    FAVVReaderRequestRef request = avvReader.GetFinishedRequest();
    while (request != nullptr)
    {
        if (request->segmentIndex > -1)
        {
            DataCache.AddSegment(request->segment);
            request->segment = nullptr;
        }
        if (request->frameNumber > -1)
        {
            DataCache.AddFrame(request->frame);
            request->frame = nullptr;
        }
        request = avvReader.GetFinishedRequest();
    }
}

bool UAVVDecoder::DecodePending(bool requestIfMissing, bool requestNextFrame)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoder_DecodeTexture);

    UpdateDataCache();

    int requestedSegmentIndex = avvReader.GetSegmentIndex(PendingState.FrameNumber);
    bool segmentFound = DataCache.HasSegment(requestedSegmentIndex) || (DecodedSegmentIndex == requestedSegmentIndex);
    bool frameFound = DataCache.HasFrame(PendingState.FrameNumber);
    bool requestedSegment = false;

    if (segmentFound && frameFound)
    {
        if (DecodedSegmentIndex != requestedSegmentIndex)
        {
            AVVEncodedSegment* segment = DataCache.GetSegment(requestedSegmentIndex);

            // Ensure luma texture is properly configured.
            if (segment->texture.blockCount > 0)
            {
                FHoloMesh* Mesh = GetHoloMesh(true);
                if (Mesh->bInitialized)
                {
                    ApplyTextures(Mesh, segment);
                }
            }
        }

        DecoderState = EDecoderState::FinishedCPU;
    }
    else 
    {
        if (requestIfMissing)
        {
            if (!segmentFound)
            {
                avvReader.AddRequest(requestedSegmentIndex, PendingState.FrameNumber, ShouldRequestTexture());
                DecoderState = EDecoderState::WaitingCPU;
                requestedSegment = true;
            }
            else if (!frameFound)
            {
                avvReader.AddRequest(-1, PendingState.FrameNumber, ShouldRequestTexture());
                DecoderState = EDecoderState::WaitingCPU;
            }
        }
    }

    // Request next frame(s) in advance
    if (requestNextFrame)
    {
        // If the engine is running at a low frame rate like 30 fps then missing a frame means we'll
        // be behind by one already on the next frame. If we only have one frame ahead in the cache
        // that window is very narrow. Cache ahead 2 frames instead.
        const int cacheAheadFrames = 2;

        for (int n = 1; n <= cacheAheadFrames; ++n)
        {
            int nextFrameNumber = PendingState.FrameNumber + n;
            if (nextFrameNumber >= avvReader.FrameCount)
            {
                nextFrameNumber = 0;
            }
            int nextSegmentIndex = avvReader.GetSegmentIndex(nextFrameNumber);
            if ((requestedSegment && nextSegmentIndex == requestedSegmentIndex) 
                || nextSegmentIndex == DecodedSegmentIndex || DataCache.HasSegment(nextSegmentIndex))
            {
                nextSegmentIndex = -1;
            }
            if (!DataCache.HasFrame(nextFrameNumber))
            {
                avvReader.AddRequest(nextSegmentIndex, nextFrameNumber, ShouldRequestTexture());
            }
        }
    }

    return (segmentFound && frameFound);
}

void UAVVDecoder::FreeUnusedMemory()
{
    // Called by the manager to flush out any excess memory usage.
    // This is only performed on editor meshes, otherwise all currently
    // held data for runtime meshes would be considered used.

    DataCache.Empty();
}

void UAVVDecoder::UpdateBoundingBox(AVVEncodedSegment* segment, FHoloMesh* meshOut)
{
    FHoloMeshVec3 originalMin = segment->GetAABBMin();
    FHoloMeshVec3 originalMax = segment->GetAABBMax();

    FHoloMeshVec3 finalMin = FHoloMeshVec3(originalMin.X * 100.0f, originalMin.Z * 100.0f, originalMin.Y * 100.0f);
    FHoloMeshVec3 finalMax = FHoloMeshVec3(originalMax.X * 100.0f, originalMax.Z * 100.0f, originalMax.Y * 100.0f);

    meshOut->LocalBox = FBox(finalMin, finalMax);
}

void UAVVDecoder::UploadData(FRDGBuilder& GraphBuilder, FRDGBufferRef Buffer, void* DataPtr, uint32_t SizeInBytes, AVVEncodedSegment* SourceSegment, AVVEncodedFrame* SourceFrame)
{
    if (SourceSegment != nullptr)
    {
        SourceSegment->activeUploadCount++;
    }
    if (SourceFrame != nullptr)
    {
        SourceFrame->activeUploadCount++;
    }

    HoloMeshUtilities::UploadBuffer(GraphBuilder, Buffer, DataPtr, SizeInBytes, 
        [SourceSegment, SourceFrame](const void* DataPtr)
        {
            // Data Upload Completed.

            if (SourceSegment != nullptr)
            {
                SourceSegment->activeUploadCount--;
            }
            if (SourceFrame != nullptr)
            {
                SourceFrame->activeUploadCount--;
            }
        }
    );
}

// Decode Animation
void UAVVDecoder::DecodeFrameAnimation(FRDGBuilder& GraphBuilder, AVVEncodedFrame* frame, FHoloMesh* meshOut)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoder_DecodeAnimation);
    DECLARE_GPU_STAT(AVVDecodeAnimation)
    RDG_EVENT_SCOPE(GraphBuilder, "AVVDecodeAnimation");
    RDG_GPU_STAT_SCOPE(GraphBuilder, AVVDecodeAnimation);
    RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

    if (frame == nullptr || DecodedVertexBuffer == nullptr)
    {
        return;
    }

    FRDGBuffer* AnimBuffer = nullptr;
    if (AnimDataBuffer == nullptr)
    {
        // Ensure buffer is large enough for SSDR or Delta.
        int initialBufferSize = FMath::Max(avvReader.Limits.MaxBoneCount * 16, avvReader.Limits.MaxVertexCount);
        AnimBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t), initialBufferSize), TEXT("AVVFrameAnimationData"));
        HoloMeshUtilities::ConvertToPooledBuffer(GraphBuilder, AnimBuffer, AnimDataBuffer);
    }

    // Upload latest animation data.
    if (AnimDataBuffer != nullptr)
    {
        if (AnimBuffer == nullptr)
        {
            AnimBuffer = GraphBuilder.RegisterExternalBuffer(AnimDataBuffer);
        }

        if (frame->ssdrBoneCount > 0)
        {
            UploadData(GraphBuilder, AnimBuffer, (uint8_t*)frame->ssdrMatrixData, frame->ssdrBoneCount * 16 * 4, nullptr, frame);
        } 
        else if (frame->deltaPosCount > 0)
        {
            uint8_t* data = frame->content->Data;
            UploadData(GraphBuilder, AnimBuffer, &data[frame->deltaDataOffset], frame->deltaDataSize, nullptr, frame);
        }
    }

    FRDGBuffer* VertexBuffer = GraphBuilder.RegisterExternalBuffer(DecodedVertexBuffer);
    FRDGBufferUAVRef DecodedVertexBufferUAV = GraphBuilder.CreateUAV(VertexBuffer, PF_R32G32B32A32_UINT);

    if (frame->ssdrBoneCount == 0 && frame->deltaPosCount == 0)
    {
        FAVVDecodeFrameAnim_None_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodeFrameAnim_None_CS::FParameters>();

        PassParameters->DecodedVertexBuffer      = DecodedVertexBufferUAV;
        PassParameters->VertexPositionBuffer     = meshOut->VertexBuffers->GetPositionBufferUAV();
        PassParameters->VertexPrevPositionBuffer = meshOut->VertexBuffers->GetPrevPositionBufferUAV();
        PassParameters->gVertexCount             = DecodedSegmentVertexCount;

        TShaderMapRef<FAVVDecodeFrameAnim_None_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.Animation"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector((DecodedSegmentVertexCount / 64) + 1, 1, 1)
        );
    }
    else if (frame->ssdrBoneCount > 0)
    {
        FRDGBufferUAVRef AnimDataBufferUAV = GraphBuilder.CreateUAV(AnimBuffer, PF_R32G32B32A32_UINT);

        FAVVDecodeFrameAnim_SSDR_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodeFrameAnim_SSDR_CS::FParameters>();

        PassParameters->FrameSSDRDataBuffer      = AnimDataBufferUAV;
        PassParameters->DecodedVertexBuffer      = DecodedVertexBufferUAV;
        PassParameters->VertexPositionBuffer     = meshOut->VertexBuffers->GetPositionBufferUAV();
        PassParameters->VertexPrevPositionBuffer = meshOut->VertexBuffers->GetPrevPositionBufferUAV();
        PassParameters->gVertexCount             = DecodedSegmentVertexCount;
        PassParameters->gBoneCount               = frame->ssdrBoneCount;

        TShaderMapRef<FAVVDecodeFrameAnim_SSDR_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.AnimationSSDR"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector((DecodedSegmentVertexCount / 64) + 1, 1, 1)
        );
    }
    else if (frame->deltaPosCount > 0)
    {
        FRDGBufferUAVRef AnimDataBufferUAV = GraphBuilder.CreateUAV(AnimBuffer, PF_R32_UINT);

        FAVVDecodeFrameAnim_Delta_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodeFrameAnim_Delta_CS::FParameters>();

        PassParameters->FrameDeltaDataBuffer     = AnimDataBufferUAV;
        PassParameters->DecodedVertexBuffer      = DecodedVertexBufferUAV;
        PassParameters->VertexPositionBuffer     = meshOut->VertexBuffers->GetPositionBufferUAV();
        PassParameters->VertexPrevPositionBuffer = meshOut->VertexBuffers->GetPrevPositionBufferUAV();
        PassParameters->gVertexCount             = DecodedSegmentVertexCount;
        PassParameters->gAABBMin                 = FHoloMeshVec3(frame->deltaAABBMin[0], frame->deltaAABBMin[1], frame->deltaAABBMin[2]);
        PassParameters->gAABBMax                 = FHoloMeshVec3(frame->deltaAABBMax[0], frame->deltaAABBMax[1], frame->deltaAABBMax[2]);

        TShaderMapRef<FAVVDecodeFrameAnim_Delta_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.AnimationDelta"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector((DecodedSegmentVertexCount / 64) + 1, 1, 1)
        );
    }
}

bool UAVVDecoder::ShouldRequestTexture()
{
    if (HoloMeshLOD < 2)
    {
        return true;
    }
    return false;
}

void UAVVDecoder::ApplyTextures(FHoloMesh* Mesh, AVVEncodedSegment* segment)
{
    if (bUseBC4HardwareDecoding)
    {
        if (!Mesh->BC4Texture.IsValid() && segment != nullptr)
        {
            Mesh->BC4Texture.Create(segment->texture.width, segment->texture.height, EPixelFormat::PF_BC4, 3);
            Mesh->MaskTexture.Create(segment->texture.width / 4, segment->texture.height / 4, RTF_R8, TextureFilter::TF_Nearest, true);
        }

        if (Mesh->BC4Texture.IsValid())
        {
            Mesh->Material->SetTextureParameterValue(FName("BaseTexture"), Cast<UTexture>(Mesh->BC4Texture.GetTexture()));
        }
    }
    else
    {
        if (!Mesh->LumaTexture.IsValid() && segment != nullptr)
        {
            Mesh->LumaTexture.Create(segment->texture.width, segment->texture.height, RTF_R8, TextureFilter::TF_Bilinear, true);
            Mesh->MaskTexture.Create(segment->texture.width / 4, segment->texture.height / 4, RTF_R8, TextureFilter::TF_Nearest, true);
        }

        if (Mesh->LumaTexture.IsValid())
        {
            Mesh->Material->SetTextureParameterValue(FName("BaseTexture"), Mesh->LumaTexture.GetRenderTarget());
        }
    }

    if (Mesh->MaskTexture.IsValid())
    {
        Mesh->Material->SetTextureParameterValue(FName("MaskTexture"), Mesh->MaskTexture.GetRenderTarget());
    }
}

void UAVVDecoder::ClearTextures(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment, FHoloMesh* meshOut)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoder_ClearTextures);
    SCOPED_GPU_STAT(GraphBuilder.RHICmdList, GPU_AVVDecoder_ClearTextures);
    SCOPED_GPU_MASK(GraphBuilder.RHICmdList, FRHIGPUMask::All());

    FTexture2DRHIRef RenderTargetTexture = meshOut->MaskTexture.GetRenderTargetRHI();
    bool validRT = RenderTargetTexture != nullptr && RenderTargetTexture->IsValid();

    // Clear Mask Texture Only
    if (validRT && !meshOut->MaskTexture.IsClear())
    {
        // Mip 0
        HoloMeshUtilities::ClearUAVFloat(GraphBuilder, meshOut->MaskTexture.GetRenderTargetUAV(0));

        if (segment->texture.multiRes)
        {
            if (RenderTargetTexture->GetNumMips() > 1)
            {
                // Mip 1
                HoloMeshUtilities::ClearUAVFloat(GraphBuilder, meshOut->MaskTexture.GetRenderTargetUAV(1));

                // Mip 2 
                HoloMeshUtilities::ClearUAVFloat(GraphBuilder, meshOut->MaskTexture.GetRenderTargetUAV(2));
            }
            else
            {
                UE_LOG(LogHoloSuitePlayer, Warning, TEXT("Texture Decode Error: multi-res expects multiple mips."));
            }
        }

        meshOut->MaskTexture.SetClearFlag(true);
    }
}

void UAVVDecoder::UpdateTextureBlockMap(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment)
{
    uint8_t* data = segment->content->Data;

    if (TextureBlockMapBuffer == nullptr)
    {
        FRDGBuffer* Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t), avvReader.Limits.MaxTextureBlocks), TEXT("AVVTextureBlockData"));
        UploadData(GraphBuilder, Buffer, &data[segment->texture.blockDataOffset], segment->texture.blockDataSize, segment);
        HoloMeshUtilities::ConvertToPooledBuffer(GraphBuilder, Buffer, TextureBlockMapBuffer);
    }
    else
    {
        FRDGBuffer* Buffer = GraphBuilder.RegisterExternalBuffer(TextureBlockMapBuffer);
        UploadData(GraphBuilder, Buffer, &data[segment->texture.blockDataOffset], segment->texture.blockDataSize, segment);
    }
}

void UAVVDecoder::DecodeFrameTexture(FRDGBuilder& GraphBuilder, AVVEncodedFrame* frame, FHoloMesh* meshOut)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoder_DecodeTexture);
    RDG_GPU_STAT_SCOPE(GraphBuilder, GPU_AVVDecoder_DecodeTexture);
    RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

    int textureBlockCount = DecodedSegmentTextureInfo.blockCount;
    if (textureBlockCount <= 0)
    {
        return;
    }

    if (bUseBC4HardwareDecoding)
    {
        if (!meshOut->BC4Texture.IsValid())
        {
            return;
        }
        FTexture2DRHIRef TextureRHI = meshOut->BC4Texture.GetTextureRHI();
        if (TextureRHI == nullptr || !TextureRHI.IsValid())
        {
            return;
        }
    }
    else
    {
        if (!meshOut->LumaTexture.IsValid())
        {
            return;
        }
        FTexture2DRHIRef RenderTargetTexture = meshOut->LumaTexture.GetRenderTargetRHI();
        if (RenderTargetTexture == nullptr || !RenderTargetTexture->IsValid())
        {
            return;
        }
    }

    if (!meshOut->MaskTexture.IsValid())
    {
        return;
    }
    FTexture2DRHIRef MaskTargetTexture = meshOut->MaskTexture.GetRenderTargetRHI();
    if (MaskTargetTexture == nullptr || !MaskTargetTexture.IsValid())
    {
        return;
    }

    if (TextureBlockMapBuffer == nullptr)
    {
        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("Texture block map hasn't been loaded yet."));
        return;
    }

    // LOD dictates some features.
    int LOD = GetHoloMeshLOD();

    // Decode Texture Blocks to Luma Texture
    if (frame->blockDecode && LOD < 2 && frame->lumaDataSize > 0)
    {
        uint8_t* data = frame->textureContent->Data;

        FRDGBufferRef LumaBlockDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t) * 2, frame->lumaDataSize / 8), TEXT("AVVFrameColorData"));
        FRDGBufferUAVRef LumaBlockDataBufferUAV = GraphBuilder.CreateUAV(LumaBlockDataBuffer, PF_R32G32_UINT);
        UploadData(GraphBuilder, LumaBlockDataBuffer, &data[frame->lumaDataOffset], frame->lumaDataSize, nullptr, frame);

        FRDGBuffer* BlockMapBuffer = GraphBuilder.RegisterExternalBuffer(TextureBlockMapBuffer);
        FRDGBufferUAVRef TextureBlockMapBufferUAV = GraphBuilder.CreateUAV(BlockMapBuffer, PF_R32_UINT);

        auto DecodeLevel = [&](int mipLevel, int blockCount, int blockOffset)
        {
            if (bUseBC4HardwareDecoding)
            {
                TShaderMapRef<FAVVCopyTextureBlock_BC4_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                FAVVCopyTextureBlock_BC4_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVCopyTextureBlock_BC4_CS::FParameters>();

                int divisor = 4 * FMath::Pow(2.0, mipLevel);
                FIntPoint mipSize = FIntPoint(DecodedSegmentTextureInfo.width / divisor, DecodedSegmentTextureInfo.height / divisor);

                FRDGTextureRef BC4StagingTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
                    mipSize,
                    EPixelFormat::PF_R32G32_UINT,
                    FClearValueBinding::None,
                    TexCreate_UAV),
                    TEXT("AVVBC4StagingTexture"));

                FRDGTextureUAVRef BC4StagingTextureUAV = GraphBuilder.CreateUAV(BC4StagingTexture);

                PassParameters->TextureBlockDataBuffer  = TextureBlockMapBufferUAV;
                PassParameters->LumaBlockDataBuffer     = LumaBlockDataBufferUAV;
                PassParameters->BC4StagingTextureOut    = BC4StagingTextureUAV;
                PassParameters->MaskTextureOut          = meshOut->MaskTexture.GetRenderTargetUAV(mipLevel);
                PassParameters->gBlockCount             = blockCount;
                PassParameters->gBlockOffset            = blockOffset;

                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("AVVDecoder.Texture_%d", mipLevel),
                    ERDGPassFlags::Compute,
                    ComputeShader,
                    PassParameters,
                    FIntVector((blockCount / 64) + 1, 1, 1)
                );

                // Copy the unpacked data from BC4 staging into actual BC4 texture.
                HoloMeshUtilities::CopyTexture(GraphBuilder, FIntVector(mipSize.X, mipSize.Y, 0), BC4StagingTexture, 0, meshOut->BC4Texture.GetTextureRHI(), mipLevel);
            }
            else
            {
                TShaderMapRef<FAVVDecodeTextureBlock_BC4_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
                FAVVDecodeTextureBlock_BC4_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodeTextureBlock_BC4_CS::FParameters>();

                PassParameters->TextureBlockDataBuffer  = TextureBlockMapBufferUAV;
                PassParameters->LumaBlockDataBuffer     = LumaBlockDataBufferUAV;
                PassParameters->LumaTextureOut          = meshOut->LumaTexture.GetRenderTargetUAV(mipLevel);
                PassParameters->MaskTextureOut          = meshOut->MaskTexture.GetRenderTargetUAV(mipLevel);
                PassParameters->gBlockCount             = blockCount;
                PassParameters->gBlockOffset            = blockOffset;

                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("AVVDecoder.Texture_%d", mipLevel),
                    ERDGPassFlags::Compute,
                    ComputeShader,
                    PassParameters,
                    FIntVector((blockCount / 64) + 1, 1, 1)
                );
            }
        };

        if (!DecodedSegmentTextureInfo.multiRes)
        {
            DecodeLevel(0, textureBlockCount, 0);
        }
        else
        {
            // Decode Quarter Resolution (Mip 2)
            if (DecodedSegmentTextureInfo.levelBlockCounts.size() > 2)
            {
                int blockOffset = DecodedSegmentTextureInfo.levelBlockCounts[1] + DecodedSegmentTextureInfo.levelBlockCounts[0];
                int blockCount  = DecodedSegmentTextureInfo.levelBlockCounts[2];

                DecodeLevel(2, blockCount, blockOffset);
            }

            // Render Half Resolution (Mip 1)
            if (DecodedSegmentTextureInfo.levelBlockCounts.size() > 1)
            {
                int blockOffset = DecodedSegmentTextureInfo.levelBlockCounts[0];
                int blockCount  = DecodedSegmentTextureInfo.levelBlockCounts[1];

                DecodeLevel(1, blockCount, blockOffset);
            }

            // Render Full Resolution (Mip 0)
            if (DecodedSegmentTextureInfo.levelBlockCounts.size() > 0)
            {
                int blockOffset = 0;
                int blockCount = DecodedSegmentTextureInfo.levelBlockCounts[0];

                DecodeLevel(0, blockCount, blockOffset);
            }
        }

        meshOut->MaskTexture.SetClearFlag(false);
    }
}

