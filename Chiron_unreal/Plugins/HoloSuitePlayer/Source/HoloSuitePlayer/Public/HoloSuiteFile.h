// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"

#include "HoloSuiteFile.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UStreamableHoloSuiteData : public UInterface
{
	GENERATED_BODY()

public:
	UStreamableHoloSuiteData(const class FObjectInitializer& ObjectInitializer);
};

/**
 *
 */
class HOLOSUITEPLAYER_API IStreamableHoloSuiteData
{
	GENERATED_BODY()

public:
	void Reset();
	SIZE_T GetMemorySize() const;

};

/**
 * 
 */
UCLASS(BlueprintType)
class HOLOSUITEPLAYER_API UHoloSuiteFile : public UObject
{
	GENERATED_BODY()

public:
	UHoloSuiteFile();

	// UObject interface
	virtual void Serialize(FArchive& Ar) override;

	//IStreamableHoloSuiteData& GetStreamableData();
	virtual FString GetPath();
	virtual void SetPath(FString NewSourcePath) { SourcePath = NewSourcePath; }

#if WITH_EDITORONLY_DATA
	/** Importing data and options used for this volumetric video */
	UPROPERTY(EditAnywhere, Instanced, Category = ImportSettings)
		class UAssetImportData* AssetImportData;
#endif

	// Original source file path.
	FString SourcePath;

};
