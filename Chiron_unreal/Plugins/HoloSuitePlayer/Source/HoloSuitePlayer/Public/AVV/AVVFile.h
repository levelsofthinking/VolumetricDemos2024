// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/BulkData.h"
#include "UObject/NoExportTypes.h"
#include "UObject/DevObjectVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"

#include <mutex>
#include <thread>

#include "HoloSuiteFile.h"
#include "HoloSuitePlayerModule.h"

#include "AVVFile.generated.h"

// Forward declare.
struct AVVSegmentTableEntry;

struct HOLOSUITEPLAYER_API FAVVIORequest
{
    enum class EType
    {
        None,
        Segment,
        Frame,
        Texture
    } Type = EType::None;

    enum class EStatus
    {
        None,
        Error,
        Waiting,
        Completed,
        Processed
    } Status = EStatus::None;

    IBulkDataIORequest* Request = nullptr;
    double StartTime = 0.0;
    double EndTime = 0.0;
    size_t SizeInBytes = 0;

    bool PollCompletion()
    {
        if (Request == nullptr)
        {
            return (Status == FAVVIORequest::EStatus::Completed);
        }

        if (Status == FAVVIORequest::EStatus::Waiting)
        {
            if (Request->PollCompletion())
            {
                Status = FAVVIORequest::EStatus::Completed;
                return true;
            }
        }

        return (Status == FAVVIORequest::EStatus::Completed);
    }
};
typedef TSharedPtr<FAVVIORequest, ESPMode::ThreadSafe> FAVVIORequestRef;

/**
 * 
 */
class HOLOSUITEPLAYER_API FAVVStreamableContainer
{
protected:
    FCriticalSection* CriticalSection;

public:

    FAVVStreamableContainer() 
    {
        CriticalSection = new FCriticalSection();
    }

    // Container Data
    FByteBulkData BulkData; 

    SIZE_T GetMemorySize() const
    {
        static const SIZE_T ClassSize = sizeof(FAVVStreamableContainer);
        SIZE_T CurrentSize = ClassSize;
        return CurrentSize;
    }

    // Serialize data into archive.
    void Serialize(FArchive& Ar, UAVVFile* Owner, int32 ContainerIndex);

    // Read data into provided buffer. Return false if buffer too small or other failure.
    // This function will block until the read is finished.
    bool Read(uint8_t* outputBuffer, size_t outputBufferSize);

    // Async read of data into provided buffer.
    FAVVIORequestRef ReadAsync(uint8_t* outputBuffer, size_t outputBufferSize);
};

/**
 * 
 */
class HOLOSUITEPLAYER_API FStreamableAVVData : public IStreamableHoloSuiteData
{
public:
    uint32_t Version;
    FByteBulkData MetaData;
    uint32_t ContainsSkeleton;
    
    SIZE_T MaxSegmentSizeBytes;
    SIZE_T MaxFrameSizeBytes;
    SIZE_T MaxFrameTextureSizeBytes;

    TArray<FAVVStreamableContainer> SegmentContainers;
    TArray<FAVVStreamableContainer> FrameContainers;
    TArray<FAVVStreamableContainer> FrameTextureContainers;

    void Serialize(FArchive& Ar, class UAVVFile* Owner);

    void ImportSegment(uint8_t* Buffer, SIZE_T SizeInBytes);

    // Upgrades a file that was previous serialized in a per-segment manner.
    void UpgradeFromPerSegment(FArchive& Ar, class UAVVFile* Owner);

    void Reset() 
    {
        SegmentContainers.Reset();
        FrameContainers.Reset();
        FrameTextureContainers.Reset();
    }

    SIZE_T GetMemorySize() const
    {
        SIZE_T ContainerSize = 0;
        for (const FAVVStreamableContainer& Container : SegmentContainers)
        {
            ContainerSize += Container.GetMemorySize();
        }
        for (const FAVVStreamableContainer& Container : FrameContainers)
        {
            ContainerSize += Container.GetMemorySize();
        }
        for (const FAVVStreamableContainer& Container : FrameTextureContainers)
        {
            ContainerSize += Container.GetMemorySize();
        }
        return sizeof(FAVVStreamableContainer) + ContainerSize;
    }

    // Read metadata content. Caller is responsible for disposal.
    // This function will block until the read is finished.
    uint8_t* ReadMetaData();
};

// Custom serialization version for UAVVObject.
struct HOLOSUITEPLAYER_API FAVVFileVersion
{
    enum Type
    {
        // Convert header and sequence data into bulkdata chunks for streaming.
        ConvertBulkData,
        // Keeps a record to the original path to the source file.
        KeepFilePath,
        // Data stored on a per frame basis instead of per segment
        PerFrameDataStorage,
        // Add new versions above this line.
        VersionPlusOne,
        LatestVersion = VersionPlusOne - 1
    };
    // The GUID for this custom version number
    const static FGuid GUID;

private:
    FAVVFileVersion() {}
};

/**
 *
 */
UCLASS(BlueprintType)
class HOLOSUITEPLAYER_API UAVVFile : public UHoloSuiteFile
{
    GENERATED_BODY()

public:
    // Default constructor.
    UAVVFile();

    // Import AVV file and build bulk data. Returns success.
    bool ImportFile(const FString& TheFileName);

    // Import AVV file and build bulk data from the given archive. Return success.
    void ImportFile(FArchive& Reader);

    // Serialize/Deserialize Unreal asset.
    virtual void Serialize(FArchive& Ar) override;

    IStreamableHoloSuiteData& GetStreamableData();

    FString GetPath() override;

private:

    // Internal bulk data.
    FStreamableAVVData StreamableAVVData;

};