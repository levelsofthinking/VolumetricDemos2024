// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "AVV/AVVSkeletalMeshFactory.h"
#include "AVV/AVVFile.h"
#include "AVV/AVVReader.h"
#include "HoloSuitePlayerEditor.h"

#include "MeshUtilities.h"
#include "Misc/Paths.h"

#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "ImportUtils/SkelImport.h"

#define LOCTEXT_NAMESPACE "AVVSkeletalMeshFactory"

UAVVSkeletalMeshFactory::UAVVSkeletalMeshFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SupportedClass = USkeletalMesh::StaticClass();
    bCreateNew = true;
    bEditAfterNew = false;
}

UObject* UAVVSkeletalMeshFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    // Input validation

    if (SourceAVV == nullptr)
    {
        UE_LOG(LogHoloSuitePlayerEditor, Error, TEXT("A source AVV File must be provided to generate a SkeletalMesh from."));
        return nullptr;
    }

    FStreamableAVVData& streamableData = (FStreamableAVVData&)SourceAVV->GetStreamableData();
    if (streamableData.Version != AVV_VERSION)
    {
        UE_LOG(LogHoloSuitePlayerEditor, Error, TEXT("Unsupported AVV Version, unable to generate a SkeletalMesh from the source AVV File provided."));
        return nullptr;
    }

    // Retrieve skeleton data

    USkeletalMesh* NewSkeletalMesh = NewObject<USkeletalMesh>(InParent, Class, Name, Flags);

    FSkeletalMeshImportData SkelImportData;
    SkelImportData.bHasVertexColors = true;
    SkelImportData.bHasNormals = false;
    SkelImportData.bHasTangents = false;
    SkelImportData.bDiffPose = false;
    SkelImportData.bUseT0AsRefPose = false;

    // Config Skeleton Import Data with Meta Skeleton
    AVVSkeleton avvMetaSkeleton;
    if (FAVVReader::DecodeMetaSkeleton(SourceAVV, &avvMetaSkeleton))
    {

        // Create root bone
        SkeletalMeshImportData::FBone& RootBone = SkelImportData.RefBonesBinary.Add_GetRef(SkeletalMeshImportData::FBone());
        RootBone.Name = TEXT("Root");
        RootBone.ParentIndex = INDEX_NONE;

        // Create bone hierarchy
        for (uint32 b = 0; b < avvMetaSkeleton.boneCount; ++b)
        {
            SkeletalMeshImportData::FBone& Bone = SkelImportData.RefBonesBinary.Add_GetRef(SkeletalMeshImportData::FBone());
            Bone.Name = FString(UTF8_TO_TCHAR(avvMetaSkeleton.boneInfo[b].name));
            Bone.ParentIndex = avvMetaSkeleton.boneInfo[b].parentIndex + 1;

            // Increment the number of children each time a bone is referenced as a parent bone; the root has a parent index of -1
            if (Bone.ParentIndex >= 0)
            {
                // The joints are ordered from parent-to-child so the parent will already have been added to the array
                SkeletalMeshImportData::FBone& ParentBone = SkelImportData.RefBonesBinary[Bone.ParentIndex];
                ++ParentBone.NumChildren;
            }
        }

        // Set positions and rotations of each joint
        for (uint32 b = 0; b < avvMetaSkeleton.boneCount; ++b)
        {
            int BoneIndex = b + 1;

            SkeletalMeshImportData::FJointPos& JointMatrix = SkelImportData.RefBonesBinary[BoneIndex].BonePos;
            FTransform boneTransform;
            // Note: y/z swap is performed here.
            boneTransform.SetTranslation(FVector(avvMetaSkeleton.positions[b].X, 
                avvMetaSkeleton.positions[b].Z, 
                avvMetaSkeleton.positions[b].Y));
            boneTransform.SetRotation(FQuat(avvMetaSkeleton.rotations[b].X, 
                avvMetaSkeleton.rotations[b].Z, 
                avvMetaSkeleton.rotations[b].Y, 
                -avvMetaSkeleton.rotations[b].W));
            boneTransform.SetScale3D(FVector(1.0f, 1.0f, 1.0f));

#if ENGINE_MAJOR_VERSION == 5
            JointMatrix.Transform = FTransform3f(boneTransform);
#else
            JointMatrix.Transform = FTransform(boneTransform);
#endif
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

        // Generate a (near-)zero-area face at the position of the root bone to trick the Editor
        // Removing this results in continuous warning "SkeletalMesh has no valid LODs for rendering".
        SkelImportData.Points.AddUninitialized(3);
        SkelImportData.PointToRawMap.AddUninitialized(3); 
        for (int p = 0; p < 3; ++p)
        {
            // Note: the use of FLT_EPSILON isn't suitable because it is still truncated into a single point.
#if ENGINE_MAJOR_VERSION == 5
            SkelImportData.Points[p] = FVector3f(-0.001*(p+1), 0, 0.001*(p+1));
#else
            SkelImportData.Points[p] = FVector(-0.001*(p+1), 0, 0.001*(p+1));
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

        // Materials & LODs
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
        //LODModel.NumVertices = SkelImportData.Points.Num();
        //LODModel.IndexBuffer.Add(0);

        SkeletalMeshImportUtils::ProcessImportMeshMaterials(NewSkeletalMesh->GetMaterials(), SkelImportData);

        int32 SkeletalDepth = 0;
        if (!SkeletalMeshImportUtils::ProcessImportMeshSkeleton(NewSkeletalMesh->GetSkeleton(), NewSkeletalMesh->GetRefSkeleton(), SkeletalDepth, SkelImportData))
        {
            UE_LOG(LogHoloSuitePlayerEditor, Error, TEXT("Unable to generate a SkeletalMesh from source AVV File, internal error."));
            return nullptr;
        }

        SkeletalMeshImportUtils::ProcessImportMeshInfluences(SkelImportData, FString(TEXT("SkelImportData")));

        NewSkeletalMesh->SaveLODImportedData(0, SkelImportData);

        NewSkeletalMesh->ResetLODInfo();
        FSkeletalMeshLODInfo& NewLODInfo = NewSkeletalMesh->AddLODInfo();
        NewLODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
        NewLODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
        NewLODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
        NewLODInfo.LODHysteresis = 0.02f;

        //FBox BoundingBox(SkelImportData.Points[0], SkelImportData.Points[2]);
        FBox BoundingBox(FVector(0.0f,0.0f,0.0f), FVector(1.0f, 1.0f, 1.0f));
        NewSkeletalMesh->SetImportedBounds(FBoxSphereBounds(BoundingBox));
#if ENGINE_MAJOR_VERSION == 5
        NewSkeletalMesh->SetHasVertexColors(SkelImportData.bHasVertexColors);
        TArray<FVector3f> LODPoints;
#elif (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
        NewSkeletalMesh->SetHasVertexColors(SkelImportData.bHasVertexColors);
        TArray<FVector> LODPoints;
#endif
        TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
        TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
        TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
        TArray<int32> LODPointToRawMap;
        SkelImportData.CopyLODImportData(LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap);

        // Build SkeletalMesh
        IMeshUtilities::MeshBuildOptions BuildOptions;
        BuildOptions.bComputeNormals = false;
        BuildOptions.bComputeTangents = false;
        IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
        TArray<FText> WarningMessages;
        TArray<FName> WarningNames;
        bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(ImportedResource->LODModels[0], FString(TEXT("NewSkeletalMesh")), NewSkeletalMesh->GetRefSkeleton(), LODInfluences, LODWedges, LODFaces, LODPoints, LODPointToRawMap, BuildOptions, &WarningMessages, &WarningNames);
        if (!bBuildSuccess)
        {
#if ENGINE_MAJOR_VERSION == 5
            NewSkeletalMesh->MarkAsGarbage();
#else
            NewSkeletalMesh->MarkPendingKill();
#endif
            UE_LOG(LogHoloSuitePlayerEditor, Error, TEXT("Unable to generate a SkeletalMesh from source AVV File, internal error."));
            return nullptr;
        }

        NewSkeletalMesh->CalculateInvRefMatrices();
        NewSkeletalMesh->Build();
        NewSkeletalMesh->MarkPackageDirty();
        NewSkeletalMesh->PostEditChange();

        // Create Skeleton asset
        USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, Flags | EObjectFlags::RF_Public);
        Skeleton->MergeAllBonesToBoneTree(NewSkeletalMesh);
        NewSkeletalMesh->SetSkeleton(Skeleton);

        return NewSkeletalMesh;
    }
    UE_LOG(LogHoloSuitePlayerEditor, Error, TEXT("Source AVV File does not contain skeleton data to generate a SkeletalMesh from."));
    return nullptr;
}

#undef LOCTEXT_NAMESPACE
