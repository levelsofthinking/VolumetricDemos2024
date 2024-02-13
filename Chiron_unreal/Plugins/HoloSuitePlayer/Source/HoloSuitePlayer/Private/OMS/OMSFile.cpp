// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "OMS/OMSFile.h"
#include "HoloSuitePlayerModule.h"
#include "UObject/DevObjectVersion.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"

#include "OMS/oms.h"
#include "Serialization/MemoryReader.h"

// Unique OMS Object version id
const FGuid FOMSFileVersion::GUID(0xEF7A3040, 0x4F8208DF, 0xC2053CA9, 0x5BB981D7);

// Register OMS custom version with Core
FDevVersionRegistration GRegisterOMSFileVersion(FOMSFileVersion::GUID, FOMSFileVersion::LatestVersion, TEXT("Dev-OMS"));

void FOMSStreamableChunk::Serialize(FArchive& Ar, UOMSFile* Owner, int32 ChunkIndex)
{
    // Discard sequence data to reduce memory footprint.
    // The OMSPlayer will request sequence data on-demand when it is presented.
    BulkData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
    BulkData.Serialize(Ar, Owner, ChunkIndex, false);
}

void FOMSStreamableChunk::ReadSequenceSync(oms_header_t* header, oms_sequence_t* sequence)
{
    if (header == nullptr)
    {
        return;
    }

    if (sequence == nullptr)
    {
        return;
    }

    CriticalSection.Lock();

    int64 sizebytes = BulkData.GetBulkDataSize();
    if (sizebytes > 0)
    {
        // Loaded in Editor.
        if (BulkData.IsBulkDataLoaded())
        {
            uint8* data = (uint8*)BulkData.Lock(LOCK_READ_ONLY);
            oms_read_sequence(data, 0, sizebytes, header, sequence);
            BulkData.Unlock();
        }
        // Load on-demand in Runtime.
        else
        {
            FBulkDataIORequestCallBack AsyncFileCallBack =
                [this, sizebytes, header, sequence](bool bWasCancelled, IBulkDataIORequest* Req)
            {
                if (dataBuffer != nullptr)
                {
                    uint32_t sequenceSize;
                    memcpy(&sequenceSize, dataBuffer, sizeof(uint32_t));

                    // FixMissingTail
                    if ((sequenceSize + 4) > sizebytes)
                    {
                        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMS data is out of date and should be reimported."));
                    }

                    oms_read_sequence(dataBuffer, 0, sizebytes, header, sequence);
                    FMemory::Free(dataBuffer);
                }
            };

            const EAsyncIOPriorityAndFlags AsyncIOPriority = AIOP_CriticalPath;

            // Allocate a temporary buffer to store the data with extra 4 bytes 
            // on the end to support OMSFile's that come before on FixMissingTail.
            dataBuffer = (uint8_t*)FMemory::Malloc(sizebytes + 4);
            IBulkDataIORequest* IORequest = BulkData.CreateStreamingRequest(AsyncIOPriority, &AsyncFileCallBack, dataBuffer);

            if (IORequest) 
            {
                IORequest->WaitCompletion();
                // Free AsyncHandle Resources.
                delete IORequest;
                IORequest = nullptr;
            }
        }
    }
    CriticalSection.Unlock();
}

void FStreamableOMSData::Serialize(FArchive& Ar, UOMSFile* Owner)
{
    int32 NumChunks = Chunks.Num();
    Ar << NumChunks;
    Ar << FrameCount;
    Ar << FrameToSequenceIndex;
    Ar << FrameToSequenceFrameOffset;

    BulkData.Serialize(Ar, Owner, INDEX_NONE, false);

    if (Ar.IsLoading())
    {
        Chunks.Reset(NumChunks);
        Chunks.AddDefaulted(NumChunks);
    }
    for (int32 ChunkIndex = 0; ChunkIndex < NumChunks; ++ChunkIndex)
    {
        Chunks[ChunkIndex].Serialize(Ar, Owner, ChunkIndex);
    }
}

void FStreamableOMSData::ReadHeaderSync(oms_header_t* header)
{
    if (header == nullptr)
    {
        return;
    }

    CriticalSection.Lock();

    int64 sizebytes = BulkData.GetBulkDataSize();
    if (sizebytes > 0)
    {
        uint8* data = (uint8*)BulkData.LockReadOnly();
        if (data != nullptr)
        {
            oms_read_header(data, 0, sizebytes, header);
        }
        BulkData.Unlock();
    }

    CriticalSection.Unlock();
}

UOMSFile::UOMSFile()
{

}

bool UOMSFile::ImportFile(const FString& TheFileName)
{
    if (TheFileName.IsEmpty())
    {
        return false;
    }

    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*TheFileName, 0));
    if (Reader)
    {
        SourcePath = TheFileName;
        ImportFile(*Reader.Get());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to load oms data from '%s'"), *TheFileName);
    }

    return true;
}

bool UOMSFile::ImportFile(const void * const InOMSData, const int32 InOMSDataSizeBytes)
{
    return false;
}

void UOMSFile::ImportFile(FArchive& Reader)
{
    oms_header_t header = {};
    Reader.Serialize(&header, sizeof(oms_header_t));

    // Todo: Create a get header size bytes function.
    size_t headerSizeBytes = sizeof(oms_header_t) + sizeof(sequence_table_entry) * header.sequence_count;
    uint8_t* buffer = (uint8_t*)malloc(headerSizeBytes);
    Reader.Seek(0);
    Reader.Serialize(buffer, headerSizeBytes);
    size_t offsetSizeBytes = oms_read_header(buffer, 0, headerSizeBytes, &header);

    StreamableOMSData.FrameCount = header.frame_count;
    StreamableOMSData.Chunks.AddDefaulted(header.sequence_count);

    StreamableOMSData.BulkData.Lock(LOCK_READ_WRITE);
    void* HeaderData = StreamableOMSData.BulkData.Realloc(headerSizeBytes);
    FMemory::Memcpy(HeaderData, buffer, headerSizeBytes);
    StreamableOMSData.BulkData.Unlock();

    for (int sequenceIndex = 0; sequenceIndex < header.sequence_count; sequenceIndex++)
    {
        Reader.Seek(offsetSizeBytes);
        int sequenceSizeBytes = 0;
        Reader.Serialize(&sequenceSizeBytes, sizeof(int));
        Reader.Seek(offsetSizeBytes);

        // The sequence size does not include the integer for its size at the beginning.
        sequenceSizeBytes += 4;

        buffer = (uint8_t*)realloc(buffer, sequenceSizeBytes);
        Reader.Serialize(buffer, sequenceSizeBytes);

        oms_sequence_t sequence = {};
        size_t readSizeBytes = oms_read_sequence(buffer, 0, sequenceSizeBytes, &header, &sequence);
        offsetSizeBytes += readSizeBytes;

        if (header.compression_level == OMS_COMPRESSION_DELTA)
        {
            for (int frameIndex = 0; frameIndex < sequence.delta_frame_count; ++frameIndex)
            {
                StreamableOMSData.FrameToSequenceIndex.Add(sequenceIndex);
                StreamableOMSData.FrameToSequenceFrameOffset.Add(frameIndex);
            }
        }
        else
        {
            for (int frameIndex = 0; frameIndex < sequence.ssdr_frame_count; ++frameIndex)
            {
                StreamableOMSData.FrameToSequenceIndex.Add(sequenceIndex);
                StreamableOMSData.FrameToSequenceFrameOffset.Add(frameIndex);
            }
        }

        StreamableOMSData.Chunks[sequenceIndex].BulkData.Lock(LOCK_READ_WRITE);
        void* ChunkData = StreamableOMSData.Chunks[sequenceIndex].BulkData.Realloc(sequenceSizeBytes);
        FMemory::Memcpy(ChunkData, buffer, sequenceSizeBytes);
        StreamableOMSData.Chunks[sequenceIndex].BulkData.Unlock();
        oms_free_sequence(&sequence);
    }

    oms_free_header(&header);
    free(buffer);
}

void UOMSFile::Serialize(FArchive& Ar)
{
    // Note: the calls inside this function are bi-directional and used for both load and save.

    Super::Serialize(Ar);
    Ar.UsingCustomVersion(FOMSFileVersion::GUID);

#if WITH_EDITORONLY_DATA
    if (Ar.IsLoading())
    {
        if (Ar.CustomVer(FOMSFileVersion::GUID) < FOMSFileVersion::BeforeCustomVersionWasAdded)
        {
            ConvertFromOMSData();
            // Skip reading Streamable OMS data (not written).
            return;
        }    
        
        if (Ar.CustomVer(FOMSFileVersion::GUID) < FOMSFileVersion::FixMissingTail)
        {
            // Data is missing 4 bytes on the end.
        }
    }
#endif // #if WITH_EDITORONLY_DATA

    StreamableOMSData.Serialize(Ar, this);

    if (Ar.CustomVer(FOMSFileVersion::GUID) >= FOMSFileVersion::KeepFilePath)
    {
        if (SourcePath.IsEmpty())
        {
            SourcePath = GetPath();
        }
        Ar << SourcePath;
    }
}

void UOMSFile::ConvertFromOMSData()
{
    // Wrap in scope to descruct.
    {
        FMemoryReader Reader(OMSData_DEPRECATED);
        ImportFile(Reader);
    }

    // Discard data. 
    OMSData_DEPRECATED.Empty();
}

IStreamableHoloSuiteData& UOMSFile::GetStreamableData()
{
    return StreamableOMSData;
}

FString UOMSFile::GetPath()
{
    if (SourcePath.IsEmpty())
    {
        FString Filename = FPackageName::LongPackageNameToFilename(GetPathName());
        Filename = FPaths::ConvertRelativePathToFull(*Filename);
        FString omsPath = FPathViews::ChangeExtension(Filename, TEXT(".oms"));

        if (FPaths::FileExists(omsPath))
        {
            SourcePath = omsPath;
        }
    }
    return SourcePath;
}