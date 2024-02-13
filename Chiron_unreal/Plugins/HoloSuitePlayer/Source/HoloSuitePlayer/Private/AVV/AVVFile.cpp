// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "AVV/AVVFile.h"
#include "AVV/AVVFormat.h"
#include "AVV/AVVDecoder.h"

#include "Async/Async.h"

#define AVV_READ(DST, SRC, POSITION, TYPE, NUM_ELEMENTS) memcpy(&DST, &SRC[POSITION], sizeof(TYPE) * NUM_ELEMENTS); POSITION += sizeof(TYPE) * NUM_ELEMENTS;

// Unique AVV Object version id
const FGuid FAVVFileVersion::GUID(0xEF7A3040, 0x4F8208DF, 0xC2053CA9, 0x5BB981D8);
// Register AVV custom version with Core
FDevVersionRegistration GRegisterAVVFileVersion(FAVVFileVersion::GUID, FAVVFileVersion::LatestVersion, TEXT("AVV"));

void FAVVStreamableContainer::Serialize(FArchive& Ar, UAVVFile* Owner, int32 ContainerIndex)
{
    // Prevents actually being loaded into memory until its requested.
    BulkData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
    BulkData.Serialize(Ar, Owner, ContainerIndex, false);
}

bool FAVVStreamableContainer::Read(uint8_t* outputBuffer, size_t outputBufferSize)
{
    size_t dataSize = BulkData.GetBulkDataSize();

    if (dataSize > outputBufferSize)
    {
        return false;
    }

    FBulkDataIORequestCallBack AsyncFileCallBack =
        [this](bool bWasCancelled, IBulkDataIORequest* Req)
    {
        // Do nothing the data is in the buffer.
    };

    // Loaded in Editor.
    if (BulkData.IsBulkDataLoaded())
    {
        uint8* data = (uint8*)BulkData.Lock(LOCK_READ_ONLY);
        memcpy(outputBuffer, data, dataSize);
        BulkData.Unlock();
        return true;
    }
    else 
    {
        const EAsyncIOPriorityAndFlags AsyncIOPriority = AIOP_High;
        IBulkDataIORequest* IORequest = BulkData.CreateStreamingRequest(AsyncIOPriority, &AsyncFileCallBack, outputBuffer);

        if (IORequest)
        {
            IORequest->WaitCompletion();
            // Free AsyncHandle Resources.
            delete IORequest;
            IORequest = nullptr;
        }
    }

    return true;
}

FAVVIORequestRef FAVVStreamableContainer::ReadAsync(uint8_t* outputBuffer, size_t outputBufferSize)
{
    FAVVIORequestRef Result = MakeShared<FAVVIORequest, ESPMode::ThreadSafe>();
    size_t dataSize = BulkData.GetBulkDataSize();
    Result->StartTime = FPlatformTime::Seconds();
    Result->SizeInBytes = dataSize;

    if (dataSize > outputBufferSize)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("AVV Reader: dataSize > outputBufferSize. %d > %d"), dataSize, outputBufferSize);
        Result->Status = FAVVIORequest::EStatus::Error;
        return Result;
    }

    // Loaded in Editor.
    if (BulkData.IsBulkDataLoaded())
    {
        Result->Status = FAVVIORequest::EStatus::Waiting;
        Result->Request = nullptr;

        // This function can get called from a worker thread through AVVReader and multiple
        // locks from off game thread from players using the same file seems unstable.
        AsyncTask(ENamedThreads::GameThread, [this, Result, outputBuffer, dataSize]
        {
            uint8* data = (uint8*)BulkData.LockReadOnly();
            memcpy(outputBuffer, data, dataSize);
            BulkData.Unlock();

            Result->EndTime = FPlatformTime::Seconds();
            Result->Status = FAVVIORequest::EStatus::Completed;
        });
    }
    else
    {
        FBulkDataIORequestCallBack AsyncFileCallBack = [Result](bool bWasCancelled, IBulkDataIORequest* Req)
        {
            Result->EndTime = FPlatformTime::Seconds();
        };

        const EAsyncIOPriorityAndFlags AsyncIOPriority = AIOP_High;
        Result->Status = FAVVIORequest::EStatus::Waiting;
        Result->Request = BulkData.CreateStreamingRequest(AsyncIOPriority, &AsyncFileCallBack, outputBuffer);
    }

    return Result;
}

// Upgrades a file that was previous serialized in a per-segment manner.
void FStreamableAVVData::UpgradeFromPerSegment(FArchive& Ar, class UAVVFile* Owner)
{
    Ar << Version;

    TArray<FAVVStreamableContainer> OldContainers;

    int32 NumContainers = OldContainers.Num();
    Ar << NumContainers;
    MetaData.Serialize(Ar, Owner, INDEX_NONE, false);

    OldContainers.Reset(NumContainers);
    OldContainers.AddDefaulted(NumContainers);

    MaxSegmentSizeBytes = 0;
    MaxFrameSizeBytes = 0;
    MaxFrameTextureSizeBytes = 0;

    SegmentContainers.Reset();
    FrameContainers.Reset();
    FrameTextureContainers.Reset();

    for (int32 i = 0; i < NumContainers; ++i)
    {
        OldContainers[i].Serialize(Ar, Owner, i);

        int64 SizeInBytes = OldContainers[i].BulkData.GetBulkDataSize();
        uint8_t* Buffer = (uint8_t*)FMemory::Malloc(SizeInBytes);

        void* ContainerData = OldContainers[i].BulkData.Lock(LOCK_READ_ONLY);
        FMemory::Memcpy(Buffer, ContainerData, SizeInBytes);
        OldContainers[i].BulkData.Unlock();

        ImportSegment(Buffer, SizeInBytes);

        FMemory::Free(Buffer);
    }
}

void FStreamableAVVData::Serialize(FArchive& Ar, UAVVFile* Owner)
{
    Ar << Version;

    MetaData.Serialize(Ar, Owner, INDEX_NONE, false);

    Ar << MaxSegmentSizeBytes;
    Ar << MaxFrameSizeBytes;
    Ar << MaxFrameTextureSizeBytes;

    int32 NumSegmentContainers = SegmentContainers.Num();
    Ar << NumSegmentContainers;

    int32 NumFrameContainers = FrameContainers.Num();
    Ar << NumFrameContainers;

    int32 NumFrameTextureContainers = FrameTextureContainers.Num();
    Ar << NumFrameTextureContainers;
    
    if (Ar.IsLoading())
    {
        SegmentContainers.Reset(NumSegmentContainers);
        SegmentContainers.AddDefaulted(NumSegmentContainers);

        FrameContainers.Reset(NumFrameContainers);
        FrameContainers.AddDefaulted(NumFrameContainers);

        FrameTextureContainers.Reset(NumFrameTextureContainers);
        FrameTextureContainers.AddDefaulted(NumFrameTextureContainers);
    }

    for (int32 i = 0; i < NumSegmentContainers; ++i)
    {
        SegmentContainers[i].Serialize(Ar, Owner, i);
    }

    for (int32 i = 0; i < NumFrameContainers; ++i)
    {
        FrameContainers[i].Serialize(Ar, Owner, i);
    }

    for (int32 i = 0; i < NumFrameTextureContainers; ++i)
    {
        FrameTextureContainers[i].Serialize(Ar, Owner, i);
    }
} 

void PatchPosSkinExpand(uint8_t* Buffer, uint32_t readPos, uint8_t* seqData, uint32_t segPos, uint32_t segContainerSize, uint8_t** updatedBufferOut, uint32_t* updatedBufferSizeOut)
{
    AVVEncodedSegment tempSegment;

    AVV_READ(tempSegment.aabbMin, seqData, segPos, float, 3);
    AVV_READ(tempSegment.aabbMax, seqData, segPos, float, 3);
    AVV_READ(tempSegment.vertexCount, seqData, segPos, uint32_t, 1);
    AVV_READ(tempSegment.compactVertexCount, seqData, segPos, uint32_t, 1);

    uint32_t endOfV1Header = segPos;

    AVV_READ(tempSegment.expansionListCount, seqData, segPos, uint32_t, 1);
    tempSegment.expansionListOffset = readPos + segPos;
    segPos += tempSegment.expansionListCount;

    // Build VertexWriteData
    tempSegment.vertexWriteTable.SetNum(tempSegment.compactVertexCount);
    uint32_t vertexWriteLocation = 0;
    uint8_t expansionListValue = 0;
    uint32_t encodedValue = 0;
    for (uint32_t v = 0; v < tempSegment.compactVertexCount; ++v)
    {
        expansionListValue = Buffer[tempSegment.expansionListOffset + v];
        encodedValue = (expansionListValue << 24) | vertexWriteLocation;
        vertexWriteLocation += expansionListValue;

        tempSegment.vertexWriteTable[v] = encodedValue;
    }

    tempSegment.posOnlySegment = false;
    tempSegment.vertexDataOffset = readPos + segPos;
    tempSegment.vertexDataSize = segContainerSize - segPos;

    // Calculate and allocate the new size we need to store the updated values.
    uint32_t updatedContainerType = AVV_SEGMENT_POS_SKIN_EXPAND_128_V2;
    uint32_t updatedBufferSize = (segContainerSize - tempSegment.expansionListCount - 4) + (tempSegment.compactVertexCount * 4);
    uint8_t* updatedBuffer = (uint8_t*)FMemory::Malloc(updatedBufferSize);

    uint32_t writePos = 0;
    memcpy(&updatedBuffer[writePos], &updatedContainerType, sizeof(uint32_t));
    writePos += sizeof(uint32_t);
    memcpy(&updatedBuffer[writePos], &updatedBufferSize, sizeof(uint32_t));
    writePos += sizeof(uint32_t);

    // Copy AABB and vertex counts from v1.
    memcpy(&updatedBuffer[writePos], &seqData[0], endOfV1Header);
    writePos += endOfV1Header;

    // Copy newly generated vertex write table.
    uint32_t sizeOfVertexWriteData = tempSegment.vertexWriteTable.Num() * sizeof(uint32_t);
    memcpy(&updatedBuffer[writePos], tempSegment.vertexWriteTable.GetData(), sizeOfVertexWriteData);
    writePos += sizeOfVertexWriteData;

    // Copy the rest of the original data.
    memcpy(&updatedBuffer[writePos], &Buffer[tempSegment.vertexDataOffset], tempSegment.vertexDataSize);

    *updatedBufferSizeOut = updatedBufferSize;
    *updatedBufferOut = updatedBuffer;
}

void FStreamableAVVData::ImportSegment(uint8_t* Buffer, SIZE_T SizeInBytes)
{
    auto CopyIntoContainer = [](FAVVStreamableContainer& TargetContainer, uint8_t* SourceData, uint32_t SizeInBytes)
    {
        TargetContainer.BulkData.Lock(LOCK_READ_WRITE);
        void* ContainerData = TargetContainer.BulkData.Realloc(SizeInBytes);
        FMemory::Memcpy(ContainerData, SourceData, SizeInBytes);
        TargetContainer.BulkData.Unlock();
    };

    SIZE_T readPos = 0;

    uint32_t containerType;
    uint32_t containerSize;
    
    AVV_READ(containerType, Buffer, readPos, uint32_t, 1);
    AVV_READ(containerSize, Buffer, readPos, uint32_t, 1);

    if (containerType == AVV_SEGMENT_FRAMES)
    {
        uint32_t segmentDataCount;
        AVV_READ(segmentDataCount, Buffer, readPos, uint32_t, 1);

        SIZE_T segmentDataStart = 8;
        SIZE_T segmentDataSize = 0;

        bool updatedSegmentPosSkinExpand = false;
        uint8_t* updatedBuffer = nullptr;
        uint32_t updatedBufferSize = 0;
        uint32_t updateOldStart = 0;
        uint32_t updateOldEnd = 0;

        for (uint32_t j = 0; j < segmentDataCount; ++j)
        {
            uint32_t segContainerType;
            uint32_t segContainerSize;

            AVV_READ(segContainerType, Buffer, readPos, uint32_t, 1);
            AVV_READ(segContainerSize, Buffer, readPos, uint32_t, 1);
            uint8_t* segData = Buffer + readPos;
            uint32_t segPos = 0;

            if (segContainerType == AVV_SEGMENT_POS_SKIN_EXPAND_128)
            {
                // HACK: we're going to upgrade this to V2 at import time because
                // v1 has a flaw that makes it slow to decode.

                PatchPosSkinExpand(Buffer, readPos, segData, segPos, segContainerSize, &updatedBuffer, &updatedBufferSize);
                updatedSegmentPosSkinExpand = true;
                updateOldStart = (readPos - 8);
                updateOldEnd = readPos + segContainerSize;
            }

            readPos += segContainerSize;
            segmentDataSize += segContainerSize;
        }

        if (updatedSegmentPosSkinExpand)
        {
            FAVVStreamableContainer& SegmentContainer = SegmentContainers.Emplace_GetRef();

            uint32_t updatedSegmentSize = (updatedBufferSize + 8);

            uint32_t sliceSize = updateOldEnd - updateOldStart;
            uint32_t newSize = segmentDataSize - sliceSize + updatedSegmentSize;

            uint8_t* tempBuffer = (uint8_t*)FMemory::Malloc(newSize);
            memcpy(&tempBuffer[0], &Buffer[segmentDataStart], updateOldStart - segmentDataStart);
            memcpy(&tempBuffer[updateOldStart - segmentDataStart], updatedBuffer, updatedSegmentSize);
            memcpy(&tempBuffer[updateOldStart - segmentDataStart + updatedSegmentSize], &Buffer[updateOldEnd], segmentDataSize - updateOldEnd);

            CopyIntoContainer(SegmentContainer, tempBuffer, newSize);
            FMemory::Free(updatedBuffer);
            FMemory::Free(tempBuffer);

            MaxSegmentSizeBytes = FMath::Max(MaxSegmentSizeBytes, (SIZE_T)newSize);
        }
        else 
        {
            FAVVStreamableContainer& SegmentContainer = SegmentContainers.Emplace_GetRef();
            CopyIntoContainer(SegmentContainer, &Buffer[segmentDataStart], segmentDataSize);

            MaxSegmentSizeBytes = FMath::Max(MaxSegmentSizeBytes, segmentDataSize);
        }

        uint32_t frameCount;
        AVV_READ(frameCount, Buffer, readPos, uint32_t, 1);

        for (uint32_t j = 0; j < frameCount; ++j)
        {
            uint32_t frameDataCount = 0;
            AVV_READ(frameDataCount, Buffer, readPos, uint32_t, 1);

            std::vector<uint8_t> frameData;
            frameData.resize(4); // Reserve for data container count.
            uint32_t finalFrameDataCount = 0;

            for (uint32_t k = 0; k < frameDataCount; ++k)
            {
                uint32_t frameContainerStart = readPos;

                uint32_t frameContainerType;
                uint32_t frameContainerSize;

                AVV_READ(frameContainerType, Buffer, readPos, uint32_t, 1);
                AVV_READ(frameContainerSize, Buffer, readPos, uint32_t, 1);

                if (frameContainerType == AVV_FRAME_TEXTURE_LUMA_BC4)
                {
                    FrameTextureContainers.AddDefaulted();
                    uint32_t frameTexIdx = FrameTextureContainers.Num() - 1;
                    CopyIntoContainer(FrameTextureContainers[frameTexIdx], &Buffer[frameContainerStart], frameContainerSize);

                    MaxFrameTextureSizeBytes = FMath::Max(MaxFrameTextureSizeBytes, (SIZE_T)frameContainerSize);
                }
                else
                {
                    uint32_t initialLocation = frameData.size();
                    frameData.resize(initialLocation + frameContainerSize + 8);
                    FMemory::Memcpy(&frameData[initialLocation], &Buffer[frameContainerStart], frameContainerSize + 8);
                    finalFrameDataCount++;
                }

                readPos += frameContainerSize;
            }

            // Write count into frame data.
            FMemory::Memcpy(&frameData[0], &finalFrameDataCount, sizeof(uint32_t));

            FrameContainers.AddDefaulted();
            uint32_t frameContainerIdx = FrameContainers.Num() - 1;
            CopyIntoContainer(FrameContainers[frameContainerIdx], frameData.data(), frameData.size());

            MaxFrameSizeBytes = FMath::Max(MaxFrameSizeBytes, (SIZE_T)frameData.size());
        }
    }
}

uint8_t* FStreamableAVVData::ReadMetaData()
{
    size_t dataSize = MetaData.GetBulkDataSize();
    
    FBulkDataIORequestCallBack AsyncFileCallBack =
        [this](bool bWasCancelled, IBulkDataIORequest* Req)
    {
        // Do nothing the data is in the buffer.
    };

    uint8_t* outputBuffer = new uint8_t[dataSize];
    
    // Loaded in Editor.
    if (MetaData.IsBulkDataLoaded())
    {
        uint8* data = (uint8*)MetaData.Lock(LOCK_READ_ONLY);
        memcpy(outputBuffer, data, dataSize);
        MetaData.Unlock();
    }
    // Load on-demand in Runtime.
    else
    {
        const EAsyncIOPriorityAndFlags AsyncIOPriority = AIOP_CriticalPath;
        IBulkDataIORequest* IORequest = MetaData.CreateStreamingRequest(AsyncIOPriority, &AsyncFileCallBack, outputBuffer);

        if (IORequest)
        {
            IORequest->WaitCompletion();
            // Free AsyncHandle Resources.
            delete IORequest;
            IORequest = nullptr;
        }
    }

    return outputBuffer;
}

UAVVFile::UAVVFile()
{

}

bool UAVVFile::ImportFile(const FString& TheFileName)
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
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("Failed to load AVV data from '%s'"), *TheFileName);
    }

    return true;
}

void UAVVFile::ImportFile(FArchive& Reader)
{
    char headerTag[4];
    uint32_t metaContainerCount;
    uint32_t segmentContainerCount;

    Reader.Serialize(&headerTag, sizeof(headerTag));

    Reader.Serialize(&StreamableAVVData.Version, sizeof(uint32_t));

    if (StreamableAVVData.Version != AVV_VERSION)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("Unsupported AVV Version: %d"), StreamableAVVData.Version);
        return;
    }

    uint32_t metaDataStart = Reader.Tell();
    uint32_t metaDataEnd = 0;

    Reader.Serialize(&metaContainerCount, sizeof(uint32_t));

    StreamableAVVData.ContainsSkeleton = 0;

    uint32_t containerType;
    uint32_t containerSize;
    // Store all the meta data for decoding later.
    for (uint32_t i = 0; i < metaContainerCount; ++i)
    {
        Reader.Serialize(&containerType, sizeof(uint32_t));
        Reader.Serialize(&containerSize, sizeof(uint32_t));

        if (containerType == AVV_META_SKELETON)
        {
            StreamableAVVData.ContainsSkeleton = 1;
        }

        int pos = Reader.Tell();
        Reader.Seek(pos + containerSize);
    }

    metaDataEnd = Reader.Tell();
    uint32_t metaDataSize = metaDataEnd - metaDataStart;
    
    Reader.Seek(metaDataStart);
    uint8_t* MetaBuffer = (uint8_t*)FMemory::Malloc(metaDataSize);
    Reader.Serialize(MetaBuffer, metaDataSize);

    StreamableAVVData.MetaData.Lock(LOCK_READ_WRITE);
    void* metaData = StreamableAVVData.MetaData.Realloc(metaDataSize);
    FMemory::Memcpy(metaData, MetaBuffer, metaDataSize);
    StreamableAVVData.MetaData.Unlock();

    Reader.Serialize(&segmentContainerCount, sizeof(uint32_t));

    StreamableAVVData.MaxSegmentSizeBytes = 0;
    StreamableAVVData.MaxFrameSizeBytes = 0;
    StreamableAVVData.MaxFrameTextureSizeBytes = 0;

    // Store each of the sequence containers in a separate entry.
    for (uint32_t i = 0; i < segmentContainerCount; ++i)
    {
        Reader.Serialize(&containerType, sizeof(uint32_t));
        Reader.Serialize(&containerSize, sizeof(uint32_t));

        containerSize += 8;

        Reader.Seek(Reader.Tell() - 8);
        uint8_t* Buffer = (uint8_t*)FMemory::Malloc(containerSize);
        Reader.Serialize(Buffer, containerSize);

        StreamableAVVData.ImportSegment(Buffer, containerSize);

        FMemory::Free(Buffer);
    }
}

void UAVVFile::Serialize(FArchive& Ar)
{
    // Note: the calls inside this function are bi-directional and used for both load and save.

    Super::Serialize(Ar);
    Ar.UsingCustomVersion(FAVVFileVersion::GUID);

    if (Ar.IsLoading() && Ar.CustomVer(FAVVFileVersion::GUID) < FAVVFileVersion::PerFrameDataStorage)
    {
        // Data was stored in per-segment containers so we upgrade it to per frame containers
        // and mark the asset dirty so it will be saved in this form.
        StreamableAVVData.UpgradeFromPerSegment(Ar, this);
        GetOutermost()->SetDirtyFlag(true);
    }
    else 
    {
        StreamableAVVData.Serialize(Ar, this);
    }

    if (Ar.CustomVer(FAVVFileVersion::GUID) >= FAVVFileVersion::KeepFilePath)
    {
        if (SourcePath.IsEmpty())
        {
            SourcePath = GetPath();
        }
        Ar << SourcePath;
    }
}

IStreamableHoloSuiteData& UAVVFile::GetStreamableData()
{
    return StreamableAVVData;
}

FString UAVVFile::GetPath()
{
    if (SourcePath.IsEmpty())
    {
        FString Filename = FPackageName::LongPackageNameToFilename(GetPathName());
        Filename = FPaths::ConvertRelativePathToFull(*Filename);
        // Given that, at the time that KeepFilePath AVV version was added, .avv export was still not available,
        // we can assume that it'll be an .amsc file.
        FString amscPath = FPathViews::ChangeExtension(Filename, TEXT(".amsc"));

        if (FPaths::FileExists(amscPath))
        {
            SourcePath = amscPath;
        }
    }
    return SourcePath;
}