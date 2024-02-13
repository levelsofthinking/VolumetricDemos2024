// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Serialization/BulkData.h"

#include "HoloSuiteFile.h"

#include "OMSFile.generated.h"

// Forward declare.
struct oms_sequence_t;
struct oms_header_t;

/**
 * 
 */
class HOLOSUITEPLAYER_API FOMSStreamableChunk
{
public:
    FOMSStreamableChunk()
    {
        dataBuffer = nullptr;
    }

    // Bulk data if stored in the package.
    FByteBulkData BulkData; // Sequence Data

    SIZE_T GetMemorySize() const
    {
        static const SIZE_T ClassSize = sizeof(FOMSStreamableChunk);
        SIZE_T CurrentSize = ClassSize;
        return CurrentSize;
    }

    /** Serialization. */
    void Serialize(FArchive& Ar, UOMSFile* Owner, int32 ChunkIndex);

    /** Reads from BulkData into sequence. Sequence must be freed with oms_free_sequence to release allocated memory. */
    void ReadSequenceSync(oms_header_t* header, oms_sequence_t* sequence);

private:
    /** Critical section to prevent concurrent access when locking the internal bulk data */
    mutable FCriticalSection CriticalSection;

    uint8* dataBuffer;
};

/**
 * 
 */
class HOLOSUITEPLAYER_API FStreamableOMSData : public IStreamableHoloSuiteData
{
public:
    TArray<FOMSStreamableChunk> Chunks;
    TArray<int> FrameToSequenceIndex;
    TArray<int> FrameToSequenceFrameOffset;
    int FrameCount;

    void Serialize(FArchive& Ar, class UOMSFile* Owner);

    void Reset()
    {
        Chunks.Reset();
    }

    SIZE_T GetMemorySize() const
    {
        SIZE_T ChunkSize = 0;
        for (const FOMSStreamableChunk& Chunk : Chunks)
        {
            ChunkSize += Chunk.GetMemorySize();
        }
        return sizeof(FOMSStreamableChunk) + ChunkSize;
    }

    FByteBulkData BulkData; // Header Data

    /** Reads from BulkData into header. Header must be freed with oms_free_header to release allocated memory. */
    void ReadHeaderSync(oms_header_t* header);

private:
    /** Critical section to prevent concurrent access when locking the internal bulk data */
    mutable FCriticalSection CriticalSection;
};

// Custom serialization version for UOMSFile.
struct HOLOSUITEPLAYER_API FOMSFileVersion
{
    enum Type
    {
        // Before any version changes were made.
        BeforeCustomVersionWasAdded,
        // Convert header and sequence data into bulkdata chunks for streaming.
        ConvertBulkData,
        // Fix for 4 bytes missing from tail of serialized chunks.
        FixMissingTail,
        // Keeps a record to the original path to the source file.
        KeepFilePath,
        // Add new versions above this line.
        VersionPlusOne,
        LatestVersion = VersionPlusOne - 1
    };
    // The GUID for this custom version number
    const static FGuid GUID;

private:
    FOMSFileVersion() {}
};

/**
 * 
 */
UCLASS(BlueprintType)
class HOLOSUITEPLAYER_API UOMSFile : public UHoloSuiteFile
{
    GENERATED_BODY()

public:
    /** Default constructor */
    UOMSFile();

    // Import OMS file and build bulk data. Returns success.
    bool ImportFile(const FString& TheFileName);

    // Import OMS file and build bulk data from the given data. Return success.
    bool ImportFile(const void* const InOMSData, const int32 InOMSDataSizeBytes);

    // Import OMS file and build bulk data from the given archive. Return success.
    void ImportFile(FArchive& Reader);

    // Serialize/Deserialize Unreal asset.
    virtual void Serialize(FArchive& Ar) override;

    IStreamableHoloSuiteData& GetStreamableData();

    FString GetPath() override;

private:

    // Internal bulk data.
    FStreamableOMSData StreamableOMSData;

    UPROPERTY()
    TArray<uint8> OMSData_DEPRECATED; // Replaced with StreamableOMSData.

    void ConvertFromOMSData();
};