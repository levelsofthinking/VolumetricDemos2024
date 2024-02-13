// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloMeshSkeleton.h"
#include "HoloMeshComponent.h"

FHoloMeshSkeleton::FHoloMeshSkeleton(USkeletalMeshComponent* SkeletalMesh)
{
	SkeletalMeshComponent = SkeletalMesh;
}

FHoloMeshSkeleton::~FHoloMeshSkeleton()
{
    SkeletalMeshComponent = nullptr;
}

TArray<FTransform> GetSkeletonTransforms(FHoloSkeleton* sourceSkeleton)
{
    TArray<FTransform> results;

    for (uint32_t i = 0; i < sourceSkeleton->boneCount; ++i)
    {
        FTransform boneTransform;
        // Note: y/z swap is performed here.
        boneTransform.SetTranslation(FVector(sourceSkeleton->positions[i].X,
            sourceSkeleton->positions[i].Z,
            sourceSkeleton->positions[i].Y));
        boneTransform.SetRotation(FQuat(sourceSkeleton->rotations[i].X,
            sourceSkeleton->rotations[i].Z,
            sourceSkeleton->rotations[i].Y,
            -sourceSkeleton->rotations[i].W));
        boneTransform.SetScale3D(FVector(1.0f, 1.0f, 1.0f));

        results.Add(boneTransform);
    }

    return results;
}

void FHoloMeshSkeleton::UpdateSkeleton(FHoloSkeleton sourceSkeleton)
{
    if (sourceSkeleton.boneCount <= 0 || SkeletalMeshComponent == nullptr)
    {
        return;
    }

#if (ENGINE_MAJOR_VERSION >= 5) && (ENGINE_MINOR_VERSION >= 1)
    USkeletalMesh* targetSkeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
#else
    USkeletalMesh* targetSkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
#endif

    // Find matching bones.
    boneMap.Empty();
    for (uint32_t i = 0; i < sourceSkeleton.boneCount; ++i)
    {
        bool foundBone = false;
        FString BoneName = sourceSkeleton.boneNames[i];
        uint32_t RefSkelBoneCount = targetSkeletalMesh->GetRefSkeleton().GetNum();
        for (uint32_t j = 0; j < RefSkelBoneCount; ++j)
        {
            FString UE4BoneName = SkeletalMeshComponent->GetBoneName(j).ToString();
            if (UE4BoneName.Compare(BoneName) == 0)
            {
                foundBone = true;
                boneMap.Add(j);
                break;
            }
        }

        if (!foundBone)
        {
            boneMap.Add(-1);
        }
    }

    // Update bone poses for each frame.
    TArray<FTransform> omsRefPoses = GetSkeletonTransforms(&sourceSkeleton);

#if ENGINE_MAJOR_VERSION == 5 ||(ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
    FReferenceSkeleton& refSkel = targetSkeletalMesh->GetRefSkeleton();
    TArray<FTransform> boneRefPoses = refSkel.GetRawRefBonePose();
#else 
    TArray<FTransform> boneRefPoses = targetSkeletalMesh->RefSkeleton.GetRawRefBonePose();
#endif
    for (uint32_t i = 0; i < sourceSkeleton.boneCount; ++i)
    {
        int ue4BoneIndex = boneMap[i];
        if (ue4BoneIndex == -1)
        {
            continue;
        }

        boneRefPoses[ue4BoneIndex] = omsRefPoses[i];
    }

    // Update reference bone poses in the skeleton with new values.
    {
#if ENGINE_MAJOR_VERSION == 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
        FReferenceSkeletonModifier refPoseUpdate(refSkel, targetSkeletalMesh->GetSkeleton());
#else
        FReferenceSkeletonModifier refPoseUpdate(targetSkeletalMesh->RefSkeleton, targetSkeletalMesh->Skeleton);
#endif 
        for (uint32_t i = 0; i < sourceSkeleton.boneCount; ++i)
        {
            int ue4BoneIndex = boneMap[i];
            if (ue4BoneIndex == -1)
            {
                continue;
            }

            refPoseUpdate.UpdateRefPoseTransform(ue4BoneIndex, boneRefPoses[ue4BoneIndex]);
        }
    }

    // Force everything to update to those new values.
#if ENGINE_MAJOR_VERSION == 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
    targetSkeletalMesh->GetRefBasesInvMatrix().Empty();
#else
    targetSkeletalMesh->RefBasesInvMatrix.Empty();
#endif

    targetSkeletalMesh->CalculateInvRefMatrices();

    SkeletalMeshComponent->bEnableUpdateRateOptimizations = 1;
    SkeletalMeshComponent->bRequiredBonesUpToDate = 0;
    SkeletalMeshComponent->TickAnimation(0.0f, true);
    SkeletalMeshComponent->RefreshBoneTransforms();

#if (ENGINE_MAJOR_VERSION >= 5) && (ENGINE_MINOR_VERSION >= 1)
    SkeletalMeshComponent->RefreshFollowerComponents();
#else
    SkeletalMeshComponent->RefreshSlaveComponents();
#endif

    SkeletalMeshComponent->UpdateComponentToWorld();
    SkeletalMeshComponent->FinalizeBoneTransform();
    SkeletalMeshComponent->MarkRenderTransformDirty();
    SkeletalMeshComponent->MarkRenderDynamicDataDirty();

}

void FHoloMeshSkeleton::UpdateRetargetMesh(FHoloMesh* writeMesh)
{

#if (ENGINE_MAJOR_VERSION >= 5) && (ENGINE_MINOR_VERSION >= 1)
    USkeletalMesh* skeletalMesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
#else
    USkeletalMesh* skeletalMesh = SkeletalMeshComponent->SkeletalMesh;
#endif

    if (!writeMesh->RetargetBoneTexture.IsValid())
    {
        writeMesh->RetargetBoneTexture.Create(4 * boneMap.Num());
    }

#if ENGINE_MAJOR_VERSION == 5
    TArray<FMatrix44f> RefBasesInvMatrix = skeletalMesh->GetRefBasesInvMatrix();
#elif ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27
    TArray<FMatrix> RefBasesInvMatrix = skeletalMesh->GetRefBasesInvMatrix();
#else
    TArray<FMatrix> RefBasesInvMatrix;
    RefBasesInvMatrix = skeletalMesh->RefBasesInvMatrix;
#endif

    if (RefBasesInvMatrix.Num() < boneMap.Num())
    {
        skeletalMesh->CalculateInvRefMatrices();
    }

    const TArray<FTransform>& CompSpaceTransforms = SkeletalMeshComponent->GetComponentSpaceTransforms();

    if (CompSpaceTransforms.Num() < boneMap.Num())
    {
        SkeletalMeshComponent->AllocateTransformData();
    }

    float* TextureData = writeMesh->RetargetBoneTexture.GetData();

    for (int i = 0; i < boneMap.Num(); ++i)
    {
        int ue4BoneIndex = boneMap[i];
        if (ue4BoneIndex == -1)
        {
            writeMesh->RetargetBoneTexture.SetToIdentity(i);
            continue;
        }
#if ENGINE_MAJOR_VERSION == 5
        FMatrix44f boneMatrix = RefBasesInvMatrix[ue4BoneIndex] * FMatrix44f(CompSpaceTransforms[ue4BoneIndex].ToMatrixWithScale());
#else
        FMatrix boneMatrix = FMatrix(RefBasesInvMatrix[ue4BoneIndex]) * CompSpaceTransforms[ue4BoneIndex].ToMatrixWithScale();
#endif
        memcpy(&TextureData[i * 16], &boneMatrix.M[0][0], sizeof(float) * 16);
    }

    writeMesh->RetargetBoneTexture.Update();
}