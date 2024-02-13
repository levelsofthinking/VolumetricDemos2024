// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "AVV/AVVDecoderCPU.h"

DECLARE_CYCLE_STAT(TEXT("AVVDecoderCPU.InitDecoder"),                   STAT_AVVDecoderCPU_InitDecoder,                  STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCPU.Close"),                         STAT_AVVDecoderCPU_Close,                        STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCPU.Update"),                        STAT_AVVDecoderCPU_Update,                       STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCPU.Update_RenderThread"),           STAT_AVVDecoderCPU_Update_RenderThread,          STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCPU.DecodeMesh"),                    STAT_AVVDecoderCPU_DecodeMesh,                   STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCPU.CPUDecodeFrameColors"),          STAT_AVVDecoderCPU_CPUDecodeFrameColors,         STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCPU.CPUDecodeFrameColorsNormals"),   STAT_AVVDecoderCPU_CPUDecodeFrameColorsNormals,  STATGROUP_HoloSuitePlayer);

#define AVV_MESH_COUNT 2

// Sets default values
UAVVDecoderCPU::UAVVDecoderCPU(const FObjectInitializer& ObjectInitializer)
    : UAVVDecoder(ObjectInitializer)
{

}

void UAVVDecoderCPU::InitDecoder(UMaterialInterface* NewMeshMaterial)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCPU_InitDecoder);

    UAVVDecoder::InitDecoder(NewMeshMaterial);

    if (!bInitialized)
    {
        HoloMeshMaterial = UHoloMeshMaterial::Create(NewMeshMaterial, GetTransientPackage());
        HoloMeshMaterial->AddToRoot();

        for (int i = 0; i < AVV_MESH_COUNT; ++i)
        {
            HoloMesh[i].VertexBuffers->Create(avvReader.Limits.MaxVertexCount, 1, true);
            HoloMesh[i].IndexBuffer->Create(avvReader.Limits.MaxIndexCount, false, true);
            HoloMesh[i].LocalBox += FVector(-100.0f, -100.0f, -100.0f);
            HoloMesh[i].LocalBox += FVector(100.0f, 100.0f, 100.0f);

            ERHIFeatureLevel::Type FeatureLevel;
            if (GetWorld())
            {
                FeatureLevel = GetWorld()->Scene->GetFeatureLevel();
            }
            else
            {
                FeatureLevel = ERHIFeatureLevel::ES3_1; // lowest feature level to ensure it still works when World isn't created yet (e.g. in child BPs)
            }
            HoloMesh[i].InitOrUpdate(FeatureLevel);

            HoloMesh[i].Material = HoloMeshMaterial->GetMaterialByIndex(i);
            SetMaterial(i, HoloMesh[i].Material);
        }

        UpdateHoloMesh();

        if (DecodedVertexData != nullptr)
        {
            delete[] DecodedVertexData;
            DecodedVertexData = nullptr;
        }

        DecodedVertexData = new uint8_t[avvReader.Limits.MaxVertexCount * 32];
        DecodedSegmentIndex = -1;

        bInitialized = true;
    }
}

void UAVVDecoderCPU::Close()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCPU_Close);

    UAVVDecoder::Close();

    if (DecodedVertexData != nullptr)
    {
        delete[] DecodedVertexData;
        DecodedVertexData = nullptr;
    }
}

void UAVVDecoderCPU::Update(float DeltaTime)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCPU_Update);

    UAVVDecoder::Update(DeltaTime);

    if (bImmediateMode)
    {
        return;
    }

    if (DecoderState == EDecoderState::FinishedCPU)
    {
        int holoMeshIndex = ReadIndex;

        int pendingSegment = avvReader.GetSegmentIndex(PendingState.FrameNumber);
        bool updatedSegment = pendingSegment != DecodedSegmentIndex;
        if (updatedSegment)
        {
            holoMeshIndex = WriteIndex;
        }

        GHoloMeshManager.AddUpdateRequest(RegisteredGUID, holoMeshIndex, pendingSegment, PendingState.FrameNumber);

        CurrentState = PendingState;
        PendingState.Reset();

        // Update bounding box.
        if (updatedSegment)
        {
            AVVEncodedSegment* segment = DataCache.GetSegment(pendingSegment);
            FHoloMesh* mesh = GetHoloMesh(holoMeshIndex);
            UpdateBoundingBox(segment, mesh);
        }

        DecoderState = EDecoderState::WaitingGPU;
    }

    if (DecoderState == EDecoderState::FinishedGPU)
    {
        if (RequiresSwap)
        {
            SwapHoloMesh();
            RequiresSwap = false;
        }
        DecoderState = EDecoderState::Idle;
    }
}

// Render Thread Update
void UAVVDecoderCPU::Update_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest UpdateRequest)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCPU_Update_RenderThread);

    FHoloMesh* mesh = GetHoloMesh(UpdateRequest.HoloMeshIndex);

    AVVEncodedSegment* segment = nullptr;
    AVVEncodedFrame* frame = nullptr;

    if (DecodedSegmentIndex != UpdateRequest.SegmentIndex)
    {
        DataCache.GetSegmentAndFrame(UpdateRequest.SegmentIndex, UpdateRequest.FrameIndex, &segment, &frame);
    }
    else
    {
        frame = DataCache.GetFrame(UpdateRequest.FrameIndex);
    }

    if (mesh == nullptr || (segment == nullptr && frame == nullptr))
    {
        DecoderState = EDecoderState::Error;
        return;
    }

    // Sequence Update
    if (segment != nullptr)
    {
        CPUDecodeMesh(mesh, segment);

        // Vertex Data
        if (DecodedVertexBuffer == nullptr)
        {
            FRDGBuffer* Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t) * 4, avvReader.Limits.MaxVertexCount * 8), TEXT("AVVDecodedVertexBuffer"));
            UploadData(GraphBuilder, Buffer, DecodedVertexData, segment->vertexCount * 32, segment);
            HoloMeshUtilities::ConvertToPooledBuffer(GraphBuilder, Buffer, DecodedVertexBuffer);
        }
        else
        {
            FRDGBuffer* Buffer = GraphBuilder.RegisterExternalBuffer(DecodedVertexBuffer);
            UploadData(GraphBuilder, Buffer, DecodedVertexData, segment->vertexCount * 32, segment);
        }

        // Texture Block Map
        if (segment->texture.blockCount > 0)
        {
            ClearTextures(GraphBuilder, segment, mesh);
            UpdateTextureBlockMap(GraphBuilder, segment);
        }

        // Positions are decoded through animation compute shader
        // Colors + Normals are uploaded per frame as well.
        EHoloMeshUpdateFlags updateFlags = EHoloMeshUpdateFlags::Indices | EHoloMeshUpdateFlags::UVs;

        GraphBuilder.AddPass(
            RDG_EVENT_NAME("AVVDecoder.UpdateMeshSegment"),
            ERDGPassFlags::NeverCull,
            [mesh, updateFlags](FRHICommandListImmediate& RHICmdList)
            {
                mesh->Update_RenderThread(RHICmdList, updateFlags);
            });
        
        DecodedSegmentIndex = UpdateRequest.SegmentIndex;
        DecodedSegmentVertexCount = segment->vertexCount;
        DecodedSegmentTextureInfo = segment->texture;
        RequiresSwap = true;
        segment->processed = true;
    }

    // Frame Update
    {
        bool requiresMeshUpdate = false;
        EHoloMeshUpdateFlags updateFlags = EHoloMeshUpdateFlags::None;

        // Decode and update vertex colors
        if (frame->colorCount > 0 && frame->normalCount > 0)
        {
            CPUDecodeFrameColorsNormals(mesh, frame);

            requiresMeshUpdate = true;
            updateFlags = EHoloMeshUpdateFlags::Colors;
        }
        else if (frame->colorCount > 0)
        {
            CPUDecodeFrameColors(mesh, frame);

            requiresMeshUpdate = true;
            updateFlags = EHoloMeshUpdateFlags::Colors;
        }

        if (requiresMeshUpdate)
        {
            GraphBuilder.AddPass(
                RDG_EVENT_NAME("AVVDecoder.UpdateMeshFrame"),
                ERDGPassFlags::NeverCull,
                [mesh, updateFlags](FRHICommandListImmediate& RHICmdList)
                {
                    mesh->Update_RenderThread(RHICmdList, updateFlags);
                });
        }

        // Anim
        if (true)
        {
            DecodeFrameAnimation(GraphBuilder, frame, mesh);
        }

        // Render/decode luma
        if (true)
        {
            DecodeFrameTexture(GraphBuilder, frame, mesh);
        }

        frame->processed = true;
    }

    DecoderState = EDecoderState::FinishedGPU;
}

void UAVVDecoderCPU::RequestCulled_RenderThread(FHoloMeshUpdateRequest request)
{
    // Update Request was culled so we reset state instead of performing any
    // swaps or further updates.

    RequiresSwap = false;
    DecoderState = EDecoderState::Idle;
    CurrentState.Reset();
}

bool UAVVDecoderCPU::CPUDecodeMesh(FHoloMesh* meshOut, AVVEncodedSegment* segment)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCPU_DecodeMesh);

    if (segment == nullptr)
    {
        return false;
    }

    if (!meshOut->bInitialized)
    {
        return false;
    }

    double decodeMeshStart = FPlatformTime::Seconds();

    uint32_t* data = nullptr;
    uint8_t count = 0;
    uint32_t writeIdx = 0;
    size_t readPos = 0;

    auto PositionData = meshOut->VertexBuffers->GetPositionData();
    FPositionVertex* Positions = (FPositionVertex*)PositionData->GetDataPointer();

    // We always used packed normals
    auto TangentData = meshOut->VertexBuffers->GetTangentsData();
    FPackedNormal* Tangents = (FPackedNormal*)TangentData->GetDataPointer();

    // We always use low precision half vectors with CPU decoding.
    auto TexCoordData = meshOut->VertexBuffers->GetTexCoordData();
    FVector2DHalf* TexCoords = (FVector2DHalf*)(TexCoordData->GetDataPointer());
    uint32 numTex = meshOut->VertexBuffers->GetNumTexCoords();

    uint8_t* SegmentData = segment->content->Data;

    if (Positions == nullptr || Tangents == nullptr || TexCoords == nullptr || SegmentData == nullptr)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("DecodeMesh Invalid Data. %d %d %d"), PositionData->Num(), TangentData->Num(), TexCoordData->Num());
        return false;
    }

    int vertCount = segment->vertexCount;

    // AVV_SEGMENT_POS_SKIN_EXPAND_128
    data = (uint32_t*)(&SegmentData[segment->vertexDataOffset]);

    // Compute based animation.
    if (segment->posOnlySegment)
    {
        // TODO: optimize this to not waste the space on empty SSDR weights/indices.

        float pos0[3];
        float pos1[3];

        uint32 vertexPairCount = (segment->vertexCount / 2);

        for (uint32_t v = 0; v < vertexPairCount; ++v)
        {
            // Each vertex is 48 bits, so 96 for two = 12 bytes. data is uint32_t.
            readPos = v * 3;

            // Positions.
            pos0[0] = decodeFloat16(data[readPos + 0] & 0xFFFF, segment->aabbMin[0], segment->aabbMax[0]);
            pos0[1] = decodeFloat16(data[readPos + 0] >> 16,    segment->aabbMin[1], segment->aabbMax[1]);
            pos0[2] = decodeFloat16(data[readPos + 1] & 0xFFFF, segment->aabbMin[2], segment->aabbMax[2]);

            memcpy(&DecodedVertexData[writeIdx + 0], &pos0, sizeof(float) * 3);

            writeIdx += 32;

            pos1[0] = decodeFloat16(data[readPos + 1] >> 16 ,   segment->aabbMin[0], segment->aabbMax[0]);
            pos1[1] = decodeFloat16(data[readPos + 2] & 0xFFFF, segment->aabbMin[1], segment->aabbMax[1]);
            pos1[2] = decodeFloat16(data[readPos + 2] >> 16,    segment->aabbMin[2], segment->aabbMax[2]);

            memcpy(&DecodedVertexData[writeIdx + 0], &pos1, sizeof(float) * 3);

            writeIdx += 32;
        }
    }
    else 
    {
        float pos[3];
        float boneWeights[4];

        for (uint32_t v = 0; v < segment->compactVertexCount; ++v)
        {
            // Each encoded vertex is 16 bytes.
            readPos = v * 4;

            // Positions.
            pos[0] = decodeFloat16(data[readPos + 0] & 0xFFFF, segment->aabbMin[0], segment->aabbMax[0]);
            pos[1] = decodeFloat16(data[readPos + 0] >> 16, segment->aabbMin[1], segment->aabbMax[1]);
            pos[2] = decodeFloat16(data[readPos + 1] & 0xFFFF, segment->aabbMin[2], segment->aabbMax[2]);

            // SSDR Weights
            boneWeights[0] = decodeFloat16(data[readPos + 1] >> 16, 0.0, 1.0);
            boneWeights[1] = decodeFloat16(data[readPos + 2] & 0xFFFF, 0.0, 1.0);
            boneWeights[2] = decodeFloat16(data[readPos + 2] >> 16, 0.0, 1.0);

            // Compute final weight
            boneWeights[3] = 1.0f - (boneWeights[0] + boneWeights[1] + boneWeights[2]);
            if (boneWeights[3] <= (3.0 / 2046.0f))
            {
                boneWeights[0] += boneWeights[3];
                boneWeights[3] = 0.0f;
            }

            // v2 of ssdr expansion container.
            if (segment->vertexWriteTableOffset > 0 && segment->vertexWriteTable.Num() == 0)
            {
                uint32_t* vertexWriteData = (uint32_t*)(&SegmentData[segment->vertexWriteTableOffset]);
                count = (vertexWriteData[v] >> 24);
            }
            else
            {
                uint8_t* expansionList = (uint8_t*)(&SegmentData[segment->expansionListOffset]);
                count = expansionList[v];
            }

            // Duplicate the vertex data as many times as the expansion list dictates.
            for (int i = 0; i < count; ++i)
            {
                memcpy(&DecodedVertexData[writeIdx + 0], &pos, sizeof(float) * 3);
                memcpy(&DecodedVertexData[writeIdx + 12], &boneWeights, sizeof(float) * 4);
                memcpy(&DecodedVertexData[writeIdx + 28], &data[readPos + 3], sizeof(uint32_t));
                memcpy(&DecodedVertexData[writeIdx + 32], &v, sizeof(uint32_t));

                // Each decoded vertex in 32 bytes.
                writeIdx += 32;
            }
        }
    }

    writeIdx = 0;

    // AVV_SEGMENT_UVS_12_NORMALS_888
    if (segment->uv12normal888)
    {
        data = (uint32_t*)(&SegmentData[segment->uvDataOffset]);
        for (uint32_t v = 0; v < (segment->uvCount / 2); ++v)
        {
            // We decode in sets of 2, each is 6 bytes.
            readPos = v * 3;

            // UV and Normal 0
            TexCoords[(writeIdx * numTex) + 0].X = decodeFloat12((data[readPos + 0] & 0x00000FFF) >> 0, 0.0f, 1.0f);
            TexCoords[(writeIdx * numTex) + 0].Y = decodeFloat12((data[readPos + 0] & 0x00FFF000) >> 12, 0.0f, 1.0f);
            Tangents[(writeIdx * 2) + 0] = FPackedNormal(FHoloMeshVec4(1.0f, 0.0f, 0.0f, 1.0f));
            Tangents[(writeIdx * 2) + 1] = FPackedNormal(FHoloMeshVec4(
                decodeFloat8((data[readPos + 0] & 0xFF000000) >> 24, -1.0f, 1.0f),
                decodeFloat8((data[readPos + 1] & 0x0000FF00) >> 8, -1.0f, 1.0f),
                decodeFloat8((data[readPos + 1] & 0x000000FF) >> 0, -1.0f, 1.0f),
                1.0f));

            writeIdx++;

            // UV and Normal 1
            TexCoords[(writeIdx * numTex) + 0].X = decodeFloat12((data[readPos + 1] & 0x0FFF0000) >> 16, 0.0f, 1.0f);
            TexCoords[(writeIdx * numTex) + 0].Y = decodeFloat12(((data[readPos + 1] & 0xF0000000) >> 28) + ((data[readPos + 2] & 0x000000FF) << 4), 0.0f, 1.0f);
            Tangents[(writeIdx * 2) + 0] = FPackedNormal(FHoloMeshVec4(1.0f, 0.0f, 0.0f, 1.0f));
            Tangents[(writeIdx * 2) + 1] = FPackedNormal(FHoloMeshVec4(decodeFloat8((data[readPos + 2] & 0x0000FF00) >> 8, -1.0f, 1.0f),
                decodeFloat8((data[readPos + 2] & 0xFF000000) >> 24, -1.0f, 1.0f),
                decodeFloat8((data[readPos + 2] & 0x00FF0000) >> 16, -1.0f, 1.0f),
                1.0f));

            writeIdx++;
        }

        writeIdx = 0;
    }
    // AVV_SEGMENT_UVS_16
    else
    {
        data = (uint32_t*)(&SegmentData[segment->uvDataOffset]);
        for (uint32_t v = 0; v < segment->uvCount; ++v)
        {
            // Each uv is 1 byte.
            readPos = v * 1;

            TexCoords[(writeIdx * numTex) + 0].X = decodeFloat16(data[readPos] & 0xFFFF, 0.0f, 1.0f);
            TexCoords[(writeIdx * numTex) + 0].Y = decodeFloat16(data[readPos] >> 16, 0.0f, 1.0f);

            writeIdx++;
        }

        writeIdx = 0;
    }

    // Indices
    FHoloMeshIndexBuffer::IndexWriter Indices(meshOut->IndexBuffer);
    if (segment->index32Bit)
    {
        // AVV_SEGMENT_TRIS_32
        uint32_t* index32 = (uint32_t*)(&SegmentData[segment->indexDataOffset]);
        Indices.Write(index32, segment->indexCount);
    }
    else
    {
        // AVV_SEGMENT_TRIS_16
        uint16_t* index16 = (uint16_t*)(&SegmentData[segment->indexDataOffset]);
        Indices.Write(index16, segment->indexCount);
    }

    // Clear unused entries in index buffer.
    meshOut->IndexBuffer->Clear(segment->indexCount);

    double decodeMeshTime = FPlatformTime::Seconds() - decodeMeshStart;
    //UE_LOG(LogHoloSuitePlayer, Warning, TEXT("Decode Mesh Time: %f"), decodeMeshTime);

    return true;
}

bool UAVVDecoderCPU::CPUDecodeFrameColors(FHoloMesh* meshOut, AVVEncodedFrame* frame)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCPU_CPUDecodeFrameColors);

    if (meshOut == nullptr || frame == nullptr)
    {
        return false;
    }

    //SCOPE_CYCLE_COUNTER(STAT_AVVPlayer_DecodeMesh);
    double decodeFrameColorsStart = FPlatformTime::Seconds();

    // RGBA8 colors
    auto ColorData = meshOut->VertexBuffers->GetColorData();
    uint8_t* Colors = (uint8_t*)ColorData->GetDataPointer();

    uint8_t* FrameData = frame->content->Data;

    // AVV_FRAME_COLORS_RGB_565
    uint16_t* data = (uint16_t*)(&FrameData[frame->colorDataOffset]);
    for (int v = 0; v < DecodedSegmentVertexCount; ++v)
    {
        // On CPU/Mobile decoding the color buffer is used for both colors and normals,
        // so we only use half of it for this container.
        Colors[(v * 4) + 0] = data[v] & 0xFF;
        Colors[(v * 4) + 1] = (data[v] >> 8) & 0xFF;
        Colors[(v * 4) + 2] = 0;
        Colors[(v * 4) + 3] = 0;
    }

    double decodeColorsTime = FPlatformTime::Seconds() - decodeFrameColorsStart;
    //UE_LOG(LogHoloSuitePlayer, Warning, TEXT("Decode Colors Time: %f"), decodeColorsTime);

    return true;
}

inline FHoloMeshVec3 decodeNormalOct16(FHoloMeshVec2 f)
{
    FHoloMeshVec2 f2 = { f.X * 2.0f - 1.0f, f.Y * 2.0f - 1.0f };

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    FHoloMeshVec3 n = { f2.X, f2.Y, 1.0f - fabs(f2.X) - fabs(f2.Y) };
    float t = FMath::Clamp(-n.Z, 0.0f, 1.0f);
    n.X += n.X >= 0.0f ? -t : t;
    n.Y += n.Y >= 0.0f ? -t : t;
    n.Normalize();
    return n;
}

bool UAVVDecoderCPU::CPUDecodeFrameColorsNormals(FHoloMesh* meshOut, AVVEncodedFrame* frame)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCPU_CPUDecodeFrameColorsNormals);

    if (meshOut == nullptr || frame == nullptr)
    {
        return false;
    }

    //SCOPE_CYCLE_COUNTER(STAT_AVVPlayer_DecodeMesh);
    double decodeFrameColorsNormalsStart = FPlatformTime::Seconds();

    // RGBA8 colors
    auto ColorData = meshOut->VertexBuffers->GetColorData();
    uint8_t* Colors = (uint8_t*)ColorData->GetDataPointer();

    uint8_t* FrameData = frame->content->Data;

    // AVV_FRAME_COLORS_RGB_565_NORMALS_OCT16
    uint16_t* data = (uint16_t*)(&FrameData[frame->colorDataOffset]);

    // On CPU/Mobile decoding the color buffer is used for both colors and normals.
    // The packed color+normal is 32 bits so it matches the color stride perfectly.
    memcpy(Colors, data, sizeof(uint32_t) * DecodedSegmentVertexCount);

    double decodeColorsNormalsTime = FPlatformTime::Seconds() - decodeFrameColorsNormalsStart;
    //UE_LOG(LogHoloSuitePlayer, Warning, TEXT("Decode Colors & Normals Time: %f"), decodeColorsNormalsTime);

    return true;
}