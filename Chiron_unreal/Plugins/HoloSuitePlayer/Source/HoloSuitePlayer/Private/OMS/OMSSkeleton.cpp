// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "OMS/OMSSkeleton.h"

DECLARE_CYCLE_STAT(TEXT("OMSSkeleton.UpdateSkeleton"),      STAT_OMSSkeleton_UpdateSkeleton,        STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSSkeleton.UpdateRetargetMesh"),  STAT_OMSSkeleton_UpdateRetargetMesh,    STATGROUP_HoloSuitePlayer);

OMSSkeleton::OMSSkeleton(USkeletalMeshComponent* skeletalMeshComponent)
{
    HoloMeshSkeleton = new FHoloMeshSkeleton(skeletalMeshComponent);
}

OMSSkeleton::~OMSSkeleton()
{
    if (HoloMeshSkeleton)
    {
        delete HoloMeshSkeleton;
        HoloMeshSkeleton = nullptr;
    }
}

void OMSSkeleton::Reset()
{
    lastRetargetFrame = -1;
}

FHoloSkeleton OMSToHoloSkeleton(oms_retarget_data_t data, int frame)
{
    // While oms_retarget_data_t contains the positions and rotations of all bones for all frames of the sequence, FHoloSkeleton only stores the positions and rotations of all bones for a single frame.

    FHoloSkeleton holoSkeleton;

    holoSkeleton.skeletonIndex = 0;
    holoSkeleton.boneCount = data.bone_count;

    for (int i = 0; i < data.bone_count; ++i)
    {
        FString boneName(data.bone_names[i]);
        holoSkeleton.boneNames.push_back(boneName);
        holoSkeleton.boneParentIndexes.push_back(data.bone_parents[i]);

        holoSkeleton.positions.push_back(FHoloMeshVec3(data.bone_positions[frame][i].x,
            data.bone_positions[frame][i].y,
            data.bone_positions[frame][i].z));
        holoSkeleton.rotations.push_back(FHoloMeshVec4(data.bone_rotations[frame][i].x,
            data.bone_rotations[frame][i].y,
            data.bone_rotations[frame][i].z,
            data.bone_rotations[frame][i].w));
    }

    return holoSkeleton;
}

bool OMSSkeleton::UpdateSkeleton(oms_sequence_t* sequence, int currentFrame, int sequenceFrame)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSSkeleton_UpdateSkeleton);

    if (currentFrame == lastRetargetFrame)
    {
        return false;
    }

    if (HoloMeshSkeleton)
    {
        if (!sequence || sequence->retarget_data.bone_count <= 0)
        {
            return false;
        }

        HoloMeshSkeleton->UpdateSkeleton(OMSToHoloSkeleton(sequence->retarget_data, sequenceFrame));

        lastRetargetFrame = currentFrame;
    }
    return true;
}

void OMSSkeleton::UpdateRetargetMesh(FHoloMesh* writeMesh)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSSkeleton_UpdateRetargetMesh);

    if (HoloMeshSkeleton)
    {
        HoloMeshSkeleton->UpdateRetargetMesh(writeMesh);
    }
}
