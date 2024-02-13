// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/Engine.h"
#include "PackedNormal.h"
#include "RenderGraphUtils.h"
#include "Stats/Stats.h" 
#include "SceneInterface.h"

#include "AVVDecoder.h"
#include "HoloMeshComponent.h"
#include "HoloMeshUtilities.h"
#include "HoloSuitePlayerModule.h"

#include "AVVDecoderCPU.generated.h"

UCLASS()
class HOLOSUITEPLAYER_API UAVVDecoderCPU : public UAVVDecoder
{
    GENERATED_BODY()
    
public:	
    UAVVDecoderCPU(const FObjectInitializer& ObjectInitializer);

    virtual void Close() override;
    virtual void Update(float DeltaTime) override;

protected:
    // Locally decoded data
    uint8_t* DecodedVertexData;
    std::atomic<bool> RequiresSwap{ false };

    virtual void InitDecoder(UMaterialInterface* NewMeshMaterial) override;
    virtual void Update_RenderThread(FRDGBuilder& GraphBuilder, FHoloMeshUpdateRequest request) override;
    virtual void RequestCulled_RenderThread(FHoloMeshUpdateRequest request) override;

    // Decoding Functions
    bool CPUDecodeMesh(FHoloMesh* meshOut, AVVEncodedSegment* segment);
    bool CPUDecodeFrameColors(FHoloMesh* meshOut, AVVEncodedFrame* frame);
    bool CPUDecodeFrameColorsNormals(FHoloMesh* meshOut, AVVEncodedFrame* frame);
};