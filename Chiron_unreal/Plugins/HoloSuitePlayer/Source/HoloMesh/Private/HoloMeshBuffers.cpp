// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloMeshBuffers.h"
#include "HoloMeshModule.h"
#include "HoloMeshManager.h"
#include "HoloMeshUtilities.h"

#include "Components.h"
#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GPUSkinCache.h"
#include "RenderResource.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "RHI.h"
#include "StaticMeshVertexData.h"

#define HOLOMESH_BUFFER_DEBUG 0

EBufferUsageFlags GetBufferUsage(bool bNeedsUAV)
{
    if (bNeedsUAV)
    {
        return BUF_ShaderResource | BUF_UnorderedAccess;
    }

    return BUF_ShaderResource | BUF_Dynamic;
}

// --- Index Buffer ---

FHoloMeshIndexBuffer::FHoloMeshIndexBuffer()
{
    IndexData = new TArray<uint32>();
}

FHoloMeshIndexBuffer::~FHoloMeshIndexBuffer()
{
    if (IndexData != nullptr)
    {
        delete IndexData;
    }

    IndexBuffer.ReleaseRHI();
}

void FHoloMeshIndexBuffer::Create(uint32 InNumIndices, bool bInUse32Bit, bool bInNeedsUAV)
{
    FScopeLock Lock(&CriticalSection);

    bInitialized = false;
    bUse32Bit = bInUse32Bit;
    bNeedsUAV = bInNeedsUAV;
    IndexData->Empty();

    if (bUse32Bit)
    {
        IndexData->SetNumZeroed(InNumIndices);
        SizeBytes = InNumIndices * 4;
    }
    else {
        IndexData->SetNumZeroed(InNumIndices / 2);
        SizeBytes = (InNumIndices / 2) * 4;
    }
}

void FHoloMeshIndexBuffer::SwapData(FHoloMeshIndexBuffer* srcIndexBuffer)
{
    FScopeLock Lock(&CriticalSection);

    if (IndexData != nullptr)
    {
        delete IndexData;
    }

    IndexData = srcIndexBuffer->TakeData();
}

uint32 FHoloMeshIndexBuffer::GetNumIndices() const
{
    if (bUse32Bit)
    {
        return IndexData->Num();
    }
    else
    {
        return IndexData->Num() * 2;
    }
}

void FHoloMeshIndexBuffer::Clear(uint32 startingIndex)
{
    if ((int)startingIndex >= GetNumIndices())
    {
        return;
    }

    if (bUse32Bit)
    {
        uint32* data = IndexData->GetData();
        memset(&data[startingIndex], 0, sizeof(uint32) * (GetNumIndices() - startingIndex));
    }
    else {
        uint16* data = (uint16*)IndexData->GetData();
        memset(&data[startingIndex], 0, sizeof(uint16) * (GetNumIndices() - startingIndex));
    }
}

void FHoloMeshIndexBuffer::InitOrUpdate()
{
    if (!bInitialized)
    {
        BeginInitResource(&IndexBuffer);
        BeginInitResource(this);

        bInitialized = true;
    }
}

#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
void FHoloMeshIndexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
    FRenderResource::InitRHI(RHICmdList);

    FRHIResourceCreateInfo CreateInfo(TEXT("FHoloMeshIndexBuffer"));

    uint32 stride = bUse32Bit ? sizeof(uint32) : sizeof(uint16);
    uint8 format  = bUse32Bit ? PF_R32_UINT    : PF_R16_UINT;

    IndexBuffer.IndexBufferRHI = RHICreateIndexBuffer(stride, GetNumIndices() * stride, GetBufferUsage(bNeedsUAV), CreateInfo);

    // Initial upload to ensure unused buffer is filled with zeros.
    if (IndexData != nullptr)
    {
        if (bUse32Bit)
        {
            HoloMeshUtilities::UploadIndexBuffer(IndexBuffer.IndexBufferRHI, IndexData->GetData(), GetNumIndices() * sizeof(uint32));
        }
        else
        {
            HoloMeshUtilities::UploadIndexBuffer(IndexBuffer.IndexBufferRHI, IndexData->GetData(), GetNumIndices() * sizeof(uint16));
        }
    }

    if (bNeedsUAV)
    {
        IndexBufferUAV = RHICreateUnorderedAccessView(IndexBuffer.IndexBufferRHI, format);
    }

    GHoloMeshManager.AddMeshBytes(SizeBytes);
}

void FHoloMeshIndexBuffer::InitResource(FRHICommandListBase& RHICmdList)
{
    FRenderResource::InitResource(RHICmdList);
    IndexBuffer.InitResource();
}
#else
void FHoloMeshIndexBuffer::InitRHI()
{
    FRenderResource::InitRHI();

    FRHIResourceCreateInfo CreateInfo(TEXT("FHoloMeshIndexBuffer"));

    uint32 stride = bUse32Bit ? sizeof(uint32) : sizeof(uint16);
    uint8 format = bUse32Bit ? PF_R32_UINT : PF_R16_UINT;

    IndexBuffer.IndexBufferRHI = RHICreateIndexBuffer(stride, GetNumIndices() * stride, GetBufferUsage(bNeedsUAV), CreateInfo);

    // Initial upload to ensure unused buffer is filled with zeros.
    if (IndexData != nullptr)
    {
        if (bUse32Bit)
        {
            HoloMeshUtilities::UploadIndexBuffer(IndexBuffer.IndexBufferRHI, IndexData->GetData(), GetNumIndices() * sizeof(uint32));
        }
        else
        {
            HoloMeshUtilities::UploadIndexBuffer(IndexBuffer.IndexBufferRHI, IndexData->GetData(), GetNumIndices() * sizeof(uint16));
        }
    }

    if (bNeedsUAV)
    {
        IndexBufferUAV = RHICreateUnorderedAccessView(IndexBuffer.IndexBufferRHI, format);
    }

    GHoloMeshManager.AddMeshBytes(SizeBytes);
}

void FHoloMeshIndexBuffer::InitResource()
{
    FRenderResource::InitResource();
    IndexBuffer.InitResource();
}
#endif

void FHoloMeshIndexBuffer::ReleaseRHI()
{
    FRenderResource::ReleaseRHI();
    IndexBuffer.ReleaseRHI();

    GHoloMeshManager.RemoveMeshBytes(SizeBytes);
}

void FHoloMeshIndexBuffer::ReleaseResource()
{
    FRenderResource::ReleaseResource();
    IndexBuffer.ReleaseResource();
}

void FHoloMeshIndexBuffer::UpdateData_RenderThread(FRHICommandListImmediate& RHICmdList)
{
    FScopeLock Lock(&CriticalSection);

    // Copy the index data into the index buffer.
    double start = FPlatformTime::Seconds();

    if (bUse32Bit)
    {
        HoloMeshUtilities::UploadIndexBuffer(IndexBuffer.IndexBufferRHI, IndexData->GetData(), GetNumIndices() * sizeof(uint32), &RHICmdList);
    }
    else 
    {
        HoloMeshUtilities::UploadIndexBuffer(IndexBuffer.IndexBufferRHI, IndexData->GetData(), GetNumIndices() * sizeof(uint16), &RHICmdList);
    }

    double end = FPlatformTime::Seconds();

#if HOLOMESH_BUFFER_DEBUG
    UE_LOG(LogHoloMesh, Warning, TEXT("Index Upload Time: %f Size: %d"), ((end - start) * 1000.0f), (GetNumIndices() * sizeof(uint32)));
#endif
}

void FHoloMeshIndexBuffer::UpdateData()
{
    ENQUEUE_RENDER_COMMAND(FHoloMeshIndexBufferUpdate)(
        [&](FRHICommandListImmediate& RHICmdList)
        {
            UpdateData_RenderThread(RHICmdList);
        });
}

// --- Vertex Buffer ---

static inline void InitOrUpdateResource(FRenderResource* Resource)
{
    if (!Resource->IsInitialized())
    {
        Resource->InitResource();
    }
    else
    {
        Resource->UpdateRHI();
    }
}

FHoloMeshVertexBuffers::FHoloMeshVertexBuffers() :
    NumVertices(0),
    NumTexCoords(0),
    PositionData(NULL),
    PrevPositionData(NULL),
    ColorData(NULL),
    TangentsData(NULL),
    TexCoordData(NULL)
{
}

FHoloMeshVertexBuffers::~FHoloMeshVertexBuffers()
{
	CleanUp();
}

/** Delete existing resources */
void FHoloMeshVertexBuffers::CleanUp()
{
	if (PositionData)
	{
		delete PositionData;
        PositionData = nullptr;
	}

    if (PrevPositionData)
    {
        delete PrevPositionData;
        PrevPositionData = nullptr;
    }

    if (ColorData)
    {
        delete ColorData;
        ColorData = nullptr;
    }

    if (TangentsData)
    {
        delete TangentsData;
        TangentsData = nullptr;
    }

    if (TexCoordData)
    {
        delete TexCoordData;
        TexCoordData = nullptr;
    }
}

void FHoloMeshVertexBuffers::Create(uint32 InNumVertices, uint32 InNumTexCoords, bool bInNeedsUAV, bool bInUseHighPrecision, bool bInNeedsCPUAccess)
{
    check(InNumTexCoords < MAX_STATIC_TEXCOORDS&& InNumTexCoords > 0);

    FScopeLock Lock(&CriticalSection);

    // Clean up any existing data.
    CleanUp();

	NumVertices         = InNumVertices;
    NumTexCoords        = InNumTexCoords;
    bUseHighPrecision   = bInUseHighPrecision;
    bNeedsUAV           = bInNeedsUAV;
    bNeedsCPUAccess     = true;
    SizeBytes           = 0;

    // Positions
    PositionData = new FPositionVertexData(bNeedsCPUAccess);
    PositionData->ResizeBuffer(NumVertices);

    // Positions
    PrevPositionData = new FPositionVertexData(bNeedsCPUAccess);
    PrevPositionData->ResizeBuffer(NumVertices);

    // Colors
    ColorData = new FColorVertexData(bNeedsCPUAccess);
    ColorData->ResizeBuffer(NumVertices);

    // Tangents (Normals)
    typedef TStaticMeshVertexTangentDatum<typename TStaticMeshVertexTangentTypeSelector<EStaticMeshVertexTangentBasisType::Default>::TangentTypeT> TangentType;
    TangentsData = new TStaticMeshVertexData<TangentType>(bNeedsCPUAccess);
    TangentsData->ResizeBuffer(NumVertices);

    // UVs
    uint32 UVTypeSize = 0;
    if (bUseHighPrecision)
    {
        typedef TStaticMeshVertexUVsDatum<typename TStaticMeshVertexUVsTypeSelector<EStaticMeshVertexUVType::HighPrecision>::UVsTypeT> UVType;
        TexCoordData = new TStaticMeshVertexData<UVType>(bNeedsCPUAccess);
        TexCoordData->ResizeBuffer(NumVertices * GetNumTexCoords());
        UVTypeSize = sizeof(UVType);
    }
    else 
    {
        typedef TStaticMeshVertexUVsDatum<typename TStaticMeshVertexUVsTypeSelector<EStaticMeshVertexUVType::Default>::UVsTypeT> UVType;
        TexCoordData = new TStaticMeshVertexData<UVType>(bNeedsCPUAccess);
        TexCoordData->ResizeBuffer(NumVertices * GetNumTexCoords());
        UVTypeSize = sizeof(UVType);
    }

    PositionVertexBuffer.CachedStride = PositionData->GetStride();
    PrevPositionVertexBuffer.CachedStride = PrevPositionData->GetStride();
    ColorVertexBuffer.CachedStride = ColorData->GetStride();
    TangentsVertexBuffer.CachedStride = sizeof(TangentType);
    TexCoordVertexBuffer.CachedStride = sizeof(UVTypeSize);

    SizeBytes += PositionData->GetResourceSize() + PrevPositionData->GetResourceSize() + TexCoordData->GetResourceSize() + ColorData->GetResourceSize() + TangentsData->GetResourceSize();
}

void FHoloMeshVertexBuffers::SwapData(FHoloMeshVertexBuffers* srcVertexBuffers)
{
    FScopeLock Lock(&CriticalSection);

    CleanUp();

    PositionData = srcVertexBuffers->TakePositionData();
    PrevPositionData = srcVertexBuffers->TakePrevPositionData();
    ColorData = srcVertexBuffers->TakeColorData();
    TangentsData = srcVertexBuffers->TakeTangentsData();
    TexCoordData = srcVertexBuffers->TakeTexCoordData();
}

void FHoloMeshVertexBuffers::InitOrUpdate(FHoloMeshVertexFactory* InVertexFactory, uint32 InLightMapIndex)
{
    check(InLightMapIndex < NumTexCoords);

    if (!bInitialized)
    {
        FHoloMeshVertexBuffers* Self = this;
        ENQUEUE_RENDER_COMMAND(HoloMeshVertexBuffersInit)(
            [InVertexFactory, Self, InLightMapIndex](FRHICommandListImmediate& RHICmdList)
            {
                InitOrUpdateResource(Self);

                FHoloMeshVertexFactory::FDataType Data;
                Self->BindVertexBuffer(InVertexFactory, Data, InLightMapIndex);
                InVertexFactory->SetData(Data);

                InitOrUpdateResource(InVertexFactory);
            });

        BeginInitResource(&PositionVertexBuffer);
        BeginInitResource(&PrevPositionVertexBuffer);
        BeginInitResource(&ColorVertexBuffer);
        BeginInitResource(&TangentsVertexBuffer);
        BeginInitResource(&TexCoordVertexBuffer);
        BeginInitResource(InVertexFactory);

        bInitialized = true;
    }
}

FHoloMeshBufferRHIRef FHoloMeshVertexBuffers::CreatePositionRHIBuffer()
{
	if (GetNumVertices())
	{
		FResourceArrayInterface* RESTRICT ResourceArray = PositionData ? PositionData->GetResourceArray() : nullptr;
		const uint32 SizeInBytes = ResourceArray ? ResourceArray->GetResourceDataSize() : 0;

#if (ENGINE_MAJOR_VERSION == 5)
        FRHIResourceCreateInfo CreateInfo(TEXT("HoloMeshPositionBuffer"), ResourceArray);
#else
        FRHIResourceCreateInfo CreateInfo(ResourceArray);
#endif

		CreateInfo.bWithoutNativeResource = !PositionData;
		return RHICreateVertexBuffer(SizeInBytes, GetBufferUsage(bNeedsUAV), CreateInfo);
	}
	return nullptr;
}

FHoloMeshBufferRHIRef FHoloMeshVertexBuffers::CreatePrevPositionRHIBuffer()
{
    if (GetNumVertices())
    {
        FResourceArrayInterface* RESTRICT ResourceArray = PrevPositionData ? PrevPositionData->GetResourceArray() : nullptr;
        const uint32 SizeInBytes = ResourceArray ? ResourceArray->GetResourceDataSize() : 0;

#if (ENGINE_MAJOR_VERSION == 5)
        FRHIResourceCreateInfo CreateInfo(TEXT("HoloMeshPrevPositionBuffer"), ResourceArray);
#else
        FRHIResourceCreateInfo CreateInfo(ResourceArray);
#endif

        CreateInfo.bWithoutNativeResource = !PrevPositionData;
        return RHICreateVertexBuffer(SizeInBytes, GetBufferUsage(bNeedsUAV), CreateInfo);
    }
    return nullptr;
}

FHoloMeshBufferRHIRef FHoloMeshVertexBuffers::CreateColorRHIBuffer()
{
    // Note: Color buffer always uses UAV for now.

    if (GetNumVertices())
    {
        FResourceArrayInterface* RESTRICT ResourceArray = ColorData ? ColorData->GetResourceArray() : nullptr;
        const uint32 SizeInBytes = ResourceArray ? ResourceArray->GetResourceDataSize() : 0;

#if (ENGINE_MAJOR_VERSION == 5)
        FRHIResourceCreateInfo CreateInfo(TEXT("HoloMeshColorBuffer"), ResourceArray);
#else
        FRHIResourceCreateInfo CreateInfo(ResourceArray);
#endif

        CreateInfo.bWithoutNativeResource = !ColorData;
        return RHICreateVertexBuffer(SizeInBytes, GetBufferUsage(true), CreateInfo);
    }
    return nullptr;
}

FHoloMeshBufferRHIRef FHoloMeshVertexBuffers::CreateTangentsRHIBuffer()
{
    if (GetNumVertices())
    {
        FResourceArrayInterface* RESTRICT ResourceArray = TangentsData ? TangentsData->GetResourceArray() : nullptr;
        const uint32 SizeInBytes = ResourceArray ? ResourceArray->GetResourceDataSize() : 0;

#if (ENGINE_MAJOR_VERSION == 5)
        FRHIResourceCreateInfo CreateInfo(TEXT("HoloMeshTangentsBuffer"), ResourceArray);
#else
        FRHIResourceCreateInfo CreateInfo(ResourceArray);
#endif

        CreateInfo.bWithoutNativeResource = !TangentsData;
        return RHICreateVertexBuffer(SizeInBytes, GetBufferUsage(bNeedsUAV), CreateInfo);
    }
    return nullptr;
}

FHoloMeshBufferRHIRef FHoloMeshVertexBuffers::CreateTexCoordRHIBuffer()
{
    if (GetNumTexCoords())
    {
        FResourceArrayInterface* RESTRICT ResourceArray = TexCoordData ? TexCoordData->GetResourceArray() : nullptr;
        const uint32 SizeInBytes = ResourceArray ? ResourceArray->GetResourceDataSize() : 0;

#if (ENGINE_MAJOR_VERSION == 5)
        FRHIResourceCreateInfo CreateInfo(TEXT("HoloMeshTexCoordBuffer"), ResourceArray);
#else
        FRHIResourceCreateInfo CreateInfo(ResourceArray);
#endif

        CreateInfo.bWithoutNativeResource = !TexCoordData;
        return RHICreateVertexBuffer(SizeInBytes, GetBufferUsage(bNeedsUAV), CreateInfo);
    }
    return nullptr;
}

#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
void FHoloMeshVertexBuffers::InitRHI(FRHICommandListBase& RHICmdList)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FHoloMeshVertexBuffer::InitRHI);

    // Positions
    {
        PositionVertexBuffer.VertexBufferRHI = CreatePositionRHIBuffer();
        if (PositionVertexBuffer.VertexBufferRHI)
        {
            bool bSRV = RHISupportsManualVertexFetch(GMaxRHIShaderPlatform) || IsGPUSkinCacheAvailable(GMaxRHIShaderPlatform);
            bSRV |= (PositionData && PositionData->GetAllowCPUAccess());
            if (bSRV)
            {
                PositionVertexBuffer.BufferSRV = RHICmdList.CreateShaderResourceView(FShaderResourceViewInitializer(PositionData ? PositionVertexBuffer.VertexBufferRHI : nullptr, PF_R32_FLOAT));
            }

            if (bNeedsUAV)
            {
                PositionVertexBuffer.BufferUAV = RHICmdList.CreateUnorderedAccessView(PositionVertexBuffer.VertexBufferRHI, PF_R32_FLOAT);
            }
        }
    }

    // Previous Positions
    {
        PrevPositionVertexBuffer.VertexBufferRHI = CreatePrevPositionRHIBuffer();
        if (PrevPositionVertexBuffer.VertexBufferRHI)
        {
            bool bSRV = RHISupportsManualVertexFetch(GMaxRHIShaderPlatform) || IsGPUSkinCacheAvailable(GMaxRHIShaderPlatform);
            bSRV |= (PrevPositionData && PrevPositionData->GetAllowCPUAccess());
            if (bSRV)
            {
                PrevPositionVertexBuffer.BufferSRV = RHICmdList.CreateShaderResourceView(FShaderResourceViewInitializer(PrevPositionData ? PrevPositionVertexBuffer.VertexBufferRHI : nullptr, PF_R32_FLOAT));
            }

            if (bNeedsUAV)
            {
                PrevPositionVertexBuffer.BufferUAV = RHICmdList.CreateUnorderedAccessView(PrevPositionVertexBuffer.VertexBufferRHI, PF_R32_FLOAT);
            }
        }
    }

    // Colors
    {
        ColorVertexBuffer.VertexBufferRHI = CreateColorRHIBuffer();
        if (ColorVertexBuffer.VertexBufferRHI && RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
        {
            ColorVertexBuffer.BufferSRV = RHICmdList.CreateShaderResourceView(FShaderResourceViewInitializer(ColorData ? ColorVertexBuffer.VertexBufferRHI : nullptr, PF_R8G8B8A8));
        }

        // Note: for now color buffer is always populated with compute, so we need a UAV for it regardless.
        ColorVertexBuffer.BufferUAV = RHICmdList.CreateUnorderedAccessView(ColorVertexBuffer.VertexBufferRHI, PF_R8G8B8A8);
    }

    // Tangents
    {
        TangentsVertexBuffer.VertexBufferRHI = CreateTangentsRHIBuffer();
        if (TangentsVertexBuffer.VertexBufferRHI && (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform) || IsGPUSkinCacheAvailable(GMaxRHIShaderPlatform)))
        {
            TangentsVertexBuffer.BufferSRV = RHICmdList.CreateShaderResourceView(FShaderResourceViewInitializer(
                TangentsData ? TangentsVertexBuffer.VertexBufferRHI : nullptr, PF_R8G8B8A8_SNORM));
        }
        if (bNeedsUAV)
        {
            TangentsVertexBuffer.BufferUAV = RHICmdList.CreateUnorderedAccessView(TangentsVertexBuffer.VertexBufferRHI, PF_R8G8B8A8_SNORM);
        }
    }

    // UVs
    {
        EPixelFormat texCoordFormat = bUseHighPrecision ? PF_G32R32F : PF_G16R16F;

        TexCoordVertexBuffer.VertexBufferRHI = CreateTexCoordRHIBuffer();
        if (TexCoordVertexBuffer.VertexBufferRHI && RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
        {
            TexCoordVertexBuffer.BufferSRV = RHICmdList.CreateShaderResourceView(FShaderResourceViewInitializer(
                TexCoordData ? TexCoordVertexBuffer.VertexBufferRHI : nullptr, texCoordFormat));
        }
        if (bNeedsUAV)
        {
            TexCoordVertexBuffer.BufferUAV = RHICmdList.CreateUnorderedAccessView(TexCoordVertexBuffer.VertexBufferRHI, texCoordFormat);
        }
    }

    GHoloMeshManager.AddMeshBytes(SizeBytes);
}

void FHoloMeshVertexBuffers::InitResource(FRHICommandListBase& RHICmdList)
{
    FRenderResource::InitResource(RHICmdList);

    PositionVertexBuffer.InitResource(RHICmdList);
    PrevPositionVertexBuffer.InitResource(RHICmdList);
    ColorVertexBuffer.InitResource(RHICmdList);
    TangentsVertexBuffer.InitResource(RHICmdList);
    TexCoordVertexBuffer.InitResource(RHICmdList);
}
#else
void FHoloMeshVertexBuffers::InitRHI()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FHoloMeshVertexBuffer::InitRHI);

    // Positions
    {
        PositionVertexBuffer.VertexBufferRHI = CreatePositionRHIBuffer();
        if (PositionVertexBuffer.VertexBufferRHI)
        {
            bool bSRV = RHISupportsManualVertexFetch(GMaxRHIShaderPlatform) || IsGPUSkinCacheAvailable(GMaxRHIShaderPlatform);
            bSRV |= (PositionData && PositionData->GetAllowCPUAccess());
            if (bSRV)
            {
                PositionVertexBuffer.BufferSRV = RHICreateShaderResourceView(FShaderResourceViewInitializer(PositionData ? PositionVertexBuffer.VertexBufferRHI : nullptr, PF_R32_FLOAT));
            }

            if (bNeedsUAV)
            {
                PositionVertexBuffer.BufferUAV = RHICreateUnorderedAccessView(PositionVertexBuffer.VertexBufferRHI, PF_R32_FLOAT);
            }
        }
    }

    // Previous Positions
    {
        PrevPositionVertexBuffer.VertexBufferRHI = CreatePrevPositionRHIBuffer();
        if (PrevPositionVertexBuffer.VertexBufferRHI)
        {
            bool bSRV = RHISupportsManualVertexFetch(GMaxRHIShaderPlatform) || IsGPUSkinCacheAvailable(GMaxRHIShaderPlatform);
            bSRV |= (PrevPositionData && PrevPositionData->GetAllowCPUAccess());
            if (bSRV)
            {
                PrevPositionVertexBuffer.BufferSRV = RHICreateShaderResourceView(FShaderResourceViewInitializer(PrevPositionData ? PrevPositionVertexBuffer.VertexBufferRHI : nullptr, PF_R32_FLOAT));
            }

            if (bNeedsUAV)
            {
                PrevPositionVertexBuffer.BufferUAV = RHICreateUnorderedAccessView(PrevPositionVertexBuffer.VertexBufferRHI, PF_R32_FLOAT);
            }
        }
    }

    // Colors
    {
        ColorVertexBuffer.VertexBufferRHI = CreateColorRHIBuffer();
        if (ColorVertexBuffer.VertexBufferRHI && RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
        {
            ColorVertexBuffer.BufferSRV = RHICreateShaderResourceView(FShaderResourceViewInitializer(ColorData ? ColorVertexBuffer.VertexBufferRHI : nullptr, PF_R8G8B8A8));
        }

        // Note: for now color buffer is always populated with compute, so we need a UAV for it regardless.
        ColorVertexBuffer.BufferUAV = RHICreateUnorderedAccessView(ColorVertexBuffer.VertexBufferRHI, PF_R8G8B8A8);
    }

    // Tangents
    {
        TangentsVertexBuffer.VertexBufferRHI = CreateTangentsRHIBuffer();
        if (TangentsVertexBuffer.VertexBufferRHI && (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform) || IsGPUSkinCacheAvailable(GMaxRHIShaderPlatform)))
        {
            TangentsVertexBuffer.BufferSRV = RHICreateShaderResourceView(FShaderResourceViewInitializer(
                TangentsData ? TangentsVertexBuffer.VertexBufferRHI : nullptr, PF_R8G8B8A8_SNORM));
        }
        if (bNeedsUAV)
        {
            TangentsVertexBuffer.BufferUAV = RHICreateUnorderedAccessView(TangentsVertexBuffer.VertexBufferRHI, PF_R8G8B8A8_SNORM);
        }
    }

    // UVs
    {
        EPixelFormat texCoordFormat = bUseHighPrecision ? PF_G32R32F : PF_G16R16F;

        TexCoordVertexBuffer.VertexBufferRHI = CreateTexCoordRHIBuffer();
        if (TexCoordVertexBuffer.VertexBufferRHI && RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
        {
            TexCoordVertexBuffer.BufferSRV = RHICreateShaderResourceView(FShaderResourceViewInitializer(
                TexCoordData ? TexCoordVertexBuffer.VertexBufferRHI : nullptr, texCoordFormat));
        }
        if (bNeedsUAV)
        {
            TexCoordVertexBuffer.BufferUAV = RHICreateUnorderedAccessView(TexCoordVertexBuffer.VertexBufferRHI, texCoordFormat);
        }
    }

    GHoloMeshManager.AddMeshBytes(SizeBytes);
}

void FHoloMeshVertexBuffers::InitResource()
{
    FRenderResource::InitResource();

    PositionVertexBuffer.InitResource();
    PrevPositionVertexBuffer.InitResource();
    ColorVertexBuffer.InitResource();
    TangentsVertexBuffer.InitResource();
    TexCoordVertexBuffer.InitResource();
}
#endif

void FHoloMeshVertexBuffers::ReleaseRHI()
{
    PositionVertexBuffer.ReleaseRHI();
    PrevPositionVertexBuffer.ReleaseRHI();
    ColorVertexBuffer.ReleaseRHI();
    TangentsVertexBuffer.ReleaseRHI();
    TexCoordVertexBuffer.ReleaseRHI();

    GHoloMeshManager.RemoveMeshBytes(SizeBytes);
}

void FHoloMeshVertexBuffers::ReleaseResource()
{
    FRenderResource::ReleaseResource();
    PositionVertexBuffer.ReleaseResource();
    PrevPositionVertexBuffer.ReleaseResource();
    ColorVertexBuffer.ReleaseResource();
    TangentsVertexBuffer.ReleaseResource();
    TexCoordVertexBuffer.ReleaseResource();   
}

void FHoloMeshVertexBuffers::BindVertexBuffer(const FHoloMeshVertexFactory* VertexFactory, FHoloMeshVertexFactory::FDataType& MeshData, int LightMapCoordinateIndex) const
{
    FScopeLock Lock(&CriticalSection);

    //UE_LOG(LogHoloMesh, Warning, TEXT("Binding Read Vertex Buffer: %d"), ReadIndex);

    // Positions
    {
        MeshData.PositionComponent = FVertexStreamComponent(
            &PositionVertexBuffer,
            STRUCT_OFFSET(FPositionVertex, Position),
            PositionVertexBuffer.CachedStride,
            VET_Float3
        );

        if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
        {
            MeshData.PositionComponentSRV = PositionVertexBuffer.BufferSRV;
        }
    }

    // Prev Positions
    {
#if (ENGINE_MAJOR_VERSION == 5)
        MeshData.PreSkinPositionComponent = FVertexStreamComponent(
            &PrevPositionVertexBuffer,
            STRUCT_OFFSET(FPositionVertex, Position),
            PrevPositionVertexBuffer.CachedStride,
            VET_Float3
        );
#endif

        if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
        {
            MeshData.PreSkinPositionComponentSRV = PrevPositionVertexBuffer.BufferSRV;
        }
    }

    // Colors
    {
        MeshData.ColorIndexMask = ~0u;
        MeshData.ColorComponent = FVertexStreamComponent(
            &ColorVertexBuffer,
            0,	// Struct offset to color
            ColorVertexBuffer.CachedStride,
            VET_Color,
            EVertexStreamUsage::ManualFetch
        );

        if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
        {
            MeshData.ColorComponentsSRV = ColorVertexBuffer.BufferSRV;
        }
    }

    // Tangents (normals)
    {
        uint32 TangentSizeInBytes = 0;
        uint32 TangentXOffset = 0;
        uint32 TangentZOffset = 0;
        EVertexElementType TangentElemType = VET_None;

        typedef TStaticMeshVertexTangentDatum<typename TStaticMeshVertexTangentTypeSelector<EStaticMeshVertexTangentBasisType::Default>::TangentTypeT> TangentType;
        TangentElemType = TStaticMeshVertexTangentTypeSelector<EStaticMeshVertexTangentBasisType::Default>::VertexElementType;
        TangentXOffset = STRUCT_OFFSET(TangentType, TangentX);
        TangentZOffset = STRUCT_OFFSET(TangentType, TangentZ);
        TangentSizeInBytes = sizeof(TangentType);

        MeshData.TangentBasisComponents[0] = FVertexStreamComponent(
            &TangentsVertexBuffer,
            TangentXOffset,
            TangentSizeInBytes,
            TangentElemType,
            EVertexStreamUsage::ManualFetch
        );

        MeshData.TangentBasisComponents[1] = FVertexStreamComponent(
            &TangentsVertexBuffer,
            TangentZOffset,
            TangentSizeInBytes,
            TangentElemType,
            EVertexStreamUsage::ManualFetch
        );

        if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
        {
            MeshData.TangentsSRV = TangentsVertexBuffer.BufferSRV;
        }
    }

    // TexCoords
    {
        MeshData.TextureCoordinates.Empty();
        MeshData.NumTexCoords = GetNumTexCoords();

        uint32 UVSizeInBytes = sizeof(TStaticMeshVertexUVsTypeSelector<EStaticMeshVertexUVType::Default>::UVsTypeT);
        EVertexElementType UVDoubleWideVertexElementType = VET_Half4;
        EVertexElementType UVVertexElementType = VET_Half2;

        if (bUseHighPrecision)
        {
            UVSizeInBytes = sizeof(TStaticMeshVertexUVsTypeSelector<EStaticMeshVertexUVType::HighPrecision>::UVsTypeT);
            UVDoubleWideVertexElementType = VET_Float4;
            UVVertexElementType = VET_Float2;
        }

        uint32 UvStride = UVSizeInBytes * GetNumTexCoords();

        int32 UVIndex;
        for (UVIndex = 0; UVIndex < (int32)GetNumTexCoords() - 1; UVIndex += 2)
        {
            MeshData.TextureCoordinates.Add(FVertexStreamComponent(
                &TexCoordVertexBuffer,
                UVSizeInBytes * UVIndex,
                UvStride,
                UVDoubleWideVertexElementType,
                EVertexStreamUsage::ManualFetch
            ));
        }

        // possible last UV channel if we have an odd number
        if (UVIndex < (int32)GetNumTexCoords())
        {
            MeshData.TextureCoordinates.Add(FVertexStreamComponent(
                &TexCoordVertexBuffer,
                UVSizeInBytes * UVIndex,
                UvStride,
                UVVertexElementType,
                EVertexStreamUsage::ManualFetch
            ));
        }

        if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
        {
            MeshData.TextureCoordinatesSRV = TexCoordVertexBuffer.BufferSRV;
        }
    }

    // Lightmap Setup
    {
        LightMapCoordinateIndex = LightMapCoordinateIndex < (int32)GetNumTexCoords() ? LightMapCoordinateIndex : (int32)GetNumTexCoords() - 1;
        check(LightMapCoordinateIndex >= 0);

        MeshData.LightMapCoordinateIndex = LightMapCoordinateIndex;
        MeshData.NumTexCoords = GetNumTexCoords();

        uint32 UVSizeInBytes = sizeof(TStaticMeshVertexUVsTypeSelector<EStaticMeshVertexUVType::HighPrecision>::UVsTypeT);
        EVertexElementType UVVertexElementType = VET_Float2;

        uint32 UvStride = UVSizeInBytes * GetNumTexCoords();

        if (LightMapCoordinateIndex >= 0 && (uint32)LightMapCoordinateIndex < GetNumTexCoords())
        {
            MeshData.LightMapCoordinateComponent = FVertexStreamComponent(
                &TexCoordVertexBuffer,
                UVSizeInBytes * LightMapCoordinateIndex,
                UvStride,
                UVVertexElementType,
                EVertexStreamUsage::ManualFetch
            );
        }

        if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
        {
            MeshData.TextureCoordinatesSRV = TexCoordVertexBuffer.BufferSRV;
        }
    }
}

void FHoloMeshVertexBuffers::UpdateData()
{
    ENQUEUE_RENDER_COMMAND(FHoloMeshVertexBuffersUpdateData)(
        [&](FRHICommandListImmediate& RHICmdList)
        {
            UpdateData_RenderThread(RHICmdList, EHoloMeshUpdateFlags::All);
        });
}

void FHoloMeshVertexBuffers::UpdateData_RenderThread(FRHICommandListImmediate& RHICmdList, EHoloMeshUpdateFlags Flags)
{
    double update_start = FPlatformTime::Seconds();

    if (GetNumVertices())
    {
        // Positions
        if ((Flags & EHoloMeshUpdateFlags::Positions) != EHoloMeshUpdateFlags::None)
        {
            FResourceArrayInterface* RESTRICT ResourceArray = PositionData ? PositionData->GetResourceArray() : nullptr;
            const uint32 SizeInBytes = ResourceArray ? ResourceArray->GetResourceDataSize() : 0;

            HoloMeshUtilities::UploadVertexBuffer(PositionVertexBuffer.VertexBufferRHI, ResourceArray->GetResourceData(), SizeInBytes, &RHICmdList);
        }

        // Normals/Tangents
        if ((Flags & EHoloMeshUpdateFlags::Normals) != EHoloMeshUpdateFlags::None)
        {
            FResourceArrayInterface* RESTRICT ResourceArray = TangentsData ? TangentsData->GetResourceArray() : nullptr;
            const uint32 SizeInBytes = ResourceArray ? ResourceArray->GetResourceDataSize() : 0;

            HoloMeshUtilities::UploadVertexBuffer(TangentsVertexBuffer.VertexBufferRHI, ResourceArray->GetResourceData(), SizeInBytes, &RHICmdList);
        }

        // Colors
        if ((Flags & EHoloMeshUpdateFlags::Colors) != EHoloMeshUpdateFlags::None)
        {
            FResourceArrayInterface* RESTRICT ResourceArray = ColorData ? ColorData->GetResourceArray() : nullptr;
            const uint32 SizeInBytes = ResourceArray ? ResourceArray->GetResourceDataSize() : 0;

            HoloMeshUtilities::UploadVertexBuffer(ColorVertexBuffer.VertexBufferRHI, ResourceArray->GetResourceData(), SizeInBytes, &RHICmdList);
        }
    }

    // UVs
    if (GetNumTexCoords() && ((Flags & EHoloMeshUpdateFlags::UVs) != EHoloMeshUpdateFlags::None))
    {
        FResourceArrayInterface* RESTRICT ResourceArray = TexCoordData ? TexCoordData->GetResourceArray() : nullptr;
        const uint32 SizeInBytes = ResourceArray ? ResourceArray->GetResourceDataSize() : 0;

        HoloMeshUtilities::UploadVertexBuffer(TexCoordVertexBuffer.VertexBufferRHI, ResourceArray->GetResourceData(), SizeInBytes, &RHICmdList);
    }

#if HOLOMESH_BUFFER_DEBUG
    double update_end = FPlatformTime::Seconds();
    UE_LOG(LogHoloMesh, Warning, TEXT("HoloMesh Vertex Update: %f"), ((update_end - update_start) * 1000.0f));
#endif
}

// --- Texture ---

FHoloMeshTexture::FHoloMeshTexture()
{
    Texture = nullptr;
    Width = 0;
    Height = 0;
    NumMips = 0;
    Format = PF_Unknown;
}

FHoloMeshTexture::~FHoloMeshTexture()
{
    Release();
}

void FHoloMeshTexture::Create(size_t width, size_t height, EPixelFormat format, uint32_t numMips, TextureFilter filter)
{
    Release();

    Width = width;
    Height = height;
    Format = format;
    NumMips = numMips;

    Texture = UTexture2D::CreateTransient(Width, Height, Format);

    FTexturePlatformData* PlatformData = new FTexturePlatformData();
    PlatformData->SizeX = Width;
    PlatformData->SizeY = Height;
    PlatformData->PixelFormat = Format;

    for (uint32 MipIt = 0; MipIt < NumMips; ++MipIt)
    {
        const uint32 MipResolutionX = Width >> MipIt;
        const uint32 MipResolutionY = Height >> MipIt;

        int32 NumBlocksX = MipResolutionX / GPixelFormats[Format].BlockSizeX;
        int32 NumBlocksY = MipResolutionY / GPixelFormats[Format].BlockSizeY;
        FTexture2DMipMap* Mip = new FTexture2DMipMap();
        PlatformData->Mips.Add(Mip);
        Mip->SizeX = MipResolutionX;
        Mip->SizeY = MipResolutionY;
        Mip->BulkData.Lock(LOCK_READ_WRITE);
        Mip->BulkData.Realloc(NumBlocksX * NumBlocksY * GPixelFormats[Format].BlockBytes);
        Mip->BulkData.Unlock();
    }

#if (ENGINE_MAJOR_VERSION == 5)
    Texture->SetPlatformData(PlatformData);
#else
    Texture->PlatformData = PlatformData;
#endif

    Texture->AddToRoot();
    Texture->Filter = filter;
    Texture->SRGB = 0;
    Texture->VirtualTextureStreaming = false;
    Texture->CompressionSettings = TextureCompressionSettings::TC_Displacementmap;

#if WITH_EDITORONLY_DATA
    Texture->DeferCompression = 1;
#endif

    Texture->UpdateResource();

    GHoloMeshManager.AddTextureBytes(GetTextureSizeBytes());
}

void FHoloMeshTexture::Release()
{
    if (Texture != nullptr)
    {
        GHoloMeshManager.RemoveTextureBytes(GetTextureSizeBytes());
        Texture->RemoveFromRoot();
        Texture = nullptr;
    }
}

int FHoloMeshTexture::GetTextureSizeBytes()
{
    double bytesPerPixel = 4.0;
    if (Format == EPixelFormat::PF_R8)
    {
        bytesPerPixel = 1.0;
    }
    if (Format == EPixelFormat::PF_R8G8)
    {
        bytesPerPixel = 2.0;
    }
    if (Format == EPixelFormat::PF_BC4)
    {
        bytesPerPixel = 0.5;
    }

    int totalSize = 0;
    int width = Width;
    int height = Height;
    int mipCount = NumMips;

    for (int i = 0; i < mipCount; ++i)
    {
        totalSize += (width * height * bytesPerPixel);
        width = width / 2;
        height = height / 2;
    }

    return totalSize;
}

FTexture2DRHIRef FHoloMeshTexture::GetTextureRHI()
{
    if (Texture == nullptr)
    {
        return nullptr;
    }

    FTextureResource* texResource = Texture->GetResource();
    if (texResource == nullptr)
    {
        return nullptr;
    }

    return texResource->GetTexture2DRHI();
}

// --- DataTexture ---

static float identityMatrix[16] =
{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

FHoloMeshDataTexture::FHoloMeshDataTexture()
{
    Texture = nullptr;
    TextureData = nullptr;
    SrcPitch = 0;
    SrcWidth = 0;
}

FHoloMeshDataTexture::~FHoloMeshDataTexture()
{
    Release();
}

void FHoloMeshDataTexture::Create(size_t width)
{
    Release();

    SrcPitch = width * 32;
    SrcWidth = width;

    Texture = UTexture2D::CreateTransient(width, 1, EPixelFormat::PF_A32B32G32R32F);
    Texture->AddToRoot();
    Texture->Filter = TextureFilter::TF_Nearest;
    Texture->SRGB = 0;
    Texture->VirtualTextureStreaming = false;
    Texture->CompressionSettings = TextureCompressionSettings::TC_Displacementmap;

#if WITH_EDITORONLY_DATA
    Texture->DeferCompression = 1;
#endif

    Texture->UpdateResource();

    TextureData = new float[width * 4];
}

void FHoloMeshDataTexture::Release()
{
    if (Texture != nullptr)
    {
        Texture->RemoveFromRoot();
        Texture = nullptr;
    }

    if (TextureData != nullptr)
    {
        delete[] TextureData;
    }
}

void FHoloMeshDataTexture::SetData(size_t destX, size_t width, float* data)
{
    if (Texture == nullptr)
    {
        return;
    }

    memcpy(&TextureData[destX * 4], data, width * 4 * 4);
}

void FHoloMeshDataTexture::Update()
{
    FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, SrcWidth, 1);
    Texture->UpdateTextureRegions(0, 1, Region, SrcPitch, 32, (uint8_t*)TextureData);
}

void FHoloMeshDataTexture::SetToIdentity(int index)
{
    if (Texture == nullptr)
    {
        return;
    }

    if (index < 0)
    {
        int matrixCount = (SrcWidth / 4);
        for (int i = 0; i < matrixCount; ++i)
        {
            memcpy(&TextureData[i * 16], identityMatrix, sizeof(float) * 16);
        }
    }
    else
    {
        SetData(index * 4, 4, identityMatrix);
    }
}

// --- RenderTarget ---

FHoloMeshRenderTarget::FHoloMeshRenderTarget()
{
    RenderTarget = nullptr;
    RenderTargetResource = nullptr;

    TextureWidth = 0;
    TextureHeight = 0;
    TextureFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    bHasMips = false;
}

FHoloMeshRenderTarget::~FHoloMeshRenderTarget()
{
    Release();
}

int FHoloMeshRenderTarget::GetTextureSizeBytes()
{
    int bytesPerPixel = 4;
    if (TextureFormat == ETextureRenderTargetFormat::RTF_R8)
    {
        bytesPerPixel = 1;
    }
    if (TextureFormat == ETextureRenderTargetFormat::RTF_RG8)
    {
        bytesPerPixel = 2;
    }

    int totalSize = 0;
    int width = TextureWidth;
    int height = TextureHeight;
    int mipCount = bHasMips ? 8 : 1;

    for (int i = 0; i < mipCount; ++i)
    {
        totalSize += (width * height * bytesPerPixel);
        width = width / 2;
        height = height / 2;
    }

    return totalSize;
}

void FHoloMeshRenderTarget::Create(size_t _width, size_t _height, ETextureRenderTargetFormat _format, TextureFilter filter, bool _generateMips)
{
    Release();

    TextureWidth = _width;
    TextureHeight = _height;
    TextureFormat = _format;
    bHasMips = _generateMips;
    
    RenderTarget = NewObject<UTextureRenderTarget2D>();
    RenderTarget->ClearColor = FLinearColor::Transparent;
    RenderTarget->bCanCreateUAV = true;
    RenderTarget->SRGB = false;
    RenderTarget->bHDR_DEPRECATED = 0;
    RenderTarget->RenderTargetFormat = _format;
    RenderTarget->Filter = filter;
    RenderTarget->bAutoGenerateMips = _generateMips;
    RenderTarget->InitAutoFormat(_width, _height);
    RenderTarget->UpdateResourceImmediate();
    RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();

    GHoloMeshManager.AddTextureBytes(GetTextureSizeBytes());
}

void FHoloMeshRenderTarget::Release()
{
    if (RenderTarget != nullptr)
    {
        GHoloMeshManager.RemoveTextureBytes(GetTextureSizeBytes());
    }
    RenderTarget = nullptr;
}

FUnorderedAccessViewRHIRef FHoloMeshRenderTarget::GetRenderTargetUAV(int mipLevel)
{ 
    if (mipLevel >= 8)
    {
        return nullptr;
    }

    if (!RenderTargetUAV[mipLevel].IsValid() && RenderTargetResource != nullptr)
    {
        FTexture2DRHIRef renderTargetTex = RenderTargetResource->GetRenderTargetTexture();
        if (renderTargetTex.IsValid())
        {
            RenderTargetUAV[mipLevel] = RHICreateUnorderedAccessView(renderTargetTex, mipLevel);
        }
    }

    return RenderTargetUAV[mipLevel];
}