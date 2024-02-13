// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <deque>

#include "AVVFile.h"
#include "AVVFormat.h"
#include "HoloSuitePlayerModule.h"

inline float clamp(float n, float lower, float upper)
{
    return std::max(lower, std::min(n, upper));
}

inline float decodeFloat8(uint16_t x, float boundsMin, float boundsMax)
{
    float zeroOne = (float)x / 255.0f;
    return (zeroOne * (boundsMax - boundsMin)) + boundsMin;
}

inline float decodeFloat10(uint16_t x, float boundsMin, float boundsMax)
{
    float zeroOne = (float)x / 1023.0f;
    return (zeroOne * (boundsMax - boundsMin)) + boundsMin;
}

inline float decodeFloat12(uint16_t x, float boundsMin, float boundsMax)
{
    float zeroOne = (float)x / 4095.0f;
    return (zeroOne * (boundsMax - boundsMin)) + boundsMin;
}

inline float decodeFloat16(uint16_t x, float boundsMin, float boundsMax)
{
    float zeroOne = (float)x / 65535.0f;
    return (zeroOne * (boundsMax - boundsMin)) + boundsMin;
}

inline float decodeFloat20(uint32_t x, float boundsMin, float boundsMax)
{
    float zeroOne = (float)x / 1048575.0f;
    return (zeroOne * (boundsMax - boundsMin)) + boundsMin;
}

struct FAVVReaderRequest
{
    ~FAVVReaderRequest()
    {
        if (segment != nullptr)
        {
            segment->Release();
            segment = nullptr;
        }
        if (frame != nullptr)
        {
            frame->Release();
            frame = nullptr;
        }
    }

    int segmentIndex = -1;
    int frameNumber = -1;
    bool requestedTexture = false;

    AVVEncodedSegment* segment = nullptr;
    AVVEncodedFrame* frame = nullptr;

    std::atomic<int> pendingIORequestCount = { 0 };
    TArray<FAVVIORequestRef> IORequests;
};
typedef TSharedPtr<FAVVReaderRequest, ESPMode::ThreadSafe> FAVVReaderRequestRef;

class HOLOSUITEPLAYER_API FAVVReader
{
public:
    enum class EAVVReaderState
    {
        None,
        Ready,
        WaitingIO,
        Finished,
        Error
    };

    FAVVReader();
    ~FAVVReader();

    bool Open(UAVVFile* avvFile);
    void Close();

    // Processes the next available request. This is a thread safe operation and is in intended
    // to be frequently called from anywhere that wants to tick the reader.
    void Update();

    // Request for a segment and/or frame. Will be available through GetNextFinishedRequest().
    bool AddRequest(int requestSegmentIndex = -1, int requestFrameIndex = -1, bool requestTexture = true, bool blockingRequest = false);

    // Returns the next completed request in a FIFO order.
    FAVVReaderRequestRef GetFinishedRequest();

    bool HasQueuedRequests() { return !pendingRequests.IsEmpty(); }

    // Returns a segment index for a given frame number.
    int GetSegmentIndex(int frameNumber);

    uint32_t Version;
    FString VersionString;
    int FrameCount;
    int SegmentCount;
    AVVLimits Limits;
    AVVSkeleton MetaSkeleton;
    std::vector<AVVSegmentTableEntry> SegmentTable;
    std::vector<int> FrameToSegment;

    static bool DecodeMetaSkeleton(UAVVFile* avvFile, AVVSkeleton* targetSkeleton);

protected:

    UAVVFile* openFile;
    uint32_t metaContainerCount;
    uint32_t segmentContainerCount;
    
    size_t segmentContainersStartByte;

    std::vector<int> sequenceLookupTable;
    std::vector<int> sequenceStartFrames;

    // Profiling
    double asyncReadStart;

    // State
    mutable FCriticalSection CriticalSection;
    std::atomic<EAVVReaderState> readerState;
    TQueue<FAVVReaderRequestRef> pendingRequests;
    TQueue<FAVVReaderRequestRef> waitingRequests;
    TQueue<FAVVReaderRequestRef> finishedRequests;
    TSet<int> activeFrameNumbers;

    // Read the current pending segment. This comes after the IO request has been fufilled. 
    void PrepareSegment(AVVEncodedSegment* segment);
    void PrepareFrame(AVVEncodedFrame* frame);
    void PrepareFrameTexture(AVVEncodedFrame* frame);

    void ReadFrameAnimMat4x4(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut);
    void ReadFrameAnimPosRotation128(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut);
    void ReadFrameAnimDeltaPos32(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut);
    void ReadFrameTextureLuma8(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut);
    void ReadFrameTextureLumaBC4(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut);
    void ReadFrameColorsRGB565(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut);
    void ReadFrameColorsRGB565NormalsOct16(uint8_t* frameData, size_t readPos, AVVEncodedFrame& decodedFrameOut);
};