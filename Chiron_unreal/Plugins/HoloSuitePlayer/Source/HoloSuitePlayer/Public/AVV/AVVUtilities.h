// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "AVVReader.h"
#include "AVVShaders.h"
#include "HoloMeshManager.h"
#include "HoloMeshUtilities.h"
#include "HoloMeshComponent.h"
#include "HoloSuitePlayerModule.h"

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderGraphResources.h"
#include "Runtime/Engine/Classes/Engine/TextureRenderTarget2D.h"
#include "ClearQuad.h"
#include "RenderResource.h"
#include "RenderGraphUtils.h"
#include "Stats/Stats.h"

#include <deque>

#include "AVVUtilities.generated.h"

DECLARE_CYCLE_STAT(TEXT("FAVVDataCache.HasSegment"), STAT_AVVDataCache_HasSegment, STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("FAVVDataCache.HasFrame"), STAT_AVVDataCache_HasFrame, STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("FAVVDataCache.GetSegment"), STAT_AVVDataCache_GetSegment, STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("FAVVDataCache.GetFrame"), STAT_AVVDataCache_GetFrame, STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("FAVVDataCache.GetSegmentAndFrame"), STAT_AVVDataCache_GetSegmentAndFrame, STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("FAVVDataCache.FreeStaleData"), STAT_AVVDataCache_FreeStaleData, STATGROUP_HoloSuitePlayer);

// DataCache holds segments and frames which are ready for GPU decoding.
// It will hold them until FreeStaleData is called.
// If any of the elements have active uploads they will not be freed.

USTRUCT()
struct FAVVDataCache
{
    GENERATED_BODY()

    FCriticalSection* CriticalSection;
    TArray<AVVEncodedSegment*> SegmentArray;
    TArray<AVVEncodedFrame*> FrameArray;
    
    FAVVDataCache()
    {
        CriticalSection = new FCriticalSection();
    }

    void AddSegment(AVVEncodedSegment* segment)
    {
        if (segment == nullptr)
        {
            return;
        }

        FScopeLock Lock(CriticalSection);
        SegmentArray.Add(segment);
    }

    void AddFrame(AVVEncodedFrame* frame)
    {
        if (frame == nullptr)
        {
            return;
        }

        FScopeLock Lock(CriticalSection);
        FrameArray.Add(frame);
    }

    bool HasSegment(int index)
    {
        SCOPE_CYCLE_COUNTER(STAT_AVVDataCache_HasSegment);

        FScopeLock Lock(CriticalSection);
        for (int i = 0; i < SegmentArray.Num(); ++i)
        {
            if (SegmentArray[i]->segmentIndex == index)
            {
                return true;
            }
        }
        return false;
    }

    bool HasFrame(int index)
    {
        SCOPE_CYCLE_COUNTER(STAT_AVVDataCache_HasFrame);

        FScopeLock Lock(CriticalSection);
        for (int i = 0; i < FrameArray.Num(); ++i)
        {
            if (FrameArray[i]->frameIndex == index)
            {
                return true;
            }
        }
        return false;
    }

    AVVEncodedSegment* GetSegment(int index)
    {
        SCOPE_CYCLE_COUNTER(STAT_AVVDataCache_GetSegment);

        FScopeLock Lock(CriticalSection);
        for (int i = 0; i < SegmentArray.Num(); ++i)
        {
            if (SegmentArray[i]->segmentIndex == index)
            {
                return SegmentArray[i];
            }
        }
        return nullptr;
    }

    AVVEncodedFrame* GetFrame(int index)
    {
        SCOPE_CYCLE_COUNTER(STAT_AVVDataCache_GetFrame);

        FScopeLock Lock(CriticalSection);
        for (int i = 0; i < FrameArray.Num(); ++i)
        {
            if (FrameArray[i]->frameIndex == index)
            {
                return FrameArray[i];
            }
        }
        return nullptr;
    }

    bool GetSegmentAndFrame(int segmentIndex, int frameIndex, AVVEncodedSegment** segmentOut, AVVEncodedFrame** frameOut)
    {
        SCOPE_CYCLE_COUNTER(STAT_AVVDataCache_GetSegmentAndFrame);

        FScopeLock Lock(CriticalSection);
        for (int i = 0; i < SegmentArray.Num(); ++i)
        {
            if (SegmentArray[i]->segmentIndex == segmentIndex)
            {
                *segmentOut = SegmentArray[i];
                break;
            }
        }
        for (int i = 0; i < FrameArray.Num(); ++i)
        {
            if (FrameArray[i]->frameIndex == frameIndex)
            {
                *frameOut = FrameArray[i];
                break;
            }
        }
        return (*segmentOut != nullptr && *frameOut != nullptr);
    }

    void Empty()
    {
        FScopeLock Lock(CriticalSection);
        for (int i = 0; i < SegmentArray.Num(); ++i)
        {
            SegmentArray[i]->Release();
        }
        SegmentArray.Empty();

        for (int i = 0; i < FrameArray.Num(); ++i)
        {
            FrameArray[i]->Release();
        }
        FrameArray.Empty();
    }

    // Frees stale data that comes before the provided segment and frame indexes.
    // Will also free data which is too far ahead to be useful.
    // If reverse is true the function will evaluate the opposite directions for reverse playback caching.
    void FreeStaleData(int staleBeforeSegment, int staleBeforeFrame, bool reverse = false)
    {
        SCOPE_CYCLE_COUNTER(STAT_AVVDataCache_FreeStaleData);

        FScopeLock Lock(CriticalSection);

        // Cap the number of frames/segments ahead we can get. This prevents low framerate playback
        // from growing the cache and then looping back to frame 0 and leaving stale frames behind.
        const int MaxSegmentsAhead = 2;
        const int MaxFramesAhead = 3;

        TArray<AVVEncodedSegment*> SegmentsToKeep;
        for (AVVEncodedSegment* Segment : SegmentArray)
        {
            bool isBehind = reverse ? (int)Segment->segmentIndex > staleBeforeSegment : (int)Segment->segmentIndex < staleBeforeSegment;
            bool isAhead = reverse ? (int)Segment->segmentIndex < (staleBeforeSegment - MaxSegmentsAhead) : (int)Segment->segmentIndex > (staleBeforeSegment + MaxSegmentsAhead);
            if ((isBehind || isAhead || Segment->processed) && Segment->activeUploadCount.load() == 0)
            {
                Segment->Release();
                delete Segment;
                continue;
            }

            SegmentsToKeep.Add(Segment);
        }
        SegmentArray = SegmentsToKeep;

        TArray<AVVEncodedFrame*> FramesToKeep;
        for (AVVEncodedFrame* Frame : FrameArray)
        {
            bool isBehind = reverse ? (int)Frame->frameIndex > staleBeforeFrame : (int)Frame->frameIndex < staleBeforeFrame;
            bool isAhead = reverse ? (int)Frame->frameIndex < (staleBeforeFrame - MaxFramesAhead) : (int)Frame->frameIndex > (staleBeforeFrame + MaxFramesAhead);
            if ((isBehind || isAhead || Frame->processed) && Frame->activeUploadCount.load() == 0)
            {
                Frame->Release();
                delete Frame;
                continue;
            }

            FramesToKeep.Add(Frame);
        }
        FrameArray = FramesToKeep;
    }
};