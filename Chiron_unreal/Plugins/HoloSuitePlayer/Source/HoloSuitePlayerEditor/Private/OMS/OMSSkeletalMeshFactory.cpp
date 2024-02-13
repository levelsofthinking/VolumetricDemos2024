// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "OMS/OMSSkeletalMeshFactory.h"
#include "OMS/OMSFile.h"
#include "OMS/oms.h"
#include "HoloSuitePlayerEditor.h"

#include "MeshUtilities.h"
#include "Misc/Paths.h"

#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "ImportUtils/SkelImport.h"

#define LOCTEXT_NAMESPACE "OMSSkeletalMeshFactory"

UOMSSkeletalMeshFactory::UOMSSkeletalMeshFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SupportedClass = USkeletalMesh::StaticClass();
    bCreateNew = true;
    bEditAfterNew = false;
}

UObject* UOMSSkeletalMeshFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    // Input validation

    if (SourceOMS == nullptr)
    {
        UE_LOG(LogHoloSuitePlayerEditor, Error, TEXT("A source OMS File must be provided to generate a SkeletalMesh from."));
        return nullptr;
    }

    USkeletalMesh* NewSkeletalMesh = NewObject<USkeletalMesh>(InParent, Class, Name, Flags);

    oms_header_t header = {};
    FStreamableOMSData& OMSStreamableData = (FStreamableOMSData&)SourceOMS->GetStreamableData();
    OMSStreamableData.ReadHeaderSync(&header);

    // We only need the first sequence.
    oms_sequence_t* sequence = new oms_sequence_t();
    OMSStreamableData.Chunks[0].ReadSequenceSync(&header, sequence);

    FSkeletalMeshImportData SkelImportData;
    SkelImportData.bHasVertexColors = true;
    SkelImportData.bHasNormals = false;
    SkelImportData.bHasTangents = false;
    SkelImportData.bDiffPose = false;
    SkelImportData.bUseT0AsRefPose = false;

    FBox* BoundingBox;

    // Retarget Skeletal Mesh
    if (Retargeting)
    {
        SkelImportData.Points.AddUninitialized(sequence->vertex_count);
        SkelImportData.PointToRawMap.AddUninitialized(sequence->vertex_count);
        for (int i = 0; i < sequence->vertex_count; ++i)
        {
#if ENGINE_MAJOR_VERSION == 5
            SkelImportData.Points[i] = FVector3f(sequence->vertices[i].x * 100.0f,
                sequence->vertices[i].z * 100.0f,
                sequence->vertices[i].y * 100.0f);
#else
            SkelImportData.Points[i] = FVector(sequence->vertices[i].x * 100.0f,
                sequence->vertices[i].z * 100.0f,
                sequence->vertices[i].y * 100.0f);
#endif
            SkelImportData.PointToRawMap[i] = i;
        }
        TArray <FVector> ChangePoints;
        for (int i = 0; i < SkelImportData.Points.Num(); ++i)
        {
            ChangePoints.Add(FVector(SkelImportData.Points[i]));
        }
        BoundingBox = new FBox(ChangePoints.GetData(), ChangePoints.Num());

        SkelImportData.Faces.AddUninitialized(sequence->index_count / 3);
        for (int i = 0; i < (sequence->index_count / 3); ++i)
        {
            SkeletalMeshImportData::FTriangle& Triangle = SkelImportData.Faces[i];

            Triangle.SmoothingGroups = 255;
            Triangle.MatIndex = 0;
            Triangle.AuxMatIndex = 0;

            int vertexIndices[3];

            if (sequence->vertex_count > (UINT16_MAX + 1))
            {
                uint32_t* indices = (uint32_t*)sequence->indices;
                vertexIndices[0] = (indices[(i * 3) + 0]);
                vertexIndices[1] = (indices[(i * 3) + 1]);
                vertexIndices[2] = (indices[(i * 3) + 2]);
            }
            else {
                uint16_t* indices = (uint16_t*)sequence->indices;
                vertexIndices[0] = (indices[(i * 3) + 0]);
                vertexIndices[1] = (indices[(i * 3) + 1]);
                vertexIndices[2] = (indices[(i * 3) + 2]);
            }

            for (int j = 0; j < 3; ++j)
            {
                const uint32 WedgeIndex = SkelImportData.Wedges.AddUninitialized();
                SkeletalMeshImportData::FVertex& SkelMeshWedge = SkelImportData.Wedges[WedgeIndex];

                SkelMeshWedge.MatIndex = Triangle.MatIndex;
                SkelMeshWedge.VertexIndex = vertexIndices[j];
                SkelMeshWedge.Color = FColor::White;
                SkelMeshWedge.Reserved = 0;
#if ENGINE_MAJOR_VERSION == 5
                SkelMeshWedge.UVs[0] = FVector2f(sequence->uvs[vertexIndices[j]].x, sequence->uvs[vertexIndices[j]].y);

                Triangle.WedgeIndex[j] = WedgeIndex;

                Triangle.TangentX[j] = FVector3f::ZeroVector;
                Triangle.TangentY[j] = FVector3f::ZeroVector;
                Triangle.TangentZ[j] = FVector3f::ZeroVector;
#else
                SkelMeshWedge.UVs[0] = FVector2D(sequence->uvs[vertexIndices[j]].x, sequence->uvs[vertexIndices[j]].y);

                Triangle.WedgeIndex[j] = WedgeIndex;

                Triangle.TangentX[j] = FVector::ZeroVector;
                Triangle.TangentY[j] = FVector::ZeroVector;
                Triangle.TangentZ[j] = FVector::ZeroVector;
#endif
            }
        }

        // Bone weights/indices.
        SkelImportData.Influences.Reserve(sequence->vertex_count);
        for (int32 PointIndex = 0; PointIndex < sequence->vertex_count; ++PointIndex)
        {
            // The JointIndices/JointWeights contain the influences data for NumPoints * NumInfluencesPerComponent
            for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
            {
                SkelImportData.Influences.AddUninitialized();
                SkelImportData.Influences.Last().BoneIndex = (int)sequence->retarget_data.indices[PointIndex].data[InfluenceIndex] + 1;
                SkelImportData.Influences.Last().Weight = sequence->retarget_data.weights[PointIndex].data[InfluenceIndex];
                SkelImportData.Influences.Last().VertexIndex = PointIndex;
            }
        }
    }
    // Actor Attachment Skeletal Mesh
    else
    {
        // Generate a (near-)zero-area face at the position of the root bone to trick the Editor
        // Removing this results in continuous warning "SkeletalMesh has no valid LODs for rendering".
        SkelImportData.Points.AddUninitialized(3);
        SkelImportData.PointToRawMap.AddUninitialized(3);
        for (int p = 0; p < 3; ++p)
        {
            // Note: the use of FLT_EPSILON isn't suitable because it is still truncated into a single point.
#if ENGINE_MAJOR_VERSION == 5
            SkelImportData.Points[p] = FVector3f(-0.001 * (p + 1), 0, 0.001 * (p + 1));
#else
            SkelImportData.Points[p] = FVector(-0.001 * (p + 1), 0, 0.001 * (p + 1));
#endif
            SkelImportData.PointToRawMap[p] = p;
        }
        SkelImportData.Faces.AddUninitialized(1);
        for (int f = 0; f < 1; ++f)
        {
            SkeletalMeshImportData::FTriangle& Triangle = SkelImportData.Faces[f];

            Triangle.SmoothingGroups = 255;
            Triangle.MatIndex = 0;
            Triangle.AuxMatIndex = 0;

            int vertexIndices[3];
            vertexIndices[0] = 0;
            vertexIndices[1] = 1;
            vertexIndices[2] = 2;

            for (int j = 0; j < 3; ++j)
            {
                const uint32 WedgeIndex = SkelImportData.Wedges.AddUninitialized();
                SkeletalMeshImportData::FVertex& SkelMeshWedge = SkelImportData.Wedges[WedgeIndex];

                SkelMeshWedge.MatIndex = Triangle.MatIndex;
                SkelMeshWedge.VertexIndex = vertexIndices[j];
                SkelMeshWedge.Color = FColor::White;
                SkelMeshWedge.Reserved = 0;

                Triangle.WedgeIndex[j] = WedgeIndex;
#if ENGINE_MAJOR_VERSION == 5
                Triangle.TangentX[j] = FVector3f::ZeroVector;
                Triangle.TangentY[j] = FVector3f::ZeroVector;
                Triangle.TangentZ[j] = FVector3f::ZeroVector;
#else
                Triangle.TangentX[j] = FVector::ZeroVector;
                Triangle.TangentY[j] = FVector::ZeroVector;
                Triangle.TangentZ[j] = FVector::ZeroVector;
#endif
            }
        }
        SkelImportData.Influences.Reserve(3);
        for (int32 PointIndex = 0; PointIndex < 3; ++PointIndex)
        {
            for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
            {
                SkelImportData.Influences.AddUninitialized();
                SkelImportData.Influences.Last().BoneIndex = 1;
                SkelImportData.Influences.Last().Weight = 1;
                SkelImportData.Influences.Last().VertexIndex = PointIndex;
            }
        }
        BoundingBox = new FBox(FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 1.0f, 1.0f));
    }

    SkeletalMeshImportData::FBone& RootBone = SkelImportData.RefBonesBinary.Add_GetRef(SkeletalMeshImportData::FBone());
    RootBone.Name = TEXT("Root");
    RootBone.ParentIndex = INDEX_NONE;

    // Bone transforms
    // Store the retrieved data as bones into the SkeletalMeshImportData
    for (int32 i = 0; i < sequence->retarget_data.bone_count; ++i)
    {
        SkeletalMeshImportData::FBone& Bone = SkelImportData.RefBonesBinary.Add_GetRef(SkeletalMeshImportData::FBone());

        Bone.Name = FString(sequence->retarget_data.bone_names[i]);
        Bone.ParentIndex = sequence->retarget_data.bone_parents[i] + 1;

        // Increment the number of children each time a bone is referenced as a parent bone; the root has a parent index of -1
        if (Bone.ParentIndex >= 0)
        {
            // The joints are ordered from parent-to-child so the parent will already have been added to the array
            SkeletalMeshImportData::FBone& ParentBone = SkelImportData.RefBonesBinary[Bone.ParentIndex];
            ++ParentBone.NumChildren;
        }

        const int frame = 0;

        SkeletalMeshImportData::FJointPos& JointMatrix = Bone.BonePos;
        FTransform boneTransform;
        boneTransform.SetTranslation(FVector(sequence->retarget_data.bone_positions[frame][i].x,
            sequence->retarget_data.bone_positions[frame][i].z,
            sequence->retarget_data.bone_positions[frame][i].y));
        boneTransform.SetRotation(FQuat(sequence->retarget_data.bone_rotations[frame][i].x,
            sequence->retarget_data.bone_rotations[frame][i].z,
            sequence->retarget_data.bone_rotations[frame][i].y,
            -sequence->retarget_data.bone_rotations[frame][i].w));
        boneTransform.SetScale3D(FVector(1.0f, 1.0f, 1.0f));

#if ENGINE_MAJOR_VERSION == 5
        JointMatrix.Transform = FTransform3f(boneTransform);
#else
        JointMatrix.Transform = FTransform(boneTransform);
#endif

        // Not sure if Length and X/Y/Z Size need to be set, there are no equivalents in USD
        JointMatrix.Length = 1.0f;
        JointMatrix.XSize = 100.0f;
        JointMatrix.YSize = 100.0f;
        JointMatrix.ZSize = 100.0f;
    }

    // Scale Root Bone
#if ENGINE_MAJOR_VERSION == 5
    SkelImportData.RefBonesBinary[0].BonePos.Transform.SetScale3D(FVector3f(100.0f, 100.0f, 100.0f));
#else
    SkelImportData.RefBonesBinary[0].BonePos.Transform.SetScale3D(FVector(100.0f, 100.0f, 100.0f));
#endif
    // Materials
    SkeletalMeshImportData::FMaterial NewMaterial;
    NewMaterial.MaterialImportName = TEXT("DopeMaterial");
    SkelImportData.Materials.Add(NewMaterial);
    SkelImportData.MaxMaterialIndex = 0;
    SkelImportData.NumTexCoords = 1;

    NewSkeletalMesh->PreEditChange(nullptr);

    FSkeletalMeshModel* ImportedResource = NewSkeletalMesh->GetImportedModel();
    ImportedResource->LODModels.Empty();
    ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());

    FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[0];
    LODModel.NumTexCoords = SkelImportData.NumTexCoords;

#if ENGINE_MAJOR_VERSION == 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
    SkeletalMeshImportUtils::ProcessImportMeshMaterials(NewSkeletalMesh->GetMaterials(), SkelImportData);

    int32 SkeletalDepth = 0;
    if (!SkeletalMeshImportUtils::ProcessImportMeshSkeleton(NewSkeletalMesh->GetSkeleton(), NewSkeletalMesh->GetRefSkeleton(), SkeletalDepth, SkelImportData))
    {
        return nullptr;
    }

    SkeletalMeshImportUtils::ProcessImportMeshInfluences(SkelImportData, FString(TEXT("SkelImportData")));
#elif ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION == 26
    SkeletalMeshHelper::ProcessImportMeshMaterials(NewSkeletalMesh->Materials, SkelImportData);

    int32 SkeletalDepth = 0;
    if (!SkeletalMeshHelper::ProcessImportMeshSkeleton(NewSkeletalMesh->Skeleton, NewSkeletalMesh->RefSkeleton, SkeletalDepth, SkelImportData))
    {
        return nullptr;
    }

    SkeletalMeshHelper::ProcessImportMeshInfluences(SkelImportData, FString(TEXT("SkelImportData")));
#else
    ProcessImportMeshMaterials(NewSkeletalMesh->Materials, SkelImportData);

    int32 SkeletalDepth = 0;
    if (!ProcessImportMeshSkeleton(NewSkeletalMesh->Skeleton, NewSkeletalMesh->RefSkeleton, SkeletalDepth, SkelImportData))
    {
        return nullptr;
    }

    ProcessImportMeshInfluences(SkelImportData);
#endif

    NewSkeletalMesh->SaveLODImportedData(0, SkelImportData);

    NewSkeletalMesh->ResetLODInfo();
    FSkeletalMeshLODInfo& NewLODInfo = NewSkeletalMesh->AddLODInfo();
    NewLODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
    NewLODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
    NewLODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
    NewLODInfo.LODHysteresis = 0.02f;

    NewSkeletalMesh->SetImportedBounds(FBoxSphereBounds(*BoundingBox));
#if ENGINE_MAJOR_VERSION == 5
    NewSkeletalMesh->SetHasVertexColors(SkelImportData.bHasVertexColors);
    TArray<FVector3f> LODPoints;
#elif (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
    NewSkeletalMesh->SetHasVertexColors(SkelImportData.bHasVertexColors);
    TArray<FVector> LODPoints;
#else
    NewSkeletalMesh->bHasVertexColors = SkelImportData.bHasVertexColors;
    TArray<FVector> LODPoints;
#endif
    TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
    TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
    TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
    TArray<int32> LODPointToRawMap;
    SkelImportData.CopyLODImportData(LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap);

    IMeshUtilities::MeshBuildOptions BuildOptions;
    BuildOptions.bComputeNormals = false;
    BuildOptions.bComputeTangents = false;

    IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

    TArray<FText> WarningMessages;
    TArray<FName> WarningNames;

#if ENGINE_MAJOR_VERSION == 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
    bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(ImportedResource->LODModels[0], FString(TEXT("NewSkeletalMesh")), NewSkeletalMesh->GetRefSkeleton(), LODInfluences, LODWedges, LODFaces, LODPoints, LODPointToRawMap, BuildOptions, &WarningMessages, &WarningNames);
#else
    bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(ImportedResource->LODModels[0], FString(TEXT("NewSkeletalMesh")), NewSkeletalMesh->RefSkeleton, LODInfluences, LODWedges, LODFaces, LODPoints, LODPointToRawMap, BuildOptions, &WarningMessages, &WarningNames);
#endif

    if (!bBuildSuccess)
    {
#if ENGINE_MAJOR_VERSION == 5
        NewSkeletalMesh->MarkAsGarbage();
#else
        NewSkeletalMesh->MarkPendingKill();
#endif
        return nullptr;
    }

    NewSkeletalMesh->CalculateInvRefMatrices();
    NewSkeletalMesh->Build();
    NewSkeletalMesh->MarkPackageDirty();
    NewSkeletalMesh->PostEditChange();

    USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, Flags | EObjectFlags::RF_Public);
    Skeleton->MergeAllBonesToBoneTree(NewSkeletalMesh);
#if ENGINE_MAJOR_VERSION == 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
    NewSkeletalMesh->SetSkeleton(Skeleton);
#else
    NewSkeletalMesh->Skeleton = Skeleton;
#endif

    return NewSkeletalMesh;
}

#undef LOCTEXT_NAMESPACE
