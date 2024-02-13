// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloSuitePlayerFactory.h"
#include "OMS/OMSFile.h"
#include "OMS/OMSFileActions.h"
#include "AVV/AVVFile.h"
#include "AVV/AVVFileActions.h"
#include "HoloSuiteFile.h"
#include "HoloSuitePlayer.h"
#include "HoloSuitePlayerEditor.h"

#include "AssetRegistry/AssetData.h"
#include "Misc/Paths.h"
#include "HAL/FileManagerGeneric.h"
#include "MediaSource.h"

UHoloSuitePlayerFactory::UHoloSuitePlayerFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NewActorClass = AHoloSuitePlayer::StaticClass();
	DisplayName = NSLOCTEXT("HoloSuitePlayer", "HoloSuitePlayerFactoryDisplayName", "HoloSuitePlayer");
	bUseSurfaceOrientation = true;
	bShowInEditorQuickMenu = true;
}

FString FindFile(TArray <FString> FileTargets, FString ExtensionType, FString StartDirectory)
{
	FFileManagerGeneric FileMgr;
	TArray<FString> FileNames;
	FileMgr.FindFilesRecursive(FileNames, *StartDirectory, *ExtensionType, true, false);

	FString FileFound;
	if (FileNames.Num() > 0)
	{
		for (int i = 0; i < FileTargets.Num(); i++)
		{
			for (int j = 0; j < FileNames.Num(); j++)
			{
				if (FileNames[j].Contains(FileTargets[i]))
				{
					// UE_LOG(LogHoloSuitePlayerEditor, Warning, TEXT("File found: %s"), *FileNames[j]);
					FileFound = FileNames[j];
					break;
				}
			}
			if (!FileFound.IsEmpty())
			{
				break;
			}
		}
	}
	return FileFound;
}

FString GetAssetPathFromAbsoluteFilePath(FString AbsoluteFilePath)
{
	FString FilePath;
	AbsoluteFilePath.Split(TEXT("content"), NULL, &FilePath);

	TArray<FString> FilePathParsed;
	FilePath.ParseIntoArray(FilePathParsed, TEXT("/"), false);

	FString AssetFileName;
	FilePathParsed[FilePathParsed.Num() - 1].Split(TEXT("."), &AssetFileName, NULL);

	FString AssetPath = "'/Game/"; // needs to follow the format: '/Game/(...)/Filename.Filename'
	for (int i = 1; i < FilePathParsed.Num() - 1; i++)
	{
		AssetPath = AssetPath.Append(FilePathParsed[i]);
		AssetPath = AssetPath.Append("/");
	}
	AssetPath = AssetPath.Append(AssetFileName);
	AssetPath = AssetPath.Append(".");
	AssetPath = AssetPath.Append(AssetFileName);
	AssetPath = AssetPath.Append("'");
	//UE_LOG(LogHoloSuitePlayerEditor, Warning, TEXT("AssetPath: %s"), *AssetPath);
	return AssetPath;
}

void UHoloSuitePlayerFactory::PrefillActorSourceParameters(UObject* Asset, AActor* NewActor)
{
	if (UAVVFile* avvFile = Cast<UAVVFile>(Asset))
	{
		// Assign AVV
		AHoloSuitePlayer* TypedActor = CastChecked<AHoloSuitePlayer>(NewActor);
		TypedActor->SourceFile = Cast<UHoloSuiteFile>(Asset);

		// Assign Mesh Material
		TypedActor->SetDefaultMeshMaterial(LoadObject<UMaterial>(Asset, TEXT("Material'/HoloSuitePlayer/Arcturus/HoloSuite_AVVLit_Mat.HoloSuite_AVVLit_Mat'")));

		// Initialize AVVPlayerComponent
		TypedActor->InitializePlayerComponent(false, false, false, false);
	}
	else if (UOMSFile* OMSFile = Cast<UOMSFile>(Asset))
	{
		// Assign OMS
		AHoloSuitePlayer* TypedActor = CastChecked<AHoloSuitePlayer>(NewActor);
		TypedActor->SourceFile = Cast<UHoloSuiteFile>(Asset);

		// Attempt to locate and assign Mesh Material
		TArray <FString> FileTargets;
		FString FileFound;
		FileTargets.Add(*OMSFile->GetName().Append("_LitMaterial.uasset"));
		FileTargets.Add(*OMSFile->GetName().Append("_UnlitMaterial.uasset"));
		FileFound = FindFile(FileTargets, "*.uasset", FPaths::ProjectContentDir());
		if (!FileFound.IsEmpty())
		{
			UMaterial* omsMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), NULL, *GetAssetPathFromAbsoluteFilePath(FileFound), NULL, LOAD_NoWarn, NULL));
			TypedActor->SetDefaultMeshMaterial(omsMaterial);
		}
		else
		{
			TypedActor->SetDefaultMeshMaterial(LoadObject<UMaterial>(Asset, TEXT("Material'/HoloSuitePlayer/Arcturus/HoloSuite_OMSLit_Mat.HoloSuite_OMSLit_Mat'")));
		}

		// Attempt to locate and assign Texture Source
		FileTargets.Empty();
		FileFound.Empty();
		FileTargets.Add(*OMSFile->GetName().Append(".mp4"));
		FileFound = FindFile(FileTargets, "*.mp4", FPaths::ProjectContentDir().Append("Movies"));
		if (!FileFound.IsEmpty())
		{
			TypedActor->TextureSource = Cast<UMediaSource>(StaticLoadObject(UMediaSource::StaticClass(), NULL, *GetAssetPathFromAbsoluteFilePath(FileFound), NULL, LOAD_NoWarn, NULL));
		}

		// Assign Media Player Material
		TypedActor->SetDefaultMediaPlayerMaterial(LoadObject<UMaterial>(Asset, TEXT("Material'/HoloSuitePlayer/Arcturus/HoloSuite_OMSMediaPlayer_Mat.HoloSuite_OMSMediaPlayer_Mat'")));

		// Assign Retargeting Animation Material
		TypedActor->SetDefaultRetargetAnimMaterial(LoadObject<UMaterial>(Asset, TEXT("Material'/HoloSuitePlayer/Arcturus/HoloSuite_OMSRetargetAnim_Mat.HoloSuite_OMSRetargetAnim_Mat'")));

		// Initialize OMSPlayerComponent
		TypedActor->InitializePlayerComponent(false, false, false, false);
	}
}

void UHoloSuitePlayerFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);
	PrefillActorSourceParameters(Asset, NewActor);
}

void UHoloSuitePlayerFactory::PostCreateBlueprint(UObject* Asset, AActor* CDO)
{
	PrefillActorSourceParameters(Asset, CDO);
}

bool UHoloSuitePlayerFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
#if WITH_EDITOR
	// This needs to be added to allow HoloSuite Player actors to be created from the Place Actors tab in the Editor
	if (!AssetData.IsValid())
	{
		return true;
	}
#endif

	if (AssetData.GetClass()->IsChildOf(UHoloSuiteFile::StaticClass()))
	{
		return true;
	}
	OutErrorMsg = NSLOCTEXT("HoloSuitePlayer", "CanCreateActorFrom_NoHoloSuiteFile", "No OMS or AVV file was specified.");
	return false;
}