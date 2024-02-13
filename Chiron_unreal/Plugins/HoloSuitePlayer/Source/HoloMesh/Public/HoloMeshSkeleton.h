// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include <vector>

#include "HoloMeshUtilities.h"

struct FHoloMesh;

struct FHoloSkeleton
{
    uint32_t skeletonIndex = 0;
    uint32_t boneCount = 0;
    std::vector<FString> boneNames;
    std::vector<int32_t> boneParentIndexes;
    std::vector<FHoloMeshVec3> positions;
    std::vector<FHoloMeshVec4> rotations;
};

/**
 * 
 */
class HOLOMESH_API FHoloMeshSkeleton
{
public:

	FHoloMeshSkeleton(USkeletalMeshComponent* SkeletalMesh);
	~FHoloMeshSkeleton();

	void UpdateSkeleton(FHoloSkeleton sourceSkeleton);
    void UpdateRetargetMesh(FHoloMesh* writeMesh);

protected:

	USkeletalMeshComponent* SkeletalMeshComponent;
    TArray<int> boneMap;
};
