#pragma once

// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.
// AVV Version 0.6

#define AVV_VERSION_MAJOR 0
#define AVV_VERSION_MINOR 6
#define AVV_VERSION (AVV_VERSION_MAJOR << 16) + AVV_VERSION_MINOR

// Container Type Categories
#define AVV_META_CONTAINER         (1 << 8)
#define AVV_SEGMENT_CONTAINER      (1 << 9)
#define AVV_FRAME_CONTAINER        (1 << 10)

// Container Type Subcategories
#define AVV_VERTEX_POS             (1 << 11)
#define AVV_VERTEX_UVS             (1 << 12)
#define AVV_VERTEX_NORMALS         (1 << 13)
#define AVV_VERTEX_COLORS          (1 << 14)
#define AVV_VERTEX_ANIM            (1 << 15)
#define AVV_TRIS                   (1 << 16)
#define AVV_TEXTURE                (1 << 17)
#define AVV_SKELETON               (1 << 18)
#define AVV_MOTION_VECTORS         (1 << 19)

// Meta Container Types
#define AVV_META_SEGMENT_TABLE                     (0x01 | AVV_META_CONTAINER)
#define AVV_META_LIMITS                            (0x02 | AVV_META_CONTAINER)
#define AVV_META_SKELETON                          (0x03 | AVV_SKELETON | AVV_META_CONTAINER)

// Segment Container Types
#define AVV_SEGMENT_FRAMES                         (0x01 | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_POS_16                         (0x01 | AVV_VERTEX_POS | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_POS_SKIN_EXPAND_128            (0x01 | AVV_VERTEX_POS | AVV_VERTEX_ANIM | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_POS_SKIN_EXPAND_128_V2         (0x02 | AVV_VERTEX_POS | AVV_VERTEX_ANIM | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_UVS_12_NORMALS_888             (0x01 | AVV_VERTEX_UVS | AVV_VERTEX_NORMALS | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_UVS_16                         (0x01 | AVV_VERTEX_UVS | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_TRIS_16                        (0x01 | AVV_TRIS | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_TRIS_32                        (0x02 | AVV_TRIS | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_TEXTURE_TRIS_16                (0x01 | AVV_TEXTURE | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_TEXTURE_TRIS_32                (0x02 | AVV_TEXTURE | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_TEXTURE_BLOCKS_32              (0x03 | AVV_TEXTURE | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_TEXTURE_BLOCKS_MULTIRES_32     (0x04 | AVV_TEXTURE | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_TEXTURE_VERTEX_MASK            (0x05 | AVV_TEXTURE | AVV_SEGMENT_CONTAINER)
#define AVV_SEGMENT_MOTION_VECTORS                 (0x01 | AVV_MOTION_VECTORS | AVV_SEGMENT_CONTAINER)

// Frame Container Types
#define AVV_FRAME_ANIM_MAT4X4_32                   (0x01 | AVV_VERTEX_ANIM | AVV_FRAME_CONTAINER)
#define AVV_FRAME_ANIM_POS_ROTATION_128            (0x02 | AVV_VERTEX_ANIM | AVV_FRAME_CONTAINER)
#define AVV_FRAME_ANIM_DELTA_POS_32                (0x03 | AVV_VERTEX_ANIM | AVV_FRAME_CONTAINER)
#define AVV_FRAME_TEXTURE_LUMA_8                   (0x01 | AVV_TEXTURE | AVV_FRAME_CONTAINER)
#define AVV_FRAME_TEXTURE_LUMA_BC4                 (0x02 | AVV_TEXTURE | AVV_FRAME_CONTAINER)
#define AVV_FRAME_COLORS_RGB_565                   (0x01 | AVV_VERTEX_COLORS | AVV_FRAME_CONTAINER)
#define AVV_FRAME_COLORS_RGB_565_NORMALS_OCT_16    (0x01 | AVV_VERTEX_COLORS | AVV_VERTEX_NORMALS | AVV_FRAME_CONTAINER)

#include "HoloMeshComponent.h"
#include "HoloMeshManager.h"
#include "HoloMeshSkeleton.h"

struct AVVSegmentTableEntry
{
    uint32_t byteStart = 0;
    uint32_t byteLength = 0;
    uint32_t frameCount = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};

struct AVVSkeleton
{
    struct BoneInfo
    {
        int32_t parentIndex = -1;
        char name[32];
    };
    std::vector<BoneInfo> boneInfo;

    uint32_t skeletonIndex = 0;
    uint32_t boneCount = 0;
    std::vector<FHoloMeshVec3> positions;
    std::vector<FHoloMeshVec4> rotations;

    FHoloSkeleton AVVToHoloSkeleton()
    {
        FHoloSkeleton holoSkeleton;

        holoSkeleton.skeletonIndex = skeletonIndex;
        holoSkeleton.boneCount = boneCount;
        holoSkeleton.positions = positions;
        holoSkeleton.rotations = rotations;
        for (int i = 0; i < boneInfo.size(); i++)
        {
            holoSkeleton.boneNames.push_back(FString(UTF8_TO_TCHAR(boneInfo.at(i).name)));
            holoSkeleton.boneParentIndexes.push_back(boneInfo.at(i).parentIndex);
        }

        return holoSkeleton;
    }
};

struct AVVEncodedFrame
{
    uint32_t frameIndex;
    FHoloMemoryBlockRef content = nullptr;
    FHoloMemoryBlockRef textureContent = nullptr;
    std::atomic<int> activeUploadCount = { 0 };
    std::atomic<bool> processed = { false };

    uint32_t ssdrBoneCount = 0;
    float* ssdrMatrixData = nullptr;

    uint32_t deltaPosCount = 0;
    uint32_t deltaDataOffset = 0;
    uint32_t deltaDataSize = 0;
    float deltaAABBMin[3];
    float deltaAABBMax[3];

    uint32_t colorCount = 0;
    uint32_t normalCount = 0;
    uint32_t colorDataOffset = 0;
    uint32_t colorDataSize;

    uint32_t lumaCount = 0;
    uint32_t lumaDataOffset = 0;
    uint32_t lumaDataSize;

    bool blockDecode = false;
    uint32_t blockCount = 0;

    AVVSkeleton skeleton;

    void Create(SIZE_T SizeInBytes, SIZE_T TextureSizeInBytes)
    {
        content = GHoloMeshManager.AllocBlock(SizeInBytes);
        if (TextureSizeInBytes > 0)
        {
            textureContent = GHoloMeshManager.AllocBlock(TextureSizeInBytes);
        }
    }

    void Release()
    {
        GHoloMeshManager.FreeBlock(content);
        if (textureContent != nullptr)
        {
            GHoloMeshManager.FreeBlock(textureContent);
        }

        if (ssdrMatrixData != nullptr)
        {
            FMemory::Free(ssdrMatrixData);
        }
    }
};

struct AVVEncodedTextureInfo
{
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t blockCount = 0;
    uint32_t blockDataOffset;
    uint32_t blockDataSize;
    std::vector<uint32_t> levelBlockCounts;
    bool multiRes = false;
};

struct AVVEncodedSegment
{
    uint32_t segmentIndex;
    FHoloMemoryBlockRef content;
    std::atomic<int> activeUploadCount = { 0 };
    std::atomic<bool> processed = { false };

    // Vertex Data
    bool posOnlySegment = false;
    float aabbMin[3];
    float aabbMax[3];
    uint32_t vertexCount = 0;
    uint32_t compactVertexCount;
    uint32_t vertexDataOffset;
    uint32_t vertexDataSize;

    // Used in SegmentPosSkinExpand128 v1
    uint32_t expansionListCount;
    uint32_t expansionListOffset;
    TArray<uint32> vertexWriteTable;

    // Used in SegmentPosSkinExpand128 v2
    uint32_t vertexWriteTableOffset;

    // Index Data
    bool index32Bit = false;
    uint32_t indexCount = 0;
    uint32_t indexDataOffset;
    uint32_t indexDataSize;

    // UV Data
    uint32_t uvCount = 0;
    uint32_t uvDataOffset;
    uint32_t uvDataSize;

    // Normal Data
    bool uv12normal888 = false;

    // Texture Data
    AVVEncodedTextureInfo texture;

    // Motion Vectors
    bool motionVectors = false;
    float motionVectorsMin[3];
    float motionVectorsMax[3];
    uint32_t motionVectorsCount = 0;
    uint32_t motionVectorsDataOffset = 0;
    uint32_t motionVectorsDataSize = 0;

    FHoloMeshVec3 GetAABBMin() { return FHoloMeshVec3(aabbMin[0], aabbMin[1], aabbMin[2]); }
    FHoloMeshVec3 GetAABBMax() { return FHoloMeshVec3(aabbMax[0], aabbMax[1], aabbMax[2]); }

    void Create(int SizeInBytes)
    {
        content = GHoloMeshManager.AllocBlock(SizeInBytes);
    }

    void Release()
    {
        GHoloMeshManager.FreeBlock(content);
    }
};

struct AVVVertex
{
    float position[3];
    float uv[2];
    float ssdrWeights[4];
    uint8_t ssdrIndices[4];
};

struct AVVTextureBlock
{
    uint16_t blockX;
    uint16_t blockY;
};

struct AVVLimits
{
    uint32_t MaxContainerSize = 0;
    uint32_t MaxVertexCount = 0;
    uint32_t MaxIndexCount = 0;
    uint32_t MaxFrameCount = 0;
    uint32_t MaxBoneCount = 0;
    uint32_t MaxTextureWidth = 0;
    uint32_t MaxTextureHeight = 0;
    uint32_t MaxTextureTriangles = 0;
    uint32_t MaxTextureBlocks = 0;
    uint32_t MaxLumaPixels = 0;
};

// 128 bit (16 byte) pos and rotation
struct PosQuat128
{
    uint16_t posX;  // 16 bit
    uint16_t posY;  // 16 bit
    uint16_t posZ;  // 16 bit 
    uint32_t quatX; // 20 bit
    uint32_t quatY; // 20 bit
    uint32_t quatZ; // 20 bit
    uint32_t quatW; // 20 bit

    void pack(uint64_t& outPacked0, uint64_t& outPacked1)
    {
        outPacked0 = ((uint64_t)posX << 48) | ((uint64_t)posY << 32) | ((uint64_t)posZ << 16) | ((uint64_t)quatX >> 4);
        outPacked1 = ((uint64_t)quatX << 60) | ((uint64_t)quatY << 40) | ((uint64_t)quatZ << 20) | (uint64_t)quatW;
    }

    void unpack(uint64_t& packed0, uint64_t& packed1)
    {
        posX = packed0 >> 48;
        posY = packed0 >> 32;
        posZ = packed0 >> 16;
        quatX = ((packed0 & 0xFFFF) << 4) | (packed1 >> 60);
        quatY = (packed1 >> 40) & 0xFFFFF;
        quatZ = (packed1 >> 20) & 0xFFFFF;
        quatW = (packed1 >> 0) & 0xFFFFF;
    }
};