// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "HoloMeshVertexFactory.h"
#include "HoloMeshUtilities.h"

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "LocalVertexFactory.h"
#include "Misc/EnumClassFlags.h"
#include "Rendering/PositionVertexBuffer.h"
#include "RenderResource.h"
#include "RHIResources.h"
#include "Runtime/Launch/Resources/Version.h"
#include "StaticMeshVertexData.h"
#include "TextureResource.h"
#include "VertexFactory.h"

#include "HoloMeshBuffers.generated.h"

enum class EHoloMeshUpdateFlags : uint8
{
    None        = 0,
    Indices     = 1 << 0,
    Positions   = 1 << 1,
    Normals     = 1 << 2,
    Colors      = 1 << 3,
    UVs         = 1 << 4,
    All         = 0xff
};
ENUM_CLASS_FLAGS(EHoloMeshUpdateFlags)

// Index Buffer
class HOLOMESH_API FHoloMeshIndexBuffer : public FRenderResource
{
public:

    // Helper class to make it easier to write 
    class IndexWriter
    {
        public:
            IndexWriter(FHoloMeshIndexBuffer* indexBuffer)
            {
                data = (uint8_t*)indexBuffer->GetIndexData32();
                pos = 0;
                stride = indexBuffer->Use32Bit() ? sizeof(uint32_t) : sizeof(uint16_t);
            }

            inline void Write(uint16_t* index, int count = 1)
            {
                memcpy(&data[pos], index, count * stride);
                pos += stride * count;
            }

            inline void Write(uint32_t* index, int count = 1)
            {
                memcpy(&data[pos], index, count * stride);
                pos += stride * count;
            }

            inline void Zero(int start, int count)
            {
                if (count <= 0)
                {
                    return;
                }

                memset(&data[start * stride], 0, count * stride);
            }

        private:
            uint8_t* data;
            uint8_t stride;
            size_t pos;
    };

    FHoloMeshIndexBuffer();
    ~FHoloMeshIndexBuffer();

    void Create(uint32 InNumIndices, bool bInUse32Bit = false, bool bInNeedsUAV = false);

    bool IsInitialized()
    {
        if (IndexBuffer.IsInitialized())
        {
            return true;
        }
        return false;
    }

    void InitOrUpdate();
    void SwapData(FHoloMeshIndexBuffer* srcIndexBuffer);
    void Clear(uint32 startingIndex = 0);

    void UpdateData();
    void UpdateData_RenderThread(FRHICommandListImmediate& RHICmdList);

    uint32  GetNumIndices() const;
    bool    Use32Bit() { return bUse32Bit; }

    uint32 GetUsedIndices() { return UsedIndices; }
    void SetUsedIndices(uint32 count)
    {
        UsedIndices = count;
    }

    TArray<uint32>* GetData() { return IndexData; }
    TArray<uint32>* TakeData() 
    { 
        auto tmpPtr = IndexData;
        IndexData = nullptr;
        return tmpPtr;
    }
    uint16* GetIndexData16() { return (uint16*)IndexData->GetData(); }
    uint32* GetIndexData32() { return (uint32*)IndexData->GetData(); }

    FUnorderedAccessViewRHIRef GetIndexBufferUAV() { return IndexBufferUAV; }
    FIndexBuffer* GetIndexBufferRef() { return &IndexBuffer; }

    // FRenderResource interface.
#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
    virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
    virtual void InitResource(FRHICommandListBase& RHICmdList) override;
#else
    virtual void InitRHI() override;
    virtual void InitResource() override;
#endif

    virtual void ReleaseRHI() override;
    virtual void ReleaseResource() override;

protected:
    bool bInitialized  = false;
    bool bUse32Bit     = false;
    bool bNeedsUAV     = false;

    uint32_t UsedIndices = 0;
    uint32_t SizeBytes = 0;

    TArray<uint32>* IndexData;
    FIndexBuffer IndexBuffer;
    FUnorderedAccessViewRHIRef IndexBufferUAV;
    FCriticalSection CriticalSection;
};

// Simple wrapper over FVertexBuffer to also store associated SRV and UAV.
class HOLOMESH_API FHoloMeshBuffer : public FVertexBuffer
{
public:
    FShaderResourceViewRHIRef BufferSRV;
    FUnorderedAccessViewRHIRef BufferUAV;
    uint32 CachedStride;

    virtual FString GetFriendlyName() const override { return TEXT("HoloMeshBuffer"); }
};

class FPositionVertexData :
    public TStaticMeshVertexData<FPositionVertex>
{
public:
    FPositionVertexData(bool InNeedsCPUAccess = false)
        : TStaticMeshVertexData<FPositionVertex>(InNeedsCPUAccess)
    {
    }
};

class FColorVertexData :
    public TStaticMeshVertexData<FColor>
{
public:
    FColorVertexData(bool InNeedsCPUAccess = false)
        : TStaticMeshVertexData<FColor>(InNeedsCPUAccess)
    {
    }
};

// Vertex Buffers.
// Containers multiple buffers: positions, colors, tangents, and texcoords.
class HOLOMESH_API FHoloMeshVertexBuffers : public FRenderResource
{
public:

    FHoloMeshVertexBuffers();
    ~FHoloMeshVertexBuffers();

    // Delete existing resources
    void CleanUp();

    // Sets the number of vertices and texcoords and allocates the requires buffers to hold them.
    void Create(uint32 NumVertices,
        uint32 NumTexCoords = 1, 
        bool bInNeedsUAV = false, 
        bool bInUseHighPrecision = false,
        bool bInNeedsCPUAccess = true);

    bool IsInitialized()
    {
        return bInitialized;
    }

    void SwapData(FHoloMeshVertexBuffers* srcVertexBuffers);

    // Initialize (or update) render resources.
    void InitOrUpdate(FHoloMeshVertexFactory* VertexFactory, uint32 LightMapIndex = 0);

    // FRenderResource interface.
#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
    virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
    virtual void InitResource(FRHICommandListBase& RHICmdList) override;
#else
    virtual void InitRHI() override;
    virtual void InitResource() override;
#endif
    
    virtual void ReleaseRHI() override;
    virtual void ReleaseResource() override;
    virtual FString GetFriendlyName() const override { return TEXT("HoloMesh VertexBuffers"); }

    // Accessors.
    FORCEINLINE TMemoryImagePtr<class FPositionVertexData> GetPositionData() const
    {
        return PositionData;
    }
    FORCEINLINE TMemoryImagePtr<class FPositionVertexData> GetPrevPositionData() const
    {
        return PrevPositionData;
    }
    FORCEINLINE class FColorVertexData* GetColorData() const
    {
        return ColorData;
    }
    FORCEINLINE FStaticMeshVertexDataInterface* GetTangentsData() const
    {
        return TangentsData;
    }
    FORCEINLINE FStaticMeshVertexDataInterface* GetTexCoordData() const
    {
        return TexCoordData;
    }

    FORCEINLINE TMemoryImagePtr<class FPositionVertexData> TakePositionData()
    {
        auto tmpPtr = PositionData;
        PositionData = nullptr;
        return tmpPtr;
    }
    FORCEINLINE TMemoryImagePtr<class FPositionVertexData> TakePrevPositionData()
    {
        auto tmpPtr = PrevPositionData;
        PrevPositionData = nullptr;
        return tmpPtr;
    }
    FORCEINLINE class FColorVertexData* TakeColorData()
    {
        auto tmpPtr = ColorData;
        ColorData = nullptr;
        return tmpPtr;
    }
    FORCEINLINE FStaticMeshVertexDataInterface* TakeTangentsData()
    {
        auto tmpPtr = TangentsData;
        TangentsData = nullptr;
        return tmpPtr;
    }
    FORCEINLINE FStaticMeshVertexDataInterface* TakeTexCoordData()
    {
        auto tmpPtr = TexCoordData;
        TexCoordData = nullptr;
        return tmpPtr;
    }

    FORCEINLINE uint32 GetNumVertices() const
    {
        return NumVertices;
    }
    FORCEINLINE_DEBUGGABLE uint32 GetNumTexCoords() const
    {
        return NumTexCoords;
    }

    FRHIShaderResourceView* GetPositionBufferSRV() const { return PositionVertexBuffer.BufferSRV; }
    FRHIShaderResourceView* GetPrevPositionBufferSRV() const { return PrevPositionVertexBuffer.BufferSRV; }
    FRHIShaderResourceView* GetColorBufferSRV() const { return ColorVertexBuffer.BufferSRV; }
    FRHIShaderResourceView* GetTangentsBufferSRV() const { return TangentsVertexBuffer.BufferSRV; }
    FRHIShaderResourceView* GetTexCoordBufferSRV() const { return TexCoordVertexBuffer.BufferSRV; }

    FUnorderedAccessViewRHIRef GetPositionBufferUAV() const { return PositionVertexBuffer.BufferUAV; }
    FUnorderedAccessViewRHIRef GetPrevPositionBufferUAV() const { return PrevPositionVertexBuffer.BufferUAV; }
    FUnorderedAccessViewRHIRef GetColorBufferUAV() const { return ColorVertexBuffer.BufferUAV; }
    FUnorderedAccessViewRHIRef GetTangentsBufferUAV() const { return TangentsVertexBuffer.BufferUAV; }
    FUnorderedAccessViewRHIRef GetTexCoordBufferUAV() const { return TexCoordVertexBuffer.BufferUAV; }

    void BindVertexBuffer(const class FHoloMeshVertexFactory* VertexFactory, struct FHoloMeshVertexFactory::FDataType& Data, int LightMapCoordinateIndex) const;
    void UpdateData();
    void UpdateData_RenderThread(FRHICommandListImmediate& RHICmdList, EHoloMeshUpdateFlags Flags);

private:

    mutable FCriticalSection CriticalSection;
    uint32 NumVertices;
    uint32 NumTexCoords;
    uint32 SizeBytes;
    bool bInitialized	   = false;
    bool bUseHighPrecision = false;
    bool bNeedsCPUAccess   = true;
    bool bNeedsUAV		   = false;
    bool bDoubleBuffer	   = true;

    // CPU Side Data
    TMemoryImagePtr<class FPositionVertexData> PositionData;
    TMemoryImagePtr<class FPositionVertexData> PrevPositionData;
    class FColorVertexData* ColorData;
    FStaticMeshVertexDataInterface* TangentsData;
    FStaticMeshVertexDataInterface* TexCoordData;

    // Buffers
    FHoloMeshBuffer PositionVertexBuffer;
    FHoloMeshBuffer PrevPositionVertexBuffer;
    FHoloMeshBuffer ColorVertexBuffer;
    FHoloMeshBuffer TangentsVertexBuffer;
    FHoloMeshBuffer TexCoordVertexBuffer;

    // Create an RHI vertex buffer with CPU data. CPU data may be discarded after creation (see TResourceArray::Discard)
    FHoloMeshBufferRHIRef CreatePositionRHIBuffer();
    FHoloMeshBufferRHIRef CreatePrevPositionRHIBuffer();
    FHoloMeshBufferRHIRef CreateColorRHIBuffer();
    FHoloMeshBufferRHIRef CreateTangentsRHIBuffer();
    FHoloMeshBufferRHIRef CreateTexCoordRHIBuffer();
};

// Texture Wrapper
USTRUCT()
struct HOLOMESH_API FHoloMeshTexture
{
    GENERATED_BODY()

    uint32_t Width;
    uint32_t Height;
    EPixelFormat Format;
    uint32_t NumMips;

    UPROPERTY()
    UTexture2D* Texture;

    FHoloMeshTexture();
    ~FHoloMeshTexture();

    void Create(size_t width, size_t height, EPixelFormat format = PF_R8G8B8A8, uint32_t numMips = 1, TextureFilter filter = TextureFilter::TF_Bilinear);
    void Release();

    int GetTextureSizeBytes();
    FTexture2DRHIRef GetTextureRHI();
    inline UTexture2D* GetTexture() { return Texture; }
    inline bool IsValid() { return Texture != nullptr; }
};

// DataTexture
// Primarily used for SSDR and Retarget bone matrices.
USTRUCT()
struct HOLOMESH_API FHoloMeshDataTexture
{
    GENERATED_BODY()

    float* TextureData;
    uint32_t SrcPitch;
    uint32_t SrcWidth;

    UPROPERTY()
    UTexture2D* Texture;

    FHoloMeshDataTexture();
    ~FHoloMeshDataTexture();

    void Create(size_t width);
    void Release();

    // Returns a pointer thats safe for CPU side writing.
    inline float* GetData() { return TextureData; }

    // Copies the data provided by the pointer.
    void SetData(size_t destX, size_t width, float* data);

    // Uploads the data to the GPU.
    void Update();

    // Treats the data texture as an array of 4x4 float matrices,
    // and either sets all or one of them to an identity matrix.
    void SetToIdentity(int index = -1);

    inline UTexture2D* GetTexture() { return Texture; }
    inline bool IsValid() { return Texture != nullptr; }
};

// Texture
// Used for Luma texture decoding. Supports render target and UAV.
USTRUCT()
struct HOLOMESH_API FHoloMeshRenderTarget
{
    GENERATED_BODY()

    uint32_t TextureWidth;
    uint32_t TextureHeight;
    ETextureRenderTargetFormat TextureFormat;
    bool bHasMips = false;
    bool bIsClear = false;

    UPROPERTY()
    UTextureRenderTarget2D* RenderTarget;

    FTextureRenderTargetResource* RenderTargetResource;
    FUnorderedAccessViewRHIRef RenderTargetUAV[8];

    FHoloMeshRenderTarget();
    ~FHoloMeshRenderTarget();

    void Create(size_t _width, size_t _height, ETextureRenderTargetFormat _format = RTF_RGBA8, TextureFilter filter = TextureFilter::TF_Bilinear, bool _generateMips = false);
    void Release();

    inline UTextureRenderTarget2D* GetRenderTarget() { return RenderTarget; }
    inline FTexture2DRHIRef GetRenderTargetRHI()
    {
        if (RenderTargetResource != nullptr)
        {
            return RenderTargetResource->GetRenderTargetTexture();
        }
        return nullptr;
    }
    FUnorderedAccessViewRHIRef GetRenderTargetUAV(int mipLevel = 0);

    inline size_t GetWidth() { return TextureWidth; }
    inline size_t GetHeight() { return TextureWidth; }
    int GetTextureSizeBytes();

    inline bool IsValid() { return RenderTarget != nullptr && RenderTargetResource != nullptr; }

    inline bool IsClear() { return bIsClear; }
    void SetClearFlag(bool isClear) { bIsClear = isClear; }
};