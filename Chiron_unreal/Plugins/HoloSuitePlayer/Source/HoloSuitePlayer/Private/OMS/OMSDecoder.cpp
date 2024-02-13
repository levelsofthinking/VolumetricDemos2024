// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "OMS/OMSDecoder.h"
#include "OMS/OMSPlayerComponent.h"
#include "OMS/OMSUtilities.h"
#include "OMS/OMSShaders.h"

DECLARE_CYCLE_STAT(TEXT("OMSDecoder.OpenOMS"),                  STAT_OMSDecoder_OpenOMS,                STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSDecoder.Update"),                   STAT_OMSDecoder_Update,                 STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSDecoder.FlushDecodedQueue"),        STAT_OMSDecoder_FlushDecodedQueue,      STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSDecoder.RequestSequence"),          STAT_OMSDecoder_RequestSequence,        STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSDecoder.AdvanceNextSequence"),      STAT_OMSDecoder_AdvanceNextSequence,    STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSDecoder.GetSequence"),              STAT_OMSDecoder_GetSequence,            STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSDecoder.DoThreadedWork"),           STAT_OMSDecoder_DoThreadedWork,         STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSDecoder.ReadbackTextureDecode"),    STAT_OMSDecoder_ReadbackTextureDecode,  STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSDecoder.ComputeTextureDecode"),     STAT_OMSDecoder_ComputeTextureDecode,   STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSDecoder.Update_RenderThread"),      STAT_OMSDecoder_Update_RenderThread,    STATGROUP_HoloSuitePlayer);

UOMSDecoder::UOMSDecoder(const FObjectInitializer& ObjectInitializer)
    : UHoloMeshComponent(ObjectInitializer)
{
    OMSFile = nullptr;
    OMSHeader = nullptr;
    MaxBufferedSequences = -1;
    DefaultMaxBufferedSequences = 20;

    ReadFrameIdx = 0;
    WriteFrameIdx = 1;
    bNewTextureFrameReady = false;
    bFrameDecoderSelected = false;
    bUseCPUDecoder = false;
    bUseFastScrubbing = GetDefault<UHoloSuitePlayerSettings>()->FastScrubbingInEditor;
}

UOMSDecoder::~UOMSDecoder()
{
    ClearData();
    Close();
}

bool UOMSDecoder::OpenOMS(UOMSFile* NewOMSFile, UMaterialInterface* NewMeshMaterial)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSDecoder_OpenOMS);

    ClearData();

    OMSFile = NewOMSFile;
   
    FStreamableOMSData* OMSStreamableData;
    OMSStreamableData = &(FStreamableOMSData&)OMSFile->GetStreamableData();

    // Reset to zero.
    OMSHeader = new oms_header_t();
    OMSStreamableData->ReadHeaderSync(OMSHeader);

    // Build Lookup Table.
    for (uint32_t frameIndex = 0; frameIndex < OMSHeader->frame_count; frameIndex++)
    {
        std::pair<int, int> entry = std::make_pair(OMSStreamableData->FrameToSequenceIndex[frameIndex], OMSStreamableData->FrameToSequenceFrameOffset[frameIndex]);
        frameLookupTable.Add(entry);
    }

    // Validate Max Number of Buffered Sequences.
    ValidateMaxBufferedSequences();

    LoadMeshMaterial(NewMeshMaterial);

    GHoloMeshManager.Register(this, GetOwner());
    TextureDecoderState = ETextureDecoderState::Idle;
    MeshDecoderState = EMeshDecoderState::Idle;

    return true;
}

void UOMSDecoder::Close()
{
    GHoloMeshManager.ClearRequests(RegisteredGUID);
    GHoloMeshManager.Unregister(RegisteredGUID);
    RegisteredGUID.Invalidate();
}

void UOMSDecoder::Configure(UOMSPlayerComponent* NewPlayerComponent, bool NewUseCPUDecoder, int NewNumBufferedSequences)
{
    ActorComponent = NewPlayerComponent;
    MaxBufferedSequences = NewNumBufferedSequences;

    if (NewUseCPUDecoder)
    {
        bUseCPUDecoder = NewUseCPUDecoder;
    }
    else
    {
        bUseCPUDecoder = !UOMSDecoder::CheckComputeSupport();
    }
    
    ValidateMaxBufferedSequences();
}

void UOMSDecoder::ValidateMaxBufferedSequences()
{
    if (frameLookupTable.Num() == 0)
    {
        return;
    }

    int MinBufferedSequences = frameLookupTable[frameLookupTable.Num() - 1].first;

    if (MaxBufferedSequences < 1)
    {
        MinBufferedSequences = MinBufferedSequences < DefaultMaxBufferedSequences ? MinBufferedSequences : DefaultMaxBufferedSequences;
        MaxBufferedSequences = MinBufferedSequences;
        return;
    }

    if (MaxBufferedSequences > MinBufferedSequences)
    {
        MaxBufferedSequences = MinBufferedSequences;
        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSDecoder: invalid number of sequences to pre-load. Set to %d."), MaxBufferedSequences);
    }
}

void UOMSDecoder::LoadMeshMaterial(UMaterialInterface* NewMeshMaterial)
{
    HoloMeshMaterial = UHoloMeshMaterial::Create(NewMeshMaterial, GetTransientPackage());

    for (int i = 0; i < 2; ++i)
    {
        HoloMesh[i].Material = HoloMeshMaterial->GetMaterialByIndex(i);

#if PLATFORM_ANDROID
        bool usingVulkan = FHardwareInfo::GetHardwareInfo(NAME_RHI) == "Vulkan";
        if (!usingVulkan && !GEngine->GameUserSettings->SupportsHDRDisplayOutput())
        {
            if (HoloMesh[i].Material != nullptr)
            {
                HoloMesh[i].Material->SetScalarParameterValue(FName("flipTextureY"), 1.0);
            }
        }
#endif

        SetMaterial(i, HoloMesh[i].Material);
    }
}

void UOMSDecoder::Update()
{
    SCOPE_CYCLE_COUNTER(STAT_OMSDecoder_Update);

    // Texture Decoding

    if (TextureDecoderState == ETextureDecoderState::Error)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("An error occured during OMS texture decoding. Resetting decoder."));
        TextureDecoderState = ETextureDecoderState::Idle;
    }

    if (TextureDecoderState == ETextureDecoderState::Waiting)
    {
        GHoloMeshManager.AddUpdateRequest(RegisteredGUID, -1, -1, -1);
    }

    // Mesh Decoding

    if (MeshDecoderState == EMeshDecoderState::Error)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("An error occured during OMS mesh decoding. Resetting decoder."));
        MeshDecoderState = EMeshDecoderState::Idle;
    }

    if (MeshDecoderState == EMeshDecoderState::Idle)
    {
        FlushDecodedQueue();

        if (nextDecodedSequence > -1)
        {
            //UE_LOG(LogHoloSuitePlayer, Warning, TEXT("Requesting Work for Segment: %d"), nextDecodedSequence.load());
            GHoloMeshManager.AddWorkRequest(RegisteredGUID, nextDecodedSequence, -1);
            MeshDecoderState = EMeshDecoderState::Waiting;

            AdvanceNextSequence();
        }
    }
}

void UOMSDecoder::UpdateMeshMaterial(bool write, bool frameTexture, bool boneTexture, bool retarget, bool ssdr, float ssdrEnabled)
{
    int index = write ? WriteIndex : ReadIndex;

    if (!HoloMesh[index].Material)
    {
        return;
    }

    if (frameTexture)
    {
        HoloMesh[index].Material->SetTextureParameterValue(FName("BaseTexture"), GetFrame().second);
        return;
    }

    if (boneTexture)
    {
        HoloMesh[index].Material->SetTextureParameterValue(FName("SSDRBoneTexture"), Cast<UTexture>(HoloMesh[index].SSDRBoneTexture.GetTexture()));
        return;
    }

    if (retarget)
    {
        HoloMesh[index].Material->SetTextureParameterValue(FName("RetargetBoneTexture"), HoloMesh[index].RetargetBoneTexture.GetTexture());
        HoloMesh[index].Material->SetScalarParameterValue("RetargetingEnabled", 1.0f);
        return;
    }

    if (ssdr)
    {
        HoloMesh[index].Material->SetScalarParameterValue("SSDREnabled", ssdrEnabled);
        return;
    }
}

void UOMSDecoder::ClearData()
{
    if (OMSHeader != nullptr)
    {
        oms_free_header(OMSHeader);
        delete OMSHeader;
        OMSHeader = nullptr;
    }
    
    decodedQueue.Empty();
    freeQueue.Empty();
    decodedSequences.Empty();
    frameLookupTable.Empty();

    OMSFile = nullptr;
}

int UOMSDecoder::GetFrameCount()
{
    if (frameLookupTable.Num() == 0)
    {
        return -1;
    }
    return frameLookupTable.Num();
}

std::pair<int, int> UOMSDecoder::GetFrameFromLookupTable(int contentFrameNumber)
{
    if (contentFrameNumber < 0 || contentFrameNumber >= frameLookupTable.Num())
    {
        return std::make_pair(-1, -1);
    }
    return frameLookupTable[contentFrameNumber];
}

void UOMSDecoder::FlushDecodedQueue()
{
    SCOPE_CYCLE_COUNTER(STAT_OMSDecoder_FlushDecodedQueue);

    while (!decodedQueue.IsEmpty())
    {
        FDecodedOMSSequenceRef sequenceData = *decodedQueue.Peek();
        decodedSequences.Add(sequenceData);
        decodedQueue.Pop();
    }
}

void UOMSDecoder::RequestSequence(int index)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSDecoder_RequestSequence);

    for (int i = 0; i < decodedSequences.Num(); ++i)
    {
        if (decodedSequences[i]->sequenceIndex == index)
        {
            // Already decoded.
            return;
        }
    }

    nextDecodedSequence = index;
    Update();
}

void UOMSDecoder::AdvanceNextSequence()
{
    SCOPE_CYCLE_COUNTER(STAT_OMSDecoder_AdvanceNextSequence);

    if (decodedSequences.Num() <= MaxBufferedSequences)
    {
        nextDecodedSequence++;
        if (nextDecodedSequence >= OMSHeader->sequence_count)
        {
            nextDecodedSequence = 0;
        }
    }
    else
    {
        nextDecodedSequence = -1;
    }
}

FDecodedOMSSequenceRef UOMSDecoder::GetSequence(int index, bool waitForSequence)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSDecoder_GetSequence);
    //double start = FPlatformTime::Seconds();

    FDecodedOMSSequenceRef result = nullptr;
    TArray<FDecodedOMSSequenceRef> newDecodedSequences;

    // Find the requested sequence if its in the list and also free any sequences
    // we may have jumped over which are no longer relevant.
    for (int i = 0; i < decodedSequences.Num(); ++i)
    {
        if (lastDecodedSequence < index)
        {
            // Decoder has looped
            if (decodedSequences[i]->sequenceIndex > lastDecodedSequence && decodedSequences[i]->sequenceIndex < index)
            {
                freeQueue.Enqueue(decodedSequences[i]);
                continue;
            }
        }
        else 
        {
            if (decodedSequences[i]->sequenceIndex < index)
            {
                freeQueue.Enqueue(decodedSequences[i]);
                continue;
            }
        }
        
        if (decodedSequences[i]->sequenceIndex == index)
        {
            result = decodedSequences[i];
            continue;
        }

        newDecodedSequences.Add(decodedSequences[i]);
    }

    // Update the decoded sequences array and advance the decoder if we need to.
    decodedSequences = newDecodedSequences;
    if (nextDecodedSequence == -1 && decodedSequences.Num() < MaxBufferedSequences)
    {
        nextDecodedSequence = lastDecodedSequence.load();
        AdvanceNextSequence();
    }

    if (!result.IsValid() && waitForSequence)
    {
        // Block until sequence is ready.
        while (true)
        {
            for (int i = 0; i < decodedSequences.Num(); ++i)
            {
                if (decodedSequences[i]->sequenceIndex == index)
                {
                    return decodedSequences[i];
                }
            }

            nextDecodedSequence = index;
            Update();
        }
    }

    //double end = FPlatformTime::Seconds();
    //UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSDecoder::GetSegment %d Time: %f"), index, ((end - start) * 1000.0f));

    return result;
}

// Read and decode requested OMS sequence from a worker thread.
void UOMSDecoder::DoThreadedWork(int sequenceIndex, int frameIndex)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSDecoder_DoThreadedWork);

    // Empty the free queue.
    freeQueue.Empty();

    if (sequenceIndex >= OMSHeader->sequence_count)
    {
        MeshDecoderState = EMeshDecoderState::Idle;
        return;
    }

    FDecodedOMSSequenceRef decodedSequence = MakeShareable(new FDecodedOMSSequence());
    decodedSequence->sequenceIndex = sequenceIndex;
    decodedSequence->sequence = oms_alloc_sequence(0, 0, 0, 0, 0, 0, 0);
    decodedSequence->holoMesh = new FHoloMesh();

    oms_sequence_t* sequence = decodedSequence->sequence;
    FHoloMesh* meshOut = decodedSequence->holoMesh;

    FStreamableOMSData* OMSStreamableData = &(FStreamableOMSData&)OMSFile->GetStreamableData();
    OMSStreamableData->Chunks[sequenceIndex].ReadSequenceSync(OMSHeader, sequence);

    bool includeRetargetData = OMSHeader->has_retarget_data; // TODO: check a decoder flag if retarget is enabled

    // We round up to the nearest 65k in the case of vertices and 60k in indices. This means the vast
    // majority of the vertex buffer and index buffer sizes will be the same between the sequences.
    // This allows an optimization of easily reusing the existing allocated gpu buffers.
    int roundedVertexCount = ((sequence->vertex_count / (UINT16_MAX + 1)) + 1) * (UINT16_MAX + 1);
    int roundedIndexCount = ((sequence->index_count / 60000) + 1) * 60000;

    meshOut->VertexBuffers->Create(roundedVertexCount, 7);
    bool use32Bit = sequence->vertex_count > (UINT16_MAX + 1);
    meshOut->IndexBuffer->Create(roundedIndexCount, use32Bit);

    auto PositionData = meshOut->VertexBuffers->GetPositionData();
    FPositionVertex* Positions = (FPositionVertex*)PositionData->GetDataPointer();

    // We always used packed normals
    auto TangentData = meshOut->VertexBuffers->GetTangentsData();
    FPackedNormal* Tangents = (FPackedNormal*)TangentData->GetDataPointer();

    // We always use low precision half vectors
    auto TexCoordData = meshOut->VertexBuffers->GetTexCoordData();
    FVector2DHalf* TexCoords = (FVector2DHalf*)(TexCoordData->GetDataPointer());
    uint32 numTex = meshOut->VertexBuffers->GetNumTexCoords();

    // Note: y/z swap and scaling is performed below.

    // Bounding Box
    {
        FVector min = FVector(sequence->aabb.min.x * 100.0f, sequence->aabb.min.z * 100.0f, sequence->aabb.min.y * 100.0f);
        FVector max = FVector(sequence->aabb.max.x * 100.0f, sequence->aabb.max.z * 100.0f, sequence->aabb.max.y * 100.0f);
        meshOut->LocalBox = FBox(min, max);
    }

    // Vertices
    for (int i = 0; i < sequence->vertex_count; ++i)
    {
        Positions[i].Position = FHoloMeshVec3(sequence->vertices[i].x * 100.0f, sequence->vertices[i].z * 100.0f, sequence->vertices[i].y * 100.0f);

        if (i < sequence->uv_count)
        {
            TexCoords[(i * numTex) + 0] = FVector2DHalf(sequence->uvs[i].x, sequence->uvs[i].y);
        }

        if (sequence->normal_count > 0)
        {
            Tangents[(i * 2) + 0] = FPackedNormal(FHoloMeshVec4(1.0f, 0.0f, 0.0f, 1.0f));
            Tangents[(i * 2) + 1] = FPackedNormal(FHoloMeshVec4(sequence->normals[i].x, sequence->normals[i].z, sequence->normals[i].y, 1.0f));
        }
        else
        {
            Tangents[(i * 2) + 0] = FPackedNormal(FHoloMeshVec4(1.0f, 0.0f, 0.0f, 1.0f));
            Tangents[(i * 2) + 1] = FPackedNormal(FHoloMeshVec4(0.0f, 0.0f, 1.0f, 1.0f));
        }

        // SSDR Data
        if (sequence->ssdr_frame_count > 1 && sequence->ssdr_bone_count > 0)
        {
            TexCoords[(i * numTex) + 1] = FVector2DHalf(sequence->ssdr_bone_weights[i].x, sequence->ssdr_bone_weights[i].y);
            TexCoords[(i * numTex) + 2] = FVector2DHalf(sequence->ssdr_bone_weights[i].z, sequence->ssdr_bone_weights[i].w);
            TexCoords[(i * numTex) + 3] = FVector2DHalf(sequence->ssdr_bone_indices[i].x, sequence->ssdr_bone_indices[i].y);
            TexCoords[(i * numTex) + 4] = FVector2DHalf(sequence->ssdr_bone_indices[i].z, sequence->ssdr_bone_indices[i].w);
        }
        else
        {
            TexCoords[(i * numTex) + 1] = FVector2DHalf(0.0f, 0.0f);
            TexCoords[(i * numTex) + 2] = FVector2DHalf(0.0f, 0.0f);
            TexCoords[(i * numTex) + 3] = FVector2DHalf(0.0f, 0.0f);
            TexCoords[(i * numTex) + 4] = FVector2DHalf(0.0f, 0.0f);
        }
    }

    // Triangles
    FHoloMeshIndexBuffer::IndexWriter Indices(meshOut->IndexBuffer);
    if (sequence->vertex_count > (UINT16_MAX + 1))
    {
        uint32_t* indices = (uint32_t*)sequence->indices;
        Indices.Write(indices, sequence->index_count);
    }
    else {
        uint16_t* indices = (uint16_t*)sequence->indices;
        Indices.Write(indices, sequence->index_count);
    }

    // Write zeros into the unused index spots.
    Indices.Zero(sequence->index_count, roundedIndexCount - sequence->index_count);

    // Retargeting
    if (includeRetargetData)
    {
        FColorVertexData* colorData = meshOut->VertexBuffers->GetColorData();
        FColor* Colors = (FColor*)colorData->GetDataPointer();

        for (int i = 0; i < sequence->vertex_count; ++i)
        {
            Colors[i].R = sequence->retarget_data.weights[i].x * 255;
            Colors[i].G = sequence->retarget_data.weights[i].y * 255;
            Colors[i].B = sequence->retarget_data.weights[i].z * 255;
            Colors[i].A = sequence->retarget_data.weights[i].w * 255;

            TexCoords[(i * numTex) + 5] = FVector2DHalf(sequence->retarget_data.indices[i].x, sequence->retarget_data.indices[i].y);
            TexCoords[(i * numTex) + 6] = FVector2DHalf(sequence->retarget_data.indices[i].z, sequence->retarget_data.indices[i].w);
        }
    }

    // Enqueue the decoded sequence.
    decodedQueue.Enqueue(decodedSequence);

    lastDecodedSequence = sequenceIndex;
    MeshDecoderState = EMeshDecoderState::Idle;
}

bool UOMSDecoder::CheckComputeSupport()
{
#if ENGINE_MAJOR_VERSION > 4
    TShaderMapRef<FDecodeFrameNumberCS> decodeFrameNumberCS(GetGlobalShaderMap(GMaxRHIFeatureLevel)); // GMaxRHIFeatureLevel == ERHIFeatureLevel::SM5 / ES3_1 / etc.
    return decodeFrameNumberCS.IsValid();
# else 
    return false;
#endif
}

void UOMSDecoder::DecodeFrameNumber()
{
    if (!ActorComponent || ActorComponent && !ActorComponent->MediaPlayerMaterial)
    {
        return;
    }

    if (TextureDecoderState != ETextureDecoderState::Idle)
    {
        return;
    }

    if (!bFrameDecoderSelected)
    {
        if (!bUseCPUDecoder)
        {
            bUseCPUDecoder = !UOMSDecoder::CheckComputeSupport();
        }
        bFrameDecoderSelected = true;
    }

    UWorld* world = ActorComponent->GetWorld();
    UMaterialInterface* MPMaterial = ActorComponent->MediaPlayerMaterial;

    bUseFastScrubbing = GetDefault<UHoloSuitePlayerSettings>()->FastScrubbingInEditor;
    if (bUseFastScrubbing)
    {
        if (world == nullptr || !world->bBegunPlay)
        {
            FastScrubbingTextureDecode();
            return;
        }
        bUseFastScrubbing = false; // if fast scrubbing is enabled but fast scrubbing frame decode isn't executed (i.e. the game has begun), then the actor needs to know that the texture will be available on the rendertarget or on the cachedframetexture
    }

    if (bUseCPUDecoder)
    {
        ReadbackTextureDecode(MPMaterial);
        return;
    }

    ComputeTextureDecode(MPMaterial);
}

int UOMSDecoder::GetNewFrameNumber()
{
    if (bNewTextureFrameReady)
    {
        return DecodedTextureFrames[WriteFrameIdx].FrameNumber;
    }

    return DecodedTextureFrames[ReadFrameIdx].FrameNumber;
}

std::pair<uint32_t, UTexture*> UOMSDecoder::GetFrame()
{
    if (bUseFastScrubbing)
    {
        TArrayView<UObject* const> MaterialTextures;
        MaterialTextures = ActorComponent->MediaPlayerMaterial->GetReferencedTextures();
        return std::pair<uint32_t, UTexture*>(DecodedTextureFrames[ReadFrameIdx].FrameNumber, Cast<UTexture>(MaterialTextures[0]));
    }

    // We use WriteFrameIdx because the swap is done afterwards
    FDecodedOMSTextureFrame* ReadFrame = &DecodedTextureFrames[WriteFrameIdx];
    if (bNewTextureFrameReady)
    {
        if (ReadFrame->FrameNumber < frameLookupTable.Num())
        {
            ReadFrameIdx = WriteFrameIdx;
            WriteFrameIdx++;
            if (WriteFrameIdx >= OMS_TEXTURE_FRAME_COUNT)
            {
                WriteFrameIdx = 0;
            }
        }
        else
        {
            // Print warning only once
            // Note: this may never be printed, but it's simpler than tracking if it has already been printed with aux vars.
            if(ReadFrame->FrameNumber == frameLookupTable.Num())
            {
                UE_LOG(LogHoloSuitePlayer, Error, TEXT("OMSDecoder: Length of texture source is higher than length of OMS source. Please make sure you are assigning the correct source files or re-export your volumetric files from HoloEdit."));
            }
        }
        bNewTextureFrameReady = false;
        TextureDecoderState = ETextureDecoderState::Idle;
    }

    return std::pair<uint32_t, UTexture*>(ReadFrame->FrameNumber, ReadFrame->Texture);
}

// Fast scrubbing will skip reading the frame number from the video and estimate the 
// frame number based on current time in playback. This is inaccurate but very fast and stable.
void UOMSDecoder::FastScrubbingTextureDecode()
{
    DecodedTextureFrames[ReadFrameIdx].FrameNumber = 0;
    if (ActorComponent->MediaPlayer != nullptr)
    {
        DecodedTextureFrames[ReadFrameIdx].FrameNumber = (int)(ActorComponent->MediaPlayer->GetTime().GetTotalSeconds() * ActorComponent->FrameRate);
    }
}

void UOMSDecoder::ReadbackTextureDecode(UMaterialInterface* sourceMaterial)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSDecoder_ReadbackTextureDecode);

    FDecodedOMSTextureFrame* WriteFrame = &DecodedTextureFrames[WriteFrameIdx];

    if (sourceMaterial != nullptr)
    {
        bool bFrameSizeSet = false;
        if (ActorComponent->MediaPlayer != nullptr)
        {
            FIntPoint videoDimensions = ActorComponent->MediaPlayer->GetVideoTrackDimensions(INDEX_NONE, INDEX_NONE);

            if (videoDimensions.X > 100 && videoDimensions.Y > 100)
            {
                WriteFrame->TextureSize = videoDimensions;
                bFrameSizeSet = true;
            }
        }

        if (!bFrameSizeSet && ActorComponent->MediaPlayerMaterial != nullptr)
        {
            UTexture* mediaPlayerTexture = OMSUtilities::GetMediaPlayerTexture(ActorComponent->MediaPlayerMaterial);
            if (mediaPlayerTexture != nullptr)
            {
                int surfaceWidth = mediaPlayerTexture->GetSurfaceWidth();
                int surfaceHeight = mediaPlayerTexture->GetSurfaceHeight();

                if (surfaceWidth > 100 && surfaceHeight > 100)
                {
                    WriteFrame->TextureSize = FIntPoint(surfaceWidth, surfaceHeight);
                }
            }
        }

        if (WriteFrame->Texture == nullptr || (((UTextureRenderTarget2D*)(WriteFrame->Texture))->SizeX != WriteFrame->TextureSize.X || ((UTextureRenderTarget2D*)(WriteFrame->Texture))->SizeY != WriteFrame->TextureSize.Y))
        {
            if (WriteFrame->TextureSize.X > 100.0f && WriteFrame->TextureSize.Y > 100.0f)
            {
                if (WriteFrame->Texture)
                {
                    // Allow garbage collection to clean up the old texture.
                    WriteFrame->Texture->RemoveFromRoot();
                }
#if ENGINE_MAJOR_VERSION == 4
                WriteFrame->Texture = NewObject<UTextureRenderTarget2D>();
                WriteFrame->Texture->AddToRoot();
                ((UTextureRenderTarget2D*)(WriteFrame->Texture))->InitAutoFormat(WriteFrame->TextureSize.X, WriteFrame->TextureSize.Y);
                ((UTextureRenderTarget2D*)(WriteFrame->Texture))->UpdateResourceImmediate();
#else
                if (WriteFrame->SourceTexture)
                {
                    FTexture2DRHIRef inputTexRef = OMS_GET_TEXREF(WriteFrame->SourceTexture);
                    WriteFrame->Texture = UTexture2D::CreateTransient(WriteFrame->TextureSize.X, WriteFrame->TextureSize.Y, inputTexRef->GetFormat());
                    WriteFrame->Texture->AddToRoot();
                    WriteFrame->Texture->UpdateResource();
                }
#endif
            }
        }

        UKismetRenderingLibrary::DrawMaterialToRenderTarget(ActorComponent, ((UTextureRenderTarget2D*)(WriteFrame->Texture)), sourceMaterial);
    }

    if (WriteFrame->Texture != nullptr)
    {
        FTextureRenderTarget2DResource* textureResource = OMS_GET_RESOURCE((FTextureRenderTarget2DResource*)WriteFrame->Texture);
        if (textureResource == nullptr)
        {
            WriteFrame->FrameNumber = 0;
            bNewTextureFrameReady = true;
            return;
        }

        TArray<FColor> pixels;

        int xReadStart = textureResource->GetSizeXY().X - 100;
        int yReadStart = textureResource->GetSizeXY().Y - 4;
        int xReadEnd = textureResource->GetSizeXY().X - 2;
        int yReadEnd = textureResource->GetSizeXY().Y - 2;

#if PLATFORM_ANDROID
        if (!OMSUtilities::IsMobileHDREnabled())
        {
            xReadStart = textureResource->GetSizeXY().X - 100;
            yReadStart = 2;
            xReadEnd = xReadStart + 96;
            yReadEnd = yReadStart + 2;
        }
#endif

        if (textureResource->ReadPixels(pixels, FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX), FIntRect(xReadStart, yReadStart, xReadEnd, yReadEnd)))
        {
            unsigned char* pixelData = new unsigned char[96 * 4];

            for (int i = 0; i < 96; ++i)
            {
                pixelData[(i * 4) + 0] = pixels[i].R;
                pixelData[(i * 4) + 1] = pixels[i].G;
                pixelData[(i * 4) + 2] = pixels[i].B;
                pixelData[(i * 4) + 3] = pixels[i].A;
            }

            WriteFrame->FrameNumber = OMSUtilities::DecodeBinaryPixels(pixelData);
            delete[] pixelData;
            bNewTextureFrameReady = true;
            return;
        }
    }

    WriteFrame->FrameNumber = -1;
}

void UOMSDecoder::ComputeTextureDecode(UMaterialInterface* sourceMaterial)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSDecoder_ComputeTextureDecode);

    UTexture* InputTexture = OMSUtilities::GetMediaPlayerTexture(sourceMaterial);
    if (!InputTexture || !OMS_GET_RESOURCE(InputTexture) || !OMS_GET_RESOURCE(InputTexture)->TextureRHI)
    {
        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSDecoder: Texture decoding failed, no valid InputTexture."));
        return;
    }

    FDecodedOMSTextureFrame* WriteFrame = &DecodedTextureFrames[WriteFrameIdx];
    if (!WriteFrame->FrameNumberReadback.IsValid())
    {
        WriteFrame->FrameNumberReadback = MakeShared<FRHIGPUBufferReadback>(*FString::Printf(TEXT("OMSReadback_%02d"), WriteFrameIdx));
    }

    // Ensure we have a cached frame texture ready to copy to.
    FTexture2DRHIRef inputTexRef = OMS_GET_TEXREF(InputTexture);
    if (!WriteFrame->Texture || WriteFrame->TextureSize != inputTexRef->GetSizeXY() || WriteFrame->TextureFormat != inputTexRef->GetFormat())
    {
        if (WriteFrame->Texture)
        {
            // Allow garbage collection to clean up the old texture.
            WriteFrame->Texture->RemoveFromRoot();
            WriteFrame->Texture = nullptr;
        }

        WriteFrame->Texture = UTexture2D::CreateTransient(inputTexRef->GetSizeXY().X, inputTexRef->GetSizeXY().Y, inputTexRef->GetFormat());
        if (!WriteFrame->Texture)
        {
            UE_LOG(LogHoloSuitePlayer, Error, TEXT("Failed to allocate Texture: %d %d %d"), inputTexRef->GetSizeXY().X, inputTexRef->GetSizeXY().Y, inputTexRef->GetFormat());
            return;
        }

        // Important: without AddToRoot() the texture will get garbage collected.
        WriteFrame->Texture->AddToRoot();
        WriteFrame->Texture->UpdateResource();
        WriteFrame->TextureSize = inputTexRef->GetSizeXY();
        WriteFrame->TextureFormat = inputTexRef->GetFormat();
    }

    WriteFrame->FrameNumber = -1;
    WriteFrame->SourceTexture = InputTexture;

    TextureDecoderState = ETextureDecoderState::Reading;
    GHoloMeshManager.AddUpdateRequest(RegisteredGUID, -1, -1, -1);
}

void UOMSDecoder::Update_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest UpdateRequest)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSDecoder_Update_RenderThread);

    if (TextureDecoderState == ETextureDecoderState::Waiting)
    {
        FDecodedOMSTextureFrame* WriteFrame = &DecodedTextureFrames[WriteFrameIdx];
        if (WriteFrame->FrameNumberReadback->IsReady())
        {
            uint32_t* data = (uint32_t*)WriteFrame->FrameNumberReadback->Lock(16);
            WriteFrame->FrameNumber = data[0];
            WriteFrame->FrameNumberReadback->Unlock();
            bNewTextureFrameReady = true;

            //UE_LOG(LogHoloSuitePlayer, Display, TEXT("OMSDecoder: Decoded Texture Frame Number %d"), WriteFrame->FrameNumber);
        }
        else 
        {
            WriteFrame->FrameNumberReadbackTimeout++;
        }

        if (WriteFrame->FrameNumberReadbackTimeout >= 3)
        {
            UE_LOG(LogHoloSuitePlayer, Error, TEXT("OMSDecoder: Frame number readback timed out after waiting 3 frames."));
            TextureDecoderState = ETextureDecoderState::Idle;
            return;
        }
    }

    if (TextureDecoderState != ETextureDecoderState::Reading)
    {
        return;
    }

    FDecodedOMSTextureFrame* WriteFrame = &DecodedTextureFrames[WriteFrameIdx];

    if (!IsValid(WriteFrame->SourceTexture) || !IsValid(WriteFrame->Texture) || WriteFrame->TextureSize.X < 100.0f || WriteFrame->TextureSize.Y < 100.0f)
    {
        TextureDecoderState = ETextureDecoderState::Idle;
        return;
    }

    FTexture2DRHIRef InputTextureRef = OMS_GET_TEXREF(WriteFrame->SourceTexture);
    FTexture2DRHIRef WriteTextureRef = OMS_GET_TEXREF(WriteFrame->Texture);

    if (!InputTextureRef || !WriteTextureRef)
    {
        TextureDecoderState = ETextureDecoderState::Error;
        return;
    }

#if (ENGINE_MAJOR_VERSION == 5)

    FRDGTextureRef SrcTextureRef = RegisterExternalTexture(GraphBuilder, InputTextureRef, TEXT("OMSDecoderSrcTexture"));
    FRDGTextureRef DestTextureRef = RegisterExternalTexture(GraphBuilder, WriteTextureRef, TEXT("OMSDecoderDestTexture"));

    // Copy Texture 
    HoloMeshUtilities::CopyTexture(GraphBuilder, FIntVector(WriteFrame->TextureSize.X, WriteFrame->TextureSize.Y, 1), SrcTextureRef, FIntVector::ZeroValue, DestTextureRef, WriteTextureRef, FIntVector::ZeroValue);

    // Read Frame Number
    {
        uint32_t frameNumberInputData[4];
        frameNumberInputData[0] = 0; // Output frame number.
        frameNumberInputData[1] = WriteFrame->TextureSize.X;
        frameNumberInputData[2] = WriteFrame->TextureSize.Y;
        frameNumberInputData[3] = GFrameNumberRenderThread;

        FRDGTextureSRVRef FrameTextureSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(DestTextureRef, 0));

        FRDGBufferRef FrameNumberBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t), 4), TEXT("OMSFrameNumberBuffer"));
        FRDGBufferUAVRef FrameNumberBufferUAV = GraphBuilder.CreateUAV(FrameNumberBuffer, PF_R32_UINT);
        HoloMeshUtilities::UploadBuffer(GraphBuilder, FrameNumberBuffer, &frameNumberInputData, 16, ERDGInitialDataFlags::None);

        {
            TShaderMapRef<FDecodeFrameNumberCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            FDecodeFrameNumberCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDecodeFrameNumberCS::FParameters>();

            PassParameters->InputTexture = FrameTextureSRV;
            PassParameters->FrameNumberBuffer = FrameNumberBufferUAV;

            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("OMSDecoder.DecodeFrameNumber"),
                ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
                ComputeShader,
                PassParameters,
                FIntVector(1, 1, 1)
            );
        }

        AddEnqueueCopyPass(GraphBuilder, WriteFrame->FrameNumberReadback.Get(), FrameNumberBuffer, 16);
        WriteFrame->FrameNumberReadbackTimeout = 0;
    }
#endif

    TextureDecoderState = ETextureDecoderState::Waiting;
}

void UOMSDecoder::RequestCulled_RenderThread(FHoloMeshUpdateRequest request)
{
    TextureDecoderState = ETextureDecoderState::Idle;
}

void UOMSDecoder::FreeUnusedMemory()
{
    decodedQueue.Empty();
    freeQueue.Empty();
    decodedSequences.Empty();
}