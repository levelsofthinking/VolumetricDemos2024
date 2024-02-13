// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "AVV/AVVReader.h"
#include "Async/Async.h"

DECLARE_CYCLE_STAT(TEXT("AVVReader.Constructor"),                   STAT_AVVReader_Constructor,                 STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.Destructor"),                    STAT_AVVReader_Destructor,                  STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.Open"),                          STAT_AVVReader_Open,                        STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.Close"),                         STAT_AVVReader_Close,                       STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.Update"),                        STAT_AVVReader_Update,                      STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.DecodeMetaSkeleton"),            STAT_AVVReader_DecodeMetaSkeleton,          STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.AllocateSegment"),               STAT_AVVReader_AllocateSegment,             STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.PreAllocateSegments"),           STAT_AVVReader_PreAllocateSegments,         STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.GetFinishedRequest"),            STAT_AVVReader_GetFinishedRequest,          STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.RequestSegment"),                STAT_AVVReader_RequestSegment,              STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.PrepareSegment"),                STAT_AVVReader_PrepareSegment,              STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.PrepareFrame"),                  STAT_AVVReader_PrepareFrame,                STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.PrepareFrameTexture"),           STAT_AVVReader_PrepareFrameTexture,         STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.GetSegmentAndFrame"),            STAT_AVVReader_GetSegmentAndFrame,          STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVReader.DecodeSkeletonPosRotations"),    STAT_AVVReader_DecodeSkeletonPosRotations,  STATGROUP_HoloSuitePlayer);

#define AVV_READ(DST, SRC, POSITION, TYPE, NUM_ELEMENTS) memcpy(&DST, &SRC[POSITION], sizeof(TYPE) * NUM_ELEMENTS); POSITION += sizeof(TYPE) * NUM_ELEMENTS;

// Number of containers pre-allocated when a file is first opened. Prevents a hitch during initial playback.
#define AVV_PREALLOCATED_CONTAINER_COUNT 4

// Decodes the position and rotation portions of a skeleton encoding.
void DecodeSkeletonPosRotations(uint8_t* dataPtr, uint32_t boneCount, AVVSkeleton& skeletonOut);

FAVVReader::FAVVReader()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_Constructor);

    readerState = EAVVReaderState::None;
    openFile = nullptr;
}

FAVVReader::~FAVVReader()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_Destructor);

    Close();
    openFile = nullptr;
}

bool FAVVReader::Open(UAVVFile* avvFile)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_Open);

    if (avvFile == nullptr)
    {
        return false;
    }

    FStreamableAVVData& streamableData = (FStreamableAVVData&) avvFile->GetStreamableData();
    if (streamableData.Version != AVV_VERSION)
    {
        std::cout << "Unsupported AVV Version: " << (int)streamableData.Version << std::endl;
        return false;
    }

    openFile = avvFile;

    Version = streamableData.Version;
    uint32_t MajorVersion = (streamableData.Version & 0xFFFF0000) >> 16;
    uint32_t MinorVersion = (streamableData.Version & 0x0000FFFF);
    VersionString = FString::Printf(TEXT("%d.%d"), MajorVersion, MinorVersion);

    uint8_t* data = streamableData.ReadMetaData();
    size_t readPos = 0;

    uint32_t containerType;
    uint32_t containerSize;

    // Decode meta containers.
    AVV_READ(metaContainerCount, data, readPos, uint32_t, 1);
    for (uint32_t i = 0; i < metaContainerCount; ++i)
    {
        AVV_READ(containerType, data, readPos, uint32_t, 1);
        AVV_READ(containerSize, data, readPos, uint32_t, 1);

        if (containerType == AVV_META_SEGMENT_TABLE)
        {
            uint32_t sequenceEntryCount;
            AVV_READ(sequenceEntryCount, data, readPos, uint32_t, 1);
            SegmentTable.resize(sequenceEntryCount);

            for (uint32_t j = 0; j < sequenceEntryCount; ++j)
            {
                AVVSegmentTableEntry& entry = SegmentTable[j];
                AVV_READ(entry.byteStart,   data, readPos, uint32_t, 1);
                AVV_READ(entry.byteLength,  data, readPos, uint32_t, 1);
                AVV_READ(entry.frameCount,  data, readPos, uint32_t, 1);
                AVV_READ(entry.vertexCount, data, readPos, uint32_t, 1);
                AVV_READ(entry.indexCount,  data, readPos, uint32_t, 1);
            }
        }
        else if (containerType == AVV_META_LIMITS)
        {
            AVV_READ(Limits.MaxContainerSize,     data, readPos, uint32_t, 1);
            AVV_READ(Limits.MaxVertexCount,       data, readPos, uint32_t, 1);
            AVV_READ(Limits.MaxIndexCount,        data, readPos, uint32_t, 1);
            AVV_READ(Limits.MaxFrameCount,        data, readPos, uint32_t, 1);
            AVV_READ(Limits.MaxBoneCount,         data, readPos, uint32_t, 1);
            AVV_READ(Limits.MaxTextureWidth,      data, readPos, uint32_t, 1);
            AVV_READ(Limits.MaxTextureHeight,     data, readPos, uint32_t, 1);
            AVV_READ(Limits.MaxTextureTriangles,  data, readPos, uint32_t, 1);
            AVV_READ(Limits.MaxTextureBlocks,     data, readPos, uint32_t, 1);
            AVV_READ(Limits.MaxLumaPixels,        data, readPos, uint32_t, 1);
        }
        else if (containerType == AVV_META_SKELETON)
        {
            AVV_READ(MetaSkeleton.skeletonIndex, data, readPos, uint32_t, 1);
            AVV_READ(MetaSkeleton.boneCount, data, readPos, uint32_t, 1);

            MetaSkeleton.boneInfo.resize(MetaSkeleton.boneCount);
            for (uint32 b = 0; b < MetaSkeleton.boneCount; ++b)
            {
                AVV_READ(MetaSkeleton.boneInfo[b].parentIndex, data, readPos, int, 1);
                AVV_READ(MetaSkeleton.boneInfo[b].name, data, readPos, char, 32);

                FString BoneName = FString(UTF8_TO_TCHAR(MetaSkeleton.boneInfo[b].name));
                //UE_LOG(LogHoloSuitePlayer, Display, TEXT("Loaded Skeleton Bone %s Parent %d"), *BoneName, MetaSkeleton.boneInfo[b].parentIndex);
            }

            DecodeSkeletonPosRotations(data + readPos, MetaSkeleton.boneCount, MetaSkeleton);
        }
        else
        {
            readPos += containerSize;
        }
    }

    // Populate sequence lookup tables.
    FrameCount = 0;
    SegmentCount = SegmentTable.size();
    sequenceLookupTable.clear();
    sequenceStartFrames.clear();
    for (int i = 0; i < SegmentCount; ++i)
    {
        sequenceStartFrames.push_back(FrameCount);
        for (uint32_t f = 0; f < SegmentTable[i].frameCount; ++f)
        {
            FrameCount++;
            sequenceLookupTable.push_back(i);
        }
    }

    FrameToSegment.clear();
    for (size_t i = 0; i < SegmentTable.size(); ++i)
    {
        for (uint32_t j = 0; j < SegmentTable[i].frameCount; ++j)
        {
            FrameToSegment.push_back(i);
        }
    }

    // Cleanup.
    delete[] data;

    readerState = EAVVReaderState::Ready;
    return true;
}

void FAVVReader::Close()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_Close);
}

void FAVVReader::Update()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_Update);

    if (!CriticalSection.TryLock())
    {
        // If we can't lock this then an update is already in progress and there would
        // be no value is us waiting for it to finish.
        return;
    }

    FScopeLockHold LockHold(&CriticalSection);

    if (readerState == EAVVReaderState::WaitingIO)
    {
        FAVVReaderRequestRef request;
        if (waitingRequests.Peek(request))
        {
            int processedRequests = 0;
            bool foundError = false;

            for (auto& ioRequest : request->IORequests)
            {
                ioRequest->PollCompletion();

                if (ioRequest->Status == FAVVIORequest::EStatus::Completed)
                {
                    double ioRequestTime = ioRequest->EndTime - ioRequest->StartTime;
                    GHoloMeshManager.AddIOResult(ioRequest->SizeInBytes, ioRequestTime * 1000.0f);

                    if (ioRequest->Type == FAVVIORequest::EType::Segment)
                    {
                        PrepareSegment(request->segment);
                    }
                    if (ioRequest->Type == FAVVIORequest::EType::Frame)
                    {
                        PrepareFrame(request->frame);
                    }
                    if (ioRequest->Type == FAVVIORequest::EType::Texture)
                    {
                        PrepareFrameTexture(request->frame);
                    }

                    if (ioRequest->Request != nullptr)
                    {
                        // There are seemingly unstable costs to deleting the finished IORequest,
                        // offload it to an AsyncTask since nothing is relying on it anymore.
                        AsyncTask(ENamedThreads::AnyThread, [request = ioRequest->Request]
                        {
                            delete request;
                        });

                        ioRequest->Request = nullptr;
                    }

                    ioRequest->Status = FAVVIORequest::EStatus::Processed;
                }

                if (ioRequest->Status == FAVVIORequest::EStatus::Processed)
                {
                    processedRequests++;
                }

                foundError |= (ioRequest->Status == FAVVIORequest::EStatus::Error);
            }

            // Check if this reader request is completed.
            if (processedRequests == request->pendingIORequestCount.load())
            {
                this->finishedRequests.Enqueue(request);
                this->waitingRequests.Pop();
                request = nullptr;
            }

            if (foundError)
            {
                UE_LOG(LogHoloSuitePlayer, Error, TEXT("Error occured processing AVVReader request."));
                activeFrameNumbers.Remove(request->frameNumber);
                this->waitingRequests.Pop();
                request = nullptr;
            }
        }

        if (waitingRequests.IsEmpty())
        { 
            readerState = EAVVReaderState::Ready;
        }
    }

    if (readerState == EAVVReaderState::Ready)
    {
        FAVVReaderRequestRef request = nullptr;
        if (!pendingRequests.Peek(request))
        {
            return;
        }
        if (!request.IsValid())
        {
            return;
        }

        FStreamableAVVData& streamableData = (FStreamableAVVData&)openFile->GetStreamableData();

        request->pendingIORequestCount = 0;
        {
            request->pendingIORequestCount += (request->segmentIndex > -1) ? 1 : 0;
            request->pendingIORequestCount += (request->frameNumber > -1) ? 1 : 0;
            request->pendingIORequestCount += request->requestedTexture ? 1 : 0;
        }

        // Segment Request
        if (request->segmentIndex > -1)
        {
            int segmentIdx = request->segmentIndex;

            if (segmentIdx >= streamableData.SegmentContainers.Num())
            {
                UE_LOG(LogHoloSuitePlayer, Error, TEXT("Sequence out of bounds: %d"), segmentIdx);
                return;
            }

            FAVVStreamableContainer& container = streamableData.SegmentContainers[segmentIdx];

            request->segment = new AVVEncodedSegment();
            request->segment->Create(streamableData.MaxSegmentSizeBytes);
            request->segment->segmentIndex = segmentIdx;

            FAVVIORequestRef segmentIORequest = container.ReadAsync(request->segment->content->Data, streamableData.MaxSegmentSizeBytes);
            segmentIORequest->Type = FAVVIORequest::EType::Segment;
            request->IORequests.Add(segmentIORequest);
        }

        // Frame Request
        if (request->frameNumber > -1)
        {
            int frameIdx = request->frameNumber;

            if (request->frameNumber >= streamableData.FrameContainers.Num())
            {
                UE_LOG(LogHoloSuitePlayer, Error, TEXT("Frame out of bounds: %d"), frameIdx);
                return;
            }

            FAVVStreamableContainer& frameContainer = streamableData.FrameContainers[frameIdx];

            SIZE_T textureSize = request->requestedTexture ? streamableData.MaxFrameTextureSizeBytes : 0;

            request->frame = new AVVEncodedFrame();
            request->frame->Create(streamableData.MaxFrameSizeBytes, textureSize);
            request->frame->frameIndex = frameIdx;

            FAVVIORequestRef frameIORequest = frameContainer.ReadAsync(request->frame->content->Data, streamableData.MaxFrameSizeBytes);
            frameIORequest->Type = FAVVIORequest::EType::Frame;
            request->IORequests.Add(frameIORequest);

            // Fetch texture data
            if (request->requestedTexture)
            {
                if (request->frameNumber >= streamableData.FrameTextureContainers.Num())
                {
                    UE_LOG(LogHoloSuitePlayer, Error, TEXT("Frame texture out of bounds: %d"), frameIdx);
                    return;
                }

                FAVVStreamableContainer& frameTextureContainer = streamableData.FrameTextureContainers[frameIdx];
                FAVVIORequestRef textureIORequest = frameTextureContainer.ReadAsync(request->frame->textureContent->Data, streamableData.MaxFrameTextureSizeBytes);
                textureIORequest->Type = FAVVIORequest::EType::Texture;
                request->IORequests.Add(textureIORequest);
            }
        }

        if (request->pendingIORequestCount > 0)
        {
            this->waitingRequests.Enqueue(request);
            this->pendingRequests.Pop();
            readerState = EAVVReaderState::WaitingIO;
        }
    }
}

FAVVReaderRequestRef FAVVReader::GetFinishedRequest()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_GetFinishedRequest);

    FAVVReaderRequestRef request;
    if (finishedRequests.Dequeue(request))
    {
        activeFrameNumbers.Remove(request->frameNumber);
        return request;
    }

    return nullptr;
}

bool FAVVReader::AddRequest(int requestSegmentIndex, int requestFrameNumber, bool requestTexture, bool blockingRequest)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_RequestSegment);

    FAVVReaderRequestRef request = MakeShareable(new FAVVReaderRequest());
    request->segmentIndex = requestSegmentIndex;
    request->frameNumber = requestFrameNumber;
    request->requestedTexture = requestTexture;

    if (request->segmentIndex > -1 && request->segmentIndex >= SegmentCount)
    {
        return false;
    }

    if (request->frameNumber > -1 && request->frameNumber >= FrameCount)
    {
        return false;
    }

    if (activeFrameNumbers.Contains(requestFrameNumber))
    {
        return false;
    }

    if (!blockingRequest)
    {
        pendingRequests.Enqueue(request);
        activeFrameNumbers.Add(request->frameNumber);

        return true;
    }
    else
    {
        FStreamableAVVData& streamableData = (FStreamableAVVData&)openFile->GetStreamableData();

        // Segment Request
        if (request->segmentIndex > -1)
        {
            int segmentIdx = request->segmentIndex;

            if (segmentIdx >= streamableData.SegmentContainers.Num())
            {
                UE_LOG(LogHoloSuitePlayer, Error, TEXT("Sequence out of bounds: %d"), segmentIdx);
                return false;
            }

            FAVVStreamableContainer& container = streamableData.SegmentContainers[segmentIdx];

            request->segment = new AVVEncodedSegment();
            request->segment->Create(streamableData.MaxSegmentSizeBytes);
            request->segment->segmentIndex = segmentIdx;
            container.Read(request->segment->content->Data, streamableData.MaxSegmentSizeBytes);

            PrepareSegment(request->segment);
        }

        // Frame Request
        if (request->frameNumber > -1)
        {
            int frameIdx = request->frameNumber;

            if (request->frameNumber >= streamableData.FrameContainers.Num())
            {
                UE_LOG(LogHoloSuitePlayer, Error, TEXT("Frame out of bounds: %d"), frameIdx);
                return false;
            }

            FAVVStreamableContainer& frameContainer = streamableData.FrameContainers[frameIdx];

            SIZE_T textureSize = request->requestedTexture ? streamableData.MaxFrameTextureSizeBytes : 0;

            request->frame = new AVVEncodedFrame();
            request->frame->Create(streamableData.MaxFrameSizeBytes, textureSize);
            request->frame->frameIndex = frameIdx;
            frameContainer.Read(request->frame->content->Data, streamableData.MaxFrameSizeBytes);
            PrepareFrame(request->frame);

            if (request->requestedTexture)
            {
                if (request->frameNumber >= streamableData.FrameTextureContainers.Num())
                {
                    UE_LOG(LogHoloSuitePlayer, Error, TEXT("Frame out of bounds: %d"), frameIdx);
                    return false;
                }

                FAVVStreamableContainer& frameTextureContainer = streamableData.FrameTextureContainers[frameIdx];
                frameTextureContainer.Read(request->frame->textureContent->Data, streamableData.MaxFrameTextureSizeBytes);
                PrepareFrameTexture(request->frame);
            }
        }

        finishedRequests.Enqueue(request);
        return true;
    }
    
    return false;
}

void FAVVReader::PrepareSegment(AVVEncodedSegment* segment)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_PrepareSegment);

    uint8_t* data = segment->content->Data;
    size_t readPos = 0;

    uint32_t containerType;
    uint32_t containerSize;

    uint32_t segmentDataCount;
    AVV_READ(segmentDataCount, data, readPos, uint32_t, 1);

    for (uint32_t i = 0; i < segmentDataCount; ++i)
    {
        AVV_READ(containerType, data, readPos, uint32_t, 1);
        AVV_READ(containerSize, data, readPos, uint32_t, 1);
        uint8_t* seqData = data + readPos;
        uint32_t seqPos = 0;

        if (containerType == AVV_SEGMENT_POS_16)
        {
            AVV_READ(segment->aabbMin, seqData, seqPos, float, 3);
            AVV_READ(segment->aabbMax, seqData, seqPos, float, 3);
            AVV_READ(segment->vertexCount, seqData, seqPos, uint32_t, 1);

            segment->posOnlySegment = true;
            segment->compactVertexCount = segment->vertexCount;
            segment->expansionListCount = 0;
            segment->vertexDataOffset = readPos + seqPos;
            segment->vertexDataSize = containerSize - seqPos;
        }

        if (containerType == AVV_SEGMENT_POS_SKIN_EXPAND_128)
        {
            AVV_READ(segment->aabbMin, seqData, seqPos, float, 3);
            AVV_READ(segment->aabbMax, seqData, seqPos, float, 3);

            AVV_READ(segment->vertexCount, seqData, seqPos, uint32_t, 1);
            AVV_READ(segment->compactVertexCount, seqData, seqPos, uint32_t, 1);

            AVV_READ(segment->expansionListCount, seqData, seqPos, uint32_t, 1);
            segment->expansionListOffset = readPos + seqPos;
            seqPos += segment->expansionListCount;

            // Build VertexWriteTable
            segment->vertexWriteTable.SetNum(segment->expansionListCount);
            uint32_t vertexWriteLocation = 0;
            uint8_t expansionListValue = 0;
            uint32_t encodedValue = 0;
            for (uint32_t v = 0; v < segment->expansionListCount; ++v)
            {
                expansionListValue = data[segment->expansionListOffset + v];
                encodedValue = (expansionListValue << 24) | vertexWriteLocation;
                vertexWriteLocation += expansionListValue;

                segment->vertexWriteTable[v] = encodedValue;
            }

            segment->posOnlySegment = false;
            segment->vertexDataOffset = readPos + seqPos;
            segment->vertexDataSize = containerSize - seqPos;
        }

        if (containerType == AVV_SEGMENT_POS_SKIN_EXPAND_128_V2)
        {
            AVV_READ(segment->aabbMin, seqData, seqPos, float, 3);
            AVV_READ(segment->aabbMax, seqData, seqPos, float, 3);

            AVV_READ(segment->vertexCount, seqData, seqPos, uint32_t, 1);
            AVV_READ(segment->compactVertexCount, seqData, seqPos, uint32_t, 1);

            segment->expansionListCount = 0;
            segment->expansionListOffset = 0;
            segment->vertexWriteTable.Empty();

            segment->vertexWriteTableOffset = readPos + seqPos;
            seqPos += (segment->compactVertexCount * sizeof(uint32_t));
            
            segment->posOnlySegment = false;
            segment->vertexDataOffset = readPos + seqPos;
            segment->vertexDataSize = containerSize - seqPos;
        }

        if (containerType == AVV_SEGMENT_TRIS_16)
        {
            AVV_READ(segment->indexCount, seqData, seqPos, uint32_t, 1);
            segment->index32Bit = false;
            segment->indexDataOffset = readPos + seqPos;
            segment->indexDataSize = containerSize - seqPos;
        }

        if (containerType == AVV_SEGMENT_TRIS_32)
        {
            AVV_READ(segment->indexCount, seqData, seqPos, uint32_t, 1);
            segment->index32Bit = true;
            segment->indexDataOffset = readPos + seqPos;
            segment->indexDataSize = containerSize - seqPos;
        }

        if (containerType == AVV_SEGMENT_UVS_16)
        {
            AVV_READ(segment->uvCount, seqData, seqPos, uint32_t, 1);
            segment->uvDataOffset = readPos + seqPos;
            segment->uvDataSize = containerSize - seqPos;
            segment->uv12normal888 = false;
        }

        if (containerType == AVV_SEGMENT_UVS_12_NORMALS_888)
        {
            AVV_READ(segment->uvCount, seqData, seqPos, uint32_t, 1);
            segment->uvDataOffset = readPos + seqPos;
            segment->uvDataSize = containerSize - seqPos;
            segment->uv12normal888 = true;
        }

        if (containerType == AVV_SEGMENT_TEXTURE_BLOCKS_32)
        {
            AVV_READ(segment->texture.blockCount, seqData, seqPos, uint32_t, 1);

            uint32_t widthHeight;
            AVV_READ(widthHeight, seqData, seqPos, uint32_t, 1);
            segment->texture.width = (int)(widthHeight >> 16);
            segment->texture.height = (int)(widthHeight & 0xFFFF);

            segment->texture.blockDataOffset = readPos + seqPos;
            segment->texture.blockDataSize = segment->texture.blockCount * 4;
            segment->texture.multiRes = false;
        }

        if (containerType == AVV_SEGMENT_TEXTURE_BLOCKS_MULTIRES_32)
        {
            AVV_READ(segment->texture.blockCount, seqData, seqPos, uint32_t, 1);

            uint32_t widthHeight;
            AVV_READ(widthHeight, seqData, seqPos, uint32_t, 1);
            segment->texture.width = (int)(widthHeight >> 16);
            segment->texture.height = (int)(widthHeight & 0xFFFF);

            segment->texture.levelBlockCounts.clear();
            uint32_t levelsCount;
            AVV_READ(levelsCount, seqData, seqPos, uint32_t, 1);
            for (uint32_t l = 0; l < levelsCount; ++l)
            {
                uint32_t levelBlockCount;
                AVV_READ(levelBlockCount, seqData, seqPos, uint32_t, 1);
                segment->texture.levelBlockCounts.push_back(levelBlockCount);
            }

            segment->texture.blockDataOffset = readPos + seqPos;
            segment->texture.blockDataSize = segment->texture.blockCount * 4;
            segment->texture.multiRes = true;
        }

        if (containerType == AVV_SEGMENT_MOTION_VECTORS)
        {
            AVV_READ(segment->motionVectorsMin, seqData, seqPos, float, 3);
            AVV_READ(segment->motionVectorsMax, seqData, seqPos, float, 3);
            AVV_READ(segment->motionVectorsCount, seqData, seqPos, uint32_t, 1);

            segment->motionVectors = true;
            segment->motionVectorsDataOffset = readPos + seqPos;
            segment->motionVectorsDataSize = containerSize - seqPos;
        }

        readPos += containerSize;
    }
}

void FAVVReader::PrepareFrame(AVVEncodedFrame* frame)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_PrepareFrame);

    uint8_t* data = frame->content->Data;
    size_t readPos = 0;

    uint32_t frameDataCount = 0;
    AVV_READ(frameDataCount, data, readPos, uint32_t, 1);

    for (uint32_t k = 0; k < frameDataCount; ++k)
    {
        uint32_t frameContainerType;
        uint32_t frameContainerSize;

        AVV_READ(frameContainerType, data, readPos, uint32_t, 1);
        AVV_READ(frameContainerSize, data, readPos, uint32_t, 1);

        if (frameContainerType == AVV_FRAME_ANIM_MAT4X4_32)
        {
            ReadFrameAnimMat4x4(data + readPos, readPos, *frame);
        }

        if (frameContainerType == AVV_FRAME_ANIM_POS_ROTATION_128)
        {
            ReadFrameAnimPosRotation128(data + readPos, readPos, *frame);
        }

        if (frameContainerType == AVV_FRAME_ANIM_DELTA_POS_32)
        {
            ReadFrameAnimDeltaPos32(data + readPos, readPos, *frame);
        }

        if (frameContainerType == AVV_FRAME_COLORS_RGB_565)
        {
            ReadFrameColorsRGB565(data + readPos, readPos, *frame);
        }

        if (frameContainerType == AVV_FRAME_COLORS_RGB_565_NORMALS_OCT_16)
        {
            ReadFrameColorsRGB565NormalsOct16(data + readPos, readPos, *frame);
        }

        readPos += frameContainerSize;
    }
}

void FAVVReader::PrepareFrameTexture(AVVEncodedFrame* frame)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_PrepareFrameTexture);

    uint8_t* data = frame->textureContent->Data;
    size_t readPos = 0;

    uint32_t frameContainerType;
    uint32_t frameContainerSize;

    AVV_READ(frameContainerType, data, readPos, uint32_t, 1);
    AVV_READ(frameContainerSize, data, readPos, uint32_t, 1);

    if (frameContainerType == AVV_FRAME_TEXTURE_LUMA_8)
    {
        ReadFrameTextureLuma8(data + readPos, readPos, *frame);
    }

    if (frameContainerType == AVV_FRAME_TEXTURE_LUMA_BC4)
    {
        ReadFrameTextureLumaBC4(data + readPos, readPos, *frame);
    }
}

void FAVVReader::ReadFrameAnimMat4x4(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut)
{
    uint32_t dataPos = 0;

    AVV_READ(decodedFrameOut.ssdrBoneCount, frameData, dataPos, uint32_t, 1);

    decodedFrameOut.ssdrMatrixData = (float*)FMemory::Malloc(decodedFrameOut.ssdrBoneCount * 16 * sizeof(float));
    for (uint32_t i = 0; i < decodedFrameOut.ssdrBoneCount; ++i)
    {
        // Note: Swizzled for Unreal.
        for (int k = 0; k < 4; ++k)
        {
            for (int j = 0; j < 4; ++j)
            {
                AVV_READ(decodedFrameOut.ssdrMatrixData[(i * 16) + (j * 4) + k], frameData, dataPos, float, 1);
            }
        }
    }
}

void FAVVReader::ReadFrameAnimPosRotation128(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut)
{
    size_t dataPos = 0;

    AVV_READ(decodedFrameOut.skeleton.skeletonIndex, frameData, dataPos, uint32_t, 1);
    AVV_READ(decodedFrameOut.skeleton.boneCount, frameData, dataPos, uint32_t, 1);

    decodedFrameOut.skeleton.boneInfo = MetaSkeleton.boneInfo;

    DecodeSkeletonPosRotations(frameData + dataPos, decodedFrameOut.skeleton.boneCount, decodedFrameOut.skeleton);
}

void FAVVReader::ReadFrameAnimDeltaPos32(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut)
{
    size_t dataPos = 0;

    AVV_READ(decodedFrameOut.deltaAABBMin, frameData, dataPos, float, 3);
    AVV_READ(decodedFrameOut.deltaAABBMax, frameData, dataPos, float, 3);
    AVV_READ(decodedFrameOut.deltaPosCount, frameData, dataPos, uint32_t, 1);

    decodedFrameOut.deltaDataOffset = readPos + dataPos;
    decodedFrameOut.deltaDataSize = decodedFrameOut.deltaPosCount * 4;
}

void FAVVReader::ReadFrameTextureLuma8(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut)
{
    uint32_t dataPos = 0;

    AVV_READ(decodedFrameOut.lumaCount, frameData, dataPos, uint32_t, 1);
    decodedFrameOut.lumaDataOffset = readPos + dataPos;
    decodedFrameOut.lumaDataSize = decodedFrameOut.lumaCount;
    decodedFrameOut.blockDecode = false;
}

void FAVVReader::ReadFrameTextureLumaBC4(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut)
{
    uint32_t dataPos = 0;

    AVV_READ(decodedFrameOut.blockCount, frameData, dataPos, uint32_t, 1);
    decodedFrameOut.lumaDataOffset = readPos + dataPos;
    decodedFrameOut.lumaDataSize = decodedFrameOut.blockCount * 8;
    decodedFrameOut.lumaCount = decodedFrameOut.blockCount * 16;
    decodedFrameOut.blockDecode = true;
}

void FAVVReader::ReadFrameColorsRGB565(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut)
{
    uint32_t dataPos = 0;

    AVV_READ(decodedFrameOut.colorCount, frameData, dataPos, uint32_t, 1);
    decodedFrameOut.normalCount = 0;
    decodedFrameOut.colorDataOffset = readPos + dataPos;
    decodedFrameOut.colorDataSize = decodedFrameOut.colorCount * 2;
}

void FAVVReader::ReadFrameColorsRGB565NormalsOct16(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut)
{
    uint32_t dataPos = 0;

    AVV_READ(decodedFrameOut.colorCount, frameData, dataPos, uint32_t, 1);
    decodedFrameOut.normalCount = decodedFrameOut.colorCount;
    decodedFrameOut.colorDataOffset = readPos + dataPos;
    decodedFrameOut.colorDataSize = decodedFrameOut.colorCount * 4;
}

int FAVVReader::GetSegmentIndex(int frameNumber)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_GetSegmentAndFrame);

    if (frameNumber < 0 || frameNumber >= FrameToSegment.size())
    {
        return -1;
    }

    return FrameToSegment[frameNumber];
}

bool FAVVReader::DecodeMetaSkeleton(UAVVFile* avvFile, AVVSkeleton* targetSkeleton)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_DecodeMetaSkeleton);

    FStreamableAVVData& streamableData = (FStreamableAVVData&)avvFile->GetStreamableData();
    uint8_t* data = streamableData.ReadMetaData();
    size_t readPos = 0;

    uint32_t metaContainerCount;
    uint32_t containerType;
    uint32_t containerSize;

    AVV_READ(metaContainerCount, data, readPos, uint32_t, 1);
    for (uint32_t i = 0; i < metaContainerCount; ++i)
    {
        AVV_READ(containerType, data, readPos, uint32_t, 1);
        AVV_READ(containerSize, data, readPos, uint32_t, 1);

        if (containerType == AVV_META_SKELETON)
        {
            AVV_READ(targetSkeleton->skeletonIndex, data, readPos, uint32_t, 1);
            AVV_READ(targetSkeleton->boneCount, data, readPos, uint32_t, 1);

            targetSkeleton->boneInfo.resize(targetSkeleton->boneCount);
            for (uint32 b = 0; b < targetSkeleton->boneCount; ++b)
            {
                AVV_READ(targetSkeleton->boneInfo[b].parentIndex, data, readPos, int, 1);
                AVV_READ(targetSkeleton->boneInfo[b].name, data, readPos, char, 32);
            }

            DecodeSkeletonPosRotations(data + readPos, targetSkeleton->boneCount, *targetSkeleton);

            return true;
        }
        else
        {
            readPos += containerSize;
        }
    }
    return false;
}

void DecodeSkeletonPosRotations(uint8_t* dataPtr, uint32_t boneCount, AVVSkeleton& skeletonOut)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVReader_DecodeSkeletonPosRotations);

    size_t dataPos = 0;

    float aabbMin[3];
    float aabbMax[3];

    AVV_READ(aabbMin, dataPtr, dataPos, float, 3);
    AVV_READ(aabbMax, dataPtr, dataPos, float, 3);
#if ENGINE_MAJOR_VERSION == 5
    FVector3f aabb_min = FVector3f(aabbMin[0], aabbMin[1], aabbMin[2]);
    FVector3f aabb_max = FVector3f(aabbMax[0], aabbMax[1], aabbMax[2]);
#else
    FVector aabb_min = FVector(aabbMin[0], aabbMin[1], aabbMin[2]);
    FVector aabb_max = FVector(aabbMax[0], aabbMax[1], aabbMax[2]);
#endif

    // 128 bit (16 byte) pos and rotation
    skeletonOut.positions.resize(boneCount);
    skeletonOut.rotations.resize(boneCount);

    for (uint32 b = 0; b < boneCount; ++b)
    {
        uint64_t packed0;
        uint64_t packed1;
        AVV_READ(packed0, dataPtr, dataPos, uint64_t, 1);
        AVV_READ(packed1, dataPtr, dataPos, uint64_t, 1);

        PosQuat128 encoded;
        encoded.unpack(packed0, packed1);

        // Note: unreal unit conversion and y/z swap is performed afterwards when updating the SkeletalMeshActor.
        skeletonOut.positions[b].X = decodeFloat16(encoded.posX, aabb_min.X, aabb_max.X);
        skeletonOut.positions[b].Y = decodeFloat16(encoded.posY, aabb_min.Y, aabb_max.Y);
        skeletonOut.positions[b].Z = decodeFloat16(encoded.posZ, aabb_min.Z, aabb_max.Z);
        skeletonOut.rotations[b].X = decodeFloat20(encoded.quatX, -1.0f, 1.0f);
        skeletonOut.rotations[b].Y = decodeFloat20(encoded.quatY, -1.0f, 1.0f);
        skeletonOut.rotations[b].Z = decodeFloat20(encoded.quatZ, -1.0f, 1.0f);
        skeletonOut.rotations[b].W = decodeFloat20(encoded.quatW, -1.0f, 1.0f);
    }
}