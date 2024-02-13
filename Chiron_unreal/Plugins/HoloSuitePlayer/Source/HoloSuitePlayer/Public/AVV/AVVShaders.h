// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "Shader.h"
#include "ShaderCompilerCore.h"

#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
const int32 HLSL_2021 = 1;
#else
const int32 HLSL_2021 = 0;
#endif

inline bool SupportsComputeShaders(const FStaticShaderPlatform Platform)
{
#if (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1)
    return true;
#else
    return RHISupportsComputeShaders(Platform);
#endif
}

// AVV_SEGMENT_POS_16
class FAVVDecodePos16_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodePos16_CS)
        SHADER_USE_PARAMETER_STRUCT(FAVVDecodePos16_CS, FGlobalShader)

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint3>, VertexDataBuffer)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, DecodedVertexBuffer)
        SHADER_PARAMETER(uint32, gVertexCount)
        SHADER_PARAMETER(FHoloMeshVec3, gAABBMin)
        SHADER_PARAMETER(FHoloMeshVec3, gAABBMax)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// AVV_SEGMENT_POS_SKIN_EXPAND_128
class FAVVDecodePosSkinExpand_128_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodePosSkinExpand_128_CS)
    SHADER_USE_PARAMETER_STRUCT(FAVVDecodePosSkinExpand_128_CS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint3>, VertexSkinDataBuffer)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>,  VertexWriteTable)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, DecodedVertexBuffer)
        SHADER_PARAMETER(uint32, gVertexCount)
        SHADER_PARAMETER(uint32, gCompactVertexCount)
        SHADER_PARAMETER(FHoloMeshVec3, gAABBMin)
        SHADER_PARAMETER(FHoloMeshVec3, gAABBMax)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);     
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// Anims
class FAVVDecodeFrameAnim_None_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodeFrameAnim_None_CS)
    SHADER_USE_PARAMETER_STRUCT(FAVVDecodeFrameAnim_None_CS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, DecodedVertexBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<float3>, VertexPositionBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<float3>, VertexPrevPositionBuffer)
        SHADER_PARAMETER(uint32, gVertexCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// AVV_FRAME_ANIM_SKIN_MAT4X4_32
class FAVVDecodeFrameAnim_SSDR_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodeFrameAnim_SSDR_CS)
    SHADER_USE_PARAMETER_STRUCT(FAVVDecodeFrameAnim_SSDR_CS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, DecodedVertexBuffer)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, FrameSSDRDataBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<float3>, VertexPositionBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<float3>, VertexPrevPositionBuffer)
        SHADER_PARAMETER(uint32, gVertexCount)
        SHADER_PARAMETER(uint32, gBoneCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// AVV_FRAME_ANIM_DELTA_POS_32
class FAVVDecodeFrameAnim_Delta_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodeFrameAnim_Delta_CS)
    SHADER_USE_PARAMETER_STRUCT(FAVVDecodeFrameAnim_Delta_CS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, DecodedVertexBuffer)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, FrameDeltaDataBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<float3>, VertexPositionBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<float3>, VertexPrevPositionBuffer)
        SHADER_PARAMETER(uint32, gVertexCount)
        SHADER_PARAMETER(FHoloMeshVec3, gAABBMin)
        SHADER_PARAMETER(FHoloMeshVec3, gAABBMax)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// AVV_SEGMENT_TRIS_16
class FAVVDecodeSegmentTris_16_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodeSegmentTris_16_CS)
    SHADER_USE_PARAMETER_STRUCT(FAVVDecodeSegmentTris_16_CS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, IndexDataBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<uint32>, IndexBuffer)
        SHADER_PARAMETER(uint32, gCompactIndexCount)
        SHADER_PARAMETER(uint32, gMaxIndexCount)
        SHADER_PARAMETER(uint32, gIndexCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// AVV_SEGMENT_TRIS_32
class FAVVDecodeSegmentTris_32_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodeSegmentTris_32_CS)
        SHADER_USE_PARAMETER_STRUCT(FAVVDecodeSegmentTris_32_CS, FGlobalShader)
        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, IndexDataBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<uint32>, IndexBuffer)
        SHADER_PARAMETER(uint32, gMaxIndexCount)
        SHADER_PARAMETER(uint32, gIndexCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

class FAVVClearUnusedTrisCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVClearUnusedTrisCS)
    SHADER_USE_PARAMETER_STRUCT(FAVVClearUnusedTrisCS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_UAV(RWBuffer<uint32>, IndexBuffer)
        SHADER_PARAMETER(uint32, gCompactIndexCount)
        SHADER_PARAMETER(uint32, gMaxIndexCount)
        SHADER_PARAMETER(uint32, gIndexCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// AVV_SEGMENT_UVS_16
class FAVVDecodeUVS_16_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodeUVS_16_CS)
    SHADER_USE_PARAMETER_STRUCT(FAVVDecodeUVS_16_CS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, gUVCount)
        SHADER_PARAMETER(uint32, gTexCoordStride)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, UVDataBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<uint32>, VertexTexCoordBuffer)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// AVV_SEGMENT_UVS_12_NORMALS_888
class FAVVDecodeUVS_12_Normals_888_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodeUVS_12_Normals_888_CS)
    SHADER_USE_PARAMETER_STRUCT(FAVVDecodeUVS_12_Normals_888_CS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, gVertexCount)
        SHADER_PARAMETER(uint32, gUVCount)
        SHADER_PARAMETER(uint32, gTexCoordStride)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, UVDataBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<uint32>, VertexTexCoordBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<uint32>, VertexTangentBuffer)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// AVV_SEGMENT_MOTION_VECTORS
class FAVVDecodeSegmentMotionVectors_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodeSegmentMotionVectors_CS)
        SHADER_USE_PARAMETER_STRUCT(FAVVDecodeSegmentMotionVectors_CS, FGlobalShader)

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, gVertexCount)
        SHADER_PARAMETER(FHoloMeshVec3, gMotionVectorsMin)
        SHADER_PARAMETER(FHoloMeshVec3, gMotionVectorsMax)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, MotionVectorsDataBuffer)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, DecodedVertexBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<float>, VertexPositionBuffer)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// AVV_FRAME_COLORS_RGB_565
struct FAVVDecodeFrameColor_RGB_565_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodeFrameColor_RGB_565_CS)
    SHADER_USE_PARAMETER_STRUCT(FAVVDecodeFrameColor_RGB_565_CS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ColorDataBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<UNORM float4>, VertexColorBuffer)
        SHADER_PARAMETER(uint32, gVertexCount)
        SHADER_PARAMETER(uint32, gColorCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// AVV_FRAME_COLORS_RGB_565_NORMALS_OCT_16
struct FAVVDecodeFrameColor_RGB_565_Normals_Oct16_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodeFrameColor_RGB_565_Normals_Oct16_CS)
    SHADER_USE_PARAMETER_STRUCT(FAVVDecodeFrameColor_RGB_565_Normals_Oct16_CS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ColorDataBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<UNORM float4>, VertexColorBuffer)
        SHADER_PARAMETER_UAV(RWBuffer<uint32>, VertexTangentBuffer)
        SHADER_PARAMETER(uint32, gVertexCount)
        SHADER_PARAMETER(uint32, gColorCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

// AVV_SEGMENT_TEXTURE_BLOCKS_32 + AVV_FRAME_TEXTURE_LUMA_BC4
struct FAVVDecodeTextureBlock_BC4_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVDecodeTextureBlock_BC4_CS)
    SHADER_USE_PARAMETER_STRUCT(FAVVDecodeTextureBlock_BC4_CS, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, TextureBlockDataBuffer)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, LumaBlockDataBuffer)
        SHADER_PARAMETER_UAV(RWTexture2D<float>, LumaTextureOut)
        SHADER_PARAMETER_UAV(RWTexture2D<float>, MaskTextureOut)
        SHADER_PARAMETER(uint32, gBlockCount)
        SHADER_PARAMETER(uint32, gBlockOffset)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};

struct FAVVCopyTextureBlock_BC4_CS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FAVVCopyTextureBlock_BC4_CS)
        SHADER_USE_PARAMETER_STRUCT(FAVVCopyTextureBlock_BC4_CS, FGlobalShader)

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, TextureBlockDataBuffer)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, LumaBlockDataBuffer)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, BC4StagingTextureOut)
        SHADER_PARAMETER_UAV(RWTexture2D<float>, MaskTextureOut)
        SHADER_PARAMETER(uint32, gBlockCount)
        SHADER_PARAMETER(uint32, gBlockOffset)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return SupportsComputeShaders(Parameters.Platform);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
        OutEnvironment.SetDefine(TEXT("HLSL_2021"), HLSL_2021);
    }
};