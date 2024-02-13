// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "AVV/AVVDecoderCompute.h"

IMPLEMENT_GLOBAL_SHADER(FAVVDecodePos16_CS,                                "/HoloSuitePlayer/AVV/AVVVertexDecodeCS.usf",       "DecodeSegmentPos16",                   SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVDecodePosSkinExpand_128_CS,                    "/HoloSuitePlayer/AVV/AVVVertexDecodeCS.usf",       "DecodeSegmentPosSkinExpand128",        SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVDecodeUVS_16_CS,                               "/HoloSuitePlayer/AVV/AVVUVDecodeCS.usf",           "DecodeSegmentUVs16",                   SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVDecodeUVS_12_Normals_888_CS,                   "/HoloSuitePlayer/AVV/AVVUVDecodeCS.usf",           "DecodeSegmentUVs12Normals888",         SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVDecodeSegmentTris_16_CS,                       "/HoloSuitePlayer/AVV/AVVIndexDecodeCS.usf",        "DecodeSegmentTris16CS",                SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVDecodeSegmentTris_32_CS,                       "/HoloSuitePlayer/AVV/AVVIndexDecodeCS.usf",        "DecodeSegmentTris32CS",                SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVClearUnusedTrisCS,                             "/HoloSuitePlayer/AVV/AVVIndexDecodeCS.usf",        "ClearUnusedTrisCS",                    SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVDecodeSegmentMotionVectors_CS,                 "/HoloSuitePlayer/AVV/AVVMotionVectorDecodeCS.usf", "DecodeSegmentMotionVectors",           SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVDecodeFrameColor_RGB_565_CS,                   "/HoloSuitePlayer/AVV/AVVColorDecodeCS.usf",        "DecodeFrameColorRGB565",               SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FAVVDecodeFrameColor_RGB_565_Normals_Oct16_CS,     "/HoloSuitePlayer/AVV/AVVColorDecodeCS.usf",        "DecodeFrameColorRGB565NormalsOct16",   SF_Compute);

DECLARE_CYCLE_STAT(TEXT("AVVDecoderCompute.InitDecoder"),                       STAT_AVVDecoderCompute_InitDecoder,                          STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCompute.Close"),                             STAT_AVVDecoderCompute_Close,                                STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCompute.Update"),                            STAT_AVVDecoderCompute_Update,                               STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCompute.Update_RenderThread"),               STAT_AVVDecoderCompute_Update_RenderThread,                  STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCompute.EndFrame_RenderThread"),             STAT_AVVDecoderCompute_EndFrame_RenderThread,                STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCompute.ComputeDecodeSegmentVertices"),      STAT_AVVDecoderCompute_ComputeDecodeSegmentVertices,         STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCompute.ComputeDecodeSegmentUVNormals"),     STAT_AVVDecoderCompute_ComputeDecodeSegmentUVNormals,        STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCompute.ComputeDecodeSegmentTriangles"),     STAT_AVVDecoderCompute_ComputeDecodeSegmentTriangles,        STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCompute.ComputeDecodeSegmentMotionVectors"), STAT_AVVDecoderCompute_ComputeDecodeSegmentMotionVectors,    STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVDecoderCompute.ComputeDecodeFrameColorNormals"),    STAT_AVVDecoderCompute_ComputeDecodeFrameColorNormals,       STATGROUP_HoloSuitePlayer);

DECLARE_GPU_STAT_NAMED(GPU_AVVDecoderCompute_ComputeDecodeSegmentVertices,      TEXT("AVVDecoderCompute.GPUComputeDecodeSegmentVertices"));
DECLARE_GPU_STAT_NAMED(GPU_AVVDecoderCompute_ComputeDecodeSegmentUVNormals,     TEXT("AVVDecoderCompute.GPUComputeDecodeSegmentUVNormals"));
DECLARE_GPU_STAT_NAMED(GPU_AVVDecoderCompute_ComputeDecodeSegmentTriangles,     TEXT("AVVDecoderCompute.GPUComputeDecodeSegmentTriangles"));
DECLARE_GPU_STAT_NAMED(GPU_AVVDecoderCompute_ComputeDecodeSegmentMotionVectors, TEXT("AVVDecoderCompute.GPUComputeDecodeSegmentMotionVectors"));
DECLARE_GPU_STAT_NAMED(GPU_AVVDecoderCompute_ComputeDecodeFrameColorNormals,    TEXT("AVVDecoderCompute.GPUComputeDecodeFrameColorNormals"));

// We only use a single buffer for compute decoding and always target a specific index.
#define AVV_MESH_INDEX 0

// Sets default values
UAVVDecoderCompute::UAVVDecoderCompute(const FObjectInitializer& ObjectInitializer)
    : UAVVDecoder(ObjectInitializer)
{

}

void UAVVDecoderCompute::InitDecoder(UMaterialInterface* NewMeshMaterial)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCompute_InitDecoder);

    UAVVDecoder::InitDecoder(NewMeshMaterial);

    if (!bInitialized)
    {
        HoloMeshMaterial = UHoloMeshMaterial::Create(NewMeshMaterial, GetTransientPackage());
        HoloMeshMaterial->AddToRoot();

        FHoloMesh& Mesh = HoloMesh[AVV_MESH_INDEX];

        Mesh.VertexBuffers->Create(avvReader.Limits.MaxVertexCount, 1, true);
        Mesh.IndexBuffer->Create(avvReader.Limits.MaxIndexCount, true, true);
        Mesh.LocalBox += FVector(-100.0f, -100.0f, -100.0f);
        Mesh.LocalBox += FVector(100.0f, 100.0f, 100.0f);

        ERHIFeatureLevel::Type FeatureLevel;
        if (GetWorld())
        {
            FeatureLevel = GetWorld()->Scene->GetFeatureLevel();
        }
        else
        {
            FeatureLevel = ERHIFeatureLevel::ES3_1; // lowest feature level to ensure it still works when World isn't created yet (e.g. in child BPs)
        }
        Mesh.InitOrUpdate(FeatureLevel);

        Mesh.Material = HoloMeshMaterial->GetMaterialByIndex(AVV_MESH_INDEX);
        SetMaterial(AVV_MESH_INDEX, Mesh.Material);

        UpdateHoloMesh();

        bInitialized = true;
        MarkRenderStateDirty();
    }
}

void UAVVDecoderCompute::Close()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCompute_Close);

    UAVVDecoder::Close();
}

void UAVVDecoderCompute::Update(float DeltaTime)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCompute_Update);

    UAVVDecoder::Update(DeltaTime);

    if (bImmediateMode)
    {
        return;
    }

    if (DecoderState == EDecoderState::FinishedCPU)
    {
        int holoMeshIndex = 0;

        int pendingSegment = avvReader.GetSegmentIndex(PendingState.FrameNumber);
        bool updatedSegment = pendingSegment != DecodedSegmentIndex;

        GHoloMeshManager.AddUpdateRequest(RegisteredGUID, holoMeshIndex, pendingSegment, PendingState.FrameNumber);

        CurrentState = PendingState;
        PendingState.Reset();

        // Update bounding box.
        if (updatedSegment)
        {
            AVVEncodedSegment* segment = DataCache.GetSegment(pendingSegment);
            FHoloMesh* mesh = GetHoloMesh(holoMeshIndex);
            UpdateBoundingBox(segment, mesh);
            DirtyHoloMesh();
        }

        DecoderState = EDecoderState::Idle;
    }
}

void UAVVDecoderCompute::Update_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest UpdateRequest)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCompute_Update_RenderThread);

    FHoloMesh* mesh = GetHoloMesh(UpdateRequest.HoloMeshIndex);
    
    AVVEncodedSegment* segment = nullptr;
    AVVEncodedFrame* frame = nullptr;

    if (DecodedSegmentIndex != UpdateRequest.SegmentIndex)
    {
        if (!DataCache.GetSegmentAndFrame(UpdateRequest.SegmentIndex, UpdateRequest.FrameIndex, &segment, &frame))
        {
            DecoderState = EDecoderState::Error;
            return;
        }
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

    bool updatedSegment = false;
    bool useMotionVectors = GetMotionVectorsEnabled() && !bReversedCaching;

    // Decode segment
    if (segment != nullptr)
    {
        // Vertex Data
        if (DecodedVertexBuffer == nullptr)
        {
            FRDGBuffer* Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t) * 4, avvReader.Limits.MaxVertexCount * 8), TEXT("AVVDecodedVertexBuffer"));
            HoloMeshUtilities::ConvertToPooledBuffer(GraphBuilder, Buffer, DecodedVertexBuffer);
        }

        UpdateTextureBlockMap(GraphBuilder, segment);
        ComputeDecodeSegmentVertices(GraphBuilder, segment, mesh);
        ComputeDecodeSegmentUVNormals(GraphBuilder, segment, mesh);
        ComputeDecodeSegmentTriangles(GraphBuilder, segment, mesh);
        ClearTextures(GraphBuilder, segment, mesh);

        if (segment->motionVectors && useMotionVectors)
        {
            ComputeDecodeSegmentMotionVectors(GraphBuilder, segment, mesh);
            mesh->UpdateUniforms(GraphBuilder, 1.0f);
        }

        DecodedSegmentIndex = UpdateRequest.SegmentIndex;
        DecodedSegmentVertexCount = segment->vertexCount;
        DecodedSegmentTextureInfo = segment->texture;
        updatedSegment = true;

        segment->processed = true;
    }

    // Decode frame
    {
        DecodeFrameAnimation(GraphBuilder, frame, mesh);

        if (!updatedSegment && useMotionVectors)
        {
            // Enable previous position motion vectors during the frame we update animation in.
            mesh->UpdateUniforms(GraphBuilder, 1.0f);
        }

        // Color/Normal Decode
        ComputeDecodeFrameColorNormals(GraphBuilder, frame, mesh);

        // Texture Decode
        if (frame->lumaCount > 0)
        {
            DecodeFrameTexture(GraphBuilder, frame, mesh);
        }

        frame->processed = true;
    }
}

void UAVVDecoderCompute::EndFrame_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest UpdateRequest)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCompute_EndFrame_RenderThread);

    FHoloMesh* mesh = GetHoloMesh(UpdateRequest.HoloMeshIndex);
    if (mesh == nullptr)
    {
        DecoderState = EDecoderState::Error;
        return;
    }

    // Reset motion vectors at end of frame, the next frame will re-enable if theres motion in it.
    mesh->UpdateUniforms(GraphBuilder, 0.0f);
}

bool UAVVDecoderCompute::ComputeDecodeSegmentVertices(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment, FHoloMesh* mesh)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCompute_ComputeDecodeSegmentVertices);
    RDG_GPU_STAT_SCOPE(GraphBuilder, GPU_AVVDecoderCompute_ComputeDecodeSegmentVertices);
    RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

    if (segment->vertexDataSize < 4)
    {
        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("Error decoding Segment Vertices."));
        return false;
    }

    uint8_t* data = segment->content->Data;

    if (segment->posOnlySegment)
    {
        // Upload Data

        FRDGBufferRef VertexDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t), segment->vertexDataSize / 4), TEXT("AVVVertexData"));
        FRDGBufferUAVRef VertexDataBufferUAV = GraphBuilder.CreateUAV(VertexDataBuffer, PF_R32_UINT);
        UploadData(GraphBuilder, VertexDataBuffer, &data[segment->vertexDataOffset], segment->vertexDataSize, segment);

        FRDGBuffer* VertBuffer = GraphBuilder.RegisterExternalBuffer(DecodedVertexBuffer);
        FRDGBufferUAVRef DecodedVertexBufferUAV = GraphBuilder.CreateUAV(VertBuffer, PF_R32G32B32A32_UINT);

        TShaderMapRef<FAVVDecodePos16_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FAVVDecodePos16_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodePos16_CS::FParameters>();

        PassParameters->gVertexCount = segment->vertexCount;
        PassParameters->gAABBMin = segment->GetAABBMin();
        PassParameters->gAABBMax = segment->GetAABBMax();
        PassParameters->VertexDataBuffer = VertexDataBufferUAV;
        PassParameters->DecodedVertexBuffer = DecodedVertexBufferUAV;

        int vertexPairCount = segment->vertexCount / 2;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.SegmentPos"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector((vertexPairCount / 64) + 1, 1, 1)
        );
    }
    else 
    {
        // Upload Data

        FRDGBufferRef VertexSkinDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t) * 4, segment->vertexDataSize / 16), TEXT("AVVVertexData"));
        FRDGBufferUAVRef VertexSkinDataBufferUAV = GraphBuilder.CreateUAV(VertexSkinDataBuffer, PF_R32G32B32A32_UINT);
        UploadData(GraphBuilder, VertexSkinDataBuffer, &data[segment->vertexDataOffset], segment->vertexDataSize, segment);

        FRDGBufferRef VertexWriteTableBuffer = nullptr;
        FRDGBufferUAVRef VertexWriteTableBufferUAV = nullptr;
        if (segment->vertexWriteTableOffset > 0 && segment->vertexWriteTable.Num() == 0)
        {
            // v2 of this container includes the vertex write data in the file.
            VertexWriteTableBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t), segment->compactVertexCount), TEXT("AVVVertexWriteTable"));
            VertexWriteTableBufferUAV = GraphBuilder.CreateUAV(VertexWriteTableBuffer, PF_R32_UINT);
            UploadData(GraphBuilder, VertexWriteTableBuffer, &data[segment->vertexWriteTableOffset], segment->compactVertexCount * 4, segment);
        }
        else 
        {
            VertexWriteTableBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t), segment->compactVertexCount), TEXT("AVVVertexWriteTable"));
            VertexWriteTableBufferUAV = GraphBuilder.CreateUAV(VertexWriteTableBuffer, PF_R32_UINT);
            UploadData(GraphBuilder, VertexWriteTableBuffer, segment->vertexWriteTable.GetData(), segment->compactVertexCount * 4, segment);
        }
        
        FRDGBuffer* VertBuffer = GraphBuilder.RegisterExternalBuffer(DecodedVertexBuffer);
        FRDGBufferUAVRef DecodedVertexBufferUAV = GraphBuilder.CreateUAV(VertBuffer, PF_R32G32B32A32_UINT);

        TShaderMapRef<FAVVDecodePosSkinExpand_128_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FAVVDecodePosSkinExpand_128_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodePosSkinExpand_128_CS::FParameters>();

        PassParameters->gVertexCount = segment->vertexCount;
        PassParameters->gCompactVertexCount = segment->compactVertexCount;
        PassParameters->gAABBMin = segment->GetAABBMin();
        PassParameters->gAABBMax = segment->GetAABBMax();
        PassParameters->VertexSkinDataBuffer = VertexSkinDataBufferUAV;
        PassParameters->VertexWriteTable = VertexWriteTableBufferUAV;
        PassParameters->DecodedVertexBuffer = DecodedVertexBufferUAV;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.SegmentPosSkinExpand"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector((segment->compactVertexCount / 64) + 1, 1, 1)
        );
    }

    return true;
}

bool UAVVDecoderCompute::ComputeDecodeSegmentUVNormals(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment, FHoloMesh* mesh)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCompute_ComputeDecodeSegmentUVNormals);
    RDG_GPU_STAT_SCOPE(GraphBuilder, GPU_AVVDecoderCompute_ComputeDecodeSegmentUVNormals);
    RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

    if (segment->uvDataSize < 4)
    {
        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("Error decoding UV Normals."));
        return false;
    }

    uint8_t* data = segment->content->Data;

    FRDGBufferRef UVDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t), segment->uvDataSize / 4), TEXT("AVVUVData"));
    FRDGBufferUAVRef UVDataBufferUAV = GraphBuilder.CreateUAV(UVDataBuffer, PF_R32_UINT);
    UploadData(GraphBuilder, UVDataBuffer, &data[segment->uvDataOffset], segment->uvDataSize, segment);

    if (segment->uv12normal888)
    {
        TShaderMapRef<FAVVDecodeUVS_12_Normals_888_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FAVVDecodeUVS_12_Normals_888_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodeUVS_12_Normals_888_CS::FParameters>();

        PassParameters->UVDataBuffer         = UVDataBufferUAV;
        PassParameters->VertexTexCoordBuffer = mesh->VertexBuffers->GetTexCoordBufferUAV();
        PassParameters->VertexTangentBuffer  = mesh->VertexBuffers->GetTangentsBufferUAV();
        PassParameters->gTexCoordStride      = mesh->VertexBuffers->GetNumTexCoords();
        PassParameters->gVertexCount         = segment->vertexCount;
        PassParameters->gUVCount             = segment->uvCount;

        int uvNormDataCount = segment->uvCount / 2;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.SegmentUVNormals"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector((uvNormDataCount / 64) + 1, 1, 1)
        );
    }
    else
    {
        TShaderMapRef<FAVVDecodeUVS_16_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FAVVDecodeUVS_16_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodeUVS_16_CS::FParameters>();

        PassParameters->UVDataBuffer         = UVDataBufferUAV;
        PassParameters->VertexTexCoordBuffer = mesh->VertexBuffers->GetTexCoordBufferUAV();
        PassParameters->gTexCoordStride      = mesh->VertexBuffers->GetNumTexCoords();
        PassParameters->gUVCount             = segment->uvCount;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.SegmentUV"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector((segment->uvCount / 64) + 1, 1, 1)
        );
    };

    return true;
}

bool UAVVDecoderCompute::ComputeDecodeSegmentTriangles(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment, FHoloMesh* mesh)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCompute_ComputeDecodeSegmentTriangles);
    RDG_GPU_STAT_SCOPE(GraphBuilder, GPU_AVVDecoderCompute_ComputeDecodeSegmentTriangles);
    RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

    if (segment->indexDataSize < 4)
    {
        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("Error decoding Segment Triangles."));
        return false;
    }

    uint8_t* data = segment->content->Data;

    // Upload Data
    FRDGBufferRef IndexDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32_t), segment->indexDataSize / 4), TEXT("AVVIndexData"));
    FRDGBufferUAVRef IndexDataBufferUAV = GraphBuilder.CreateUAV(IndexDataBuffer, PF_R32_UINT);
    UploadData(GraphBuilder, IndexDataBuffer, &data[segment->indexDataOffset], segment->indexDataSize, segment);

    // Clear unused triangles.
    HoloMeshUtilities::ClearUAVUInt(GraphBuilder, mesh->IndexBuffer->GetIndexBufferUAV());

    if (segment->index32Bit)
    {
        TShaderMapRef<FAVVDecodeSegmentTris_32_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FAVVDecodeSegmentTris_32_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodeSegmentTris_32_CS::FParameters>();

        PassParameters->IndexDataBuffer = IndexDataBufferUAV;
        PassParameters->IndexBuffer     = mesh->IndexBuffer->GetIndexBufferUAV();
        PassParameters->gMaxIndexCount  = avvReader.Limits.MaxIndexCount;
        PassParameters->gIndexCount     = segment->indexCount;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.SegmentTris32"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector((segment->indexCount / 64) + 1, 1, 1)
        );
    }
    else
    {
        TShaderMapRef<FAVVDecodeSegmentTris_16_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FAVVDecodeSegmentTris_16_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodeSegmentTris_16_CS::FParameters>();

        int compactIndexCount = ((segment->indexCount - 1) / 2) + 1;

        PassParameters->IndexDataBuffer     = IndexDataBufferUAV;
        PassParameters->IndexBuffer         = mesh->IndexBuffer->GetIndexBufferUAV();
        PassParameters->gCompactIndexCount  = compactIndexCount;
        PassParameters->gMaxIndexCount      = avvReader.Limits.MaxIndexCount;
        PassParameters->gIndexCount         = segment->indexCount;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.SegmentTris16"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector((compactIndexCount / 64) + 1, 1, 1)
        );
    }

    // Clear unused triangles.
    if (false)
    {
        TShaderMapRef<FAVVClearUnusedTrisCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FAVVClearUnusedTrisCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVClearUnusedTrisCS::FParameters>();

        int compactIndexCount = ((segment->indexCount - 1) / 2) + 1;

        PassParameters->IndexBuffer = mesh->IndexBuffer->GetIndexBufferUAV();
        PassParameters->gCompactIndexCount = compactIndexCount;
        PassParameters->gMaxIndexCount = avvReader.Limits.MaxIndexCount;
        PassParameters->gIndexCount = segment->indexCount;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.ClearUnusedTris"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector(1, 1, 1)
        );
    }

    return true;
}

bool UAVVDecoderCompute::ComputeDecodeSegmentMotionVectors(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment, FHoloMesh* meshOut)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCompute_ComputeDecodeSegmentMotionVectors);
    RDG_GPU_STAT_SCOPE(GraphBuilder, GPU_AVVDecoderCompute_ComputeDecodeSegmentMotionVectors);
    RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

    if (segment->motionVectorsDataSize < 4)
    {
        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("Error decoding Segment Motion Vectors."));
        return false;
    }

    uint8_t* data = segment->content->Data;

    // Upload motion vector data.
    FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4, segment->motionVectorsDataSize), TEXT("AVVSegmentMotionVectorsData"));
    FRDGBufferUAVRef BufferUAV = GraphBuilder.CreateUAV(Buffer, PF_R32_UINT);
    UploadData(GraphBuilder, Buffer, &data[segment->motionVectorsDataOffset], segment->motionVectorsDataSize, segment);

    // Min/Max for quantization
    FHoloMeshVec3 MotionVectorsMin(segment->motionVectorsMin[0], segment->motionVectorsMin[1], segment->motionVectorsMin[2]);
    FHoloMeshVec3 MotionVectorsMax(segment->motionVectorsMax[0], segment->motionVectorsMax[1], segment->motionVectorsMax[2]);

    // Decoded Vertex Buffer
    FRDGBuffer* VertBuffer = GraphBuilder.RegisterExternalBuffer(DecodedVertexBuffer);
    FRDGBufferUAVRef DecodedVertexBufferUAV = GraphBuilder.CreateUAV(VertBuffer, PF_R32G32B32A32_UINT);

    TShaderMapRef<FAVVDecodeSegmentMotionVectors_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    FAVVDecodeSegmentMotionVectors_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodeSegmentMotionVectors_CS::FParameters>();

    PassParameters->gVertexCount = segment->vertexCount;
    PassParameters->gMotionVectorsMin = MotionVectorsMin;
    PassParameters->gMotionVectorsMax = MotionVectorsMax;
    PassParameters->MotionVectorsDataBuffer = BufferUAV;
    PassParameters->DecodedVertexBuffer = DecodedVertexBufferUAV;
    PassParameters->VertexPositionBuffer = meshOut->VertexBuffers->GetPositionBufferUAV();

    int motionVectorDataCount = segment->motionVectorsCount;

    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("AVVDecoder.SegmentMotionVectors"),
        ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
        ComputeShader,
        PassParameters,
        FIntVector((motionVectorDataCount / 64) + 1, 1, 1)
    );

    return true;
}

bool UAVVDecoderCompute::ComputeDecodeFrameColorNormals(FRDGBuilder& GraphBuilder, AVVEncodedFrame* frame, FHoloMesh* mesh)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVDecoderCompute_ComputeDecodeFrameColorNormals);
    RDG_GPU_STAT_SCOPE(GraphBuilder, GPU_AVVDecoderCompute_ComputeDecodeFrameColorNormals);
    RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

    if (frame->colorDataSize < 4)
    {
        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("Error decoding color normals."));
        return false;
    }

    uint8_t* data = frame->content->Data;

    FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4, frame->colorDataSize), TEXT("AVVFrameColorNormalsData"));
    FRDGBufferUAVRef BufferUAV = GraphBuilder.CreateUAV(Buffer, PF_R32_UINT);
    UploadData(GraphBuilder, Buffer, &data[frame->colorDataOffset], frame->colorDataSize, nullptr, frame);

    if (frame->colorCount > 0 && frame->normalCount > 0)
    {
        TShaderMapRef<FAVVDecodeFrameColor_RGB_565_Normals_Oct16_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FAVVDecodeFrameColor_RGB_565_Normals_Oct16_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodeFrameColor_RGB_565_Normals_Oct16_CS::FParameters>();

        PassParameters->ColorDataBuffer     = BufferUAV;
        PassParameters->VertexColorBuffer   = mesh->VertexBuffers->GetColorBufferUAV();
        PassParameters->VertexTangentBuffer = mesh->VertexBuffers->GetTangentsBufferUAV();
        PassParameters->gVertexCount        = DecodedSegmentVertexCount;
        PassParameters->gColorCount         = frame->colorCount;

        int colorNormalDataCount = frame->colorCount;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.FrameColorNormals"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector((colorNormalDataCount / 64) + 1, 1, 1)
        );
    }
    else if (frame->colorCount > 0)
    {
        TShaderMapRef<FAVVDecodeFrameColor_RGB_565_CS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FAVVDecodeFrameColor_RGB_565_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAVVDecodeFrameColor_RGB_565_CS::FParameters>();

        PassParameters->ColorDataBuffer     = BufferUAV;
        PassParameters->VertexColorBuffer   = mesh->VertexBuffers->GetColorBufferUAV();
        PassParameters->gVertexCount        = DecodedSegmentVertexCount;
        PassParameters->gColorCount         = frame->colorCount;

        int colorDataCount = frame->colorCount / 2;

        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("AVVDecoder.FrameColor"),
            ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            ComputeShader,
            PassParameters,
            FIntVector((colorDataCount / 64) + 1, 1, 1)
        );
    }

    return true;
}