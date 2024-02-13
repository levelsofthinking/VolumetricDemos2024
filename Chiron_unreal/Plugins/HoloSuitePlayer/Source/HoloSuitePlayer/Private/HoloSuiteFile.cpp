// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloSuiteFile.h"

// Add default functionality here for any IHoloSuiteFile functions that are not pure virtual.

UStreamableHoloSuiteData::UStreamableHoloSuiteData(const class FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{

}

UHoloSuiteFile::UHoloSuiteFile()
{

}

void UHoloSuiteFile::Serialize(FArchive& Ar)
{
    Super::Serialize(Ar);
}

FString UHoloSuiteFile::GetPath()
{
    FString FilePath = FPackageName::LongPackageNameToFilename(GetPathName());
    FilePath = FPaths::ConvertRelativePathToFull(*FilePath);

    return FilePath;
}