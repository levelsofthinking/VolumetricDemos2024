// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/Engine.h"
#include "RenderGraphUtils.h"
#include "Stats/Stats.h"
#include "SceneInterface.h"

#include "AVVDecoder.h"
#include "HoloMeshUtilities.h"
#include "HoloSuitePlayerModule.h"
#include "HoloSuitePlayerSettings.h"

#include "AVVDecoderCompute.generated.h"

UCLASS()
class HOLOSUITEPLAYER_API UAVVDecoderCompute : public UAVVDecoder
{
    GENERATED_BODY()
    
public:	
    UAVVDecoderCompute(const FObjectInitializer& ObjectInitializer);

    virtual void Close() override;
    virtual void Update(float DeltaTime) override;

    // In compute decoding we always use the same single holomesh.
    virtual FHoloMesh* GetHoloMesh(bool write = false) override { return &HoloMesh[0]; }
    virtual FHoloMesh* GetHoloMesh(int index) override { return UAVVDecoder::GetHoloMesh(index); }

protected:

    virtual void InitDecoder(UMaterialInterface* NewMeshMaterial) override;
    virtual void Update_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest request) override;
    virtual void EndFrame_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest request) override;

    // Decoding Functions
    bool ComputeDecodeSegmentVertices(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment, FHoloMesh* mesh);
    bool ComputeDecodeSegmentUVNormals(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment, FHoloMesh* mesh);
    bool ComputeDecodeSegmentTriangles(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment, FHoloMesh* mesh);
    bool ComputeDecodeSegmentMotionVectors(FRDGBuilder& GraphBuilder, AVVEncodedSegment* segment, FHoloMesh* mesh);
    bool ComputeDecodeFrameColorNormals(FRDGBuilder& GraphBuilder, AVVEncodedFrame* frame, FHoloMesh* mesh);
};