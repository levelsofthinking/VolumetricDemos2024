// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/CircularBuffer.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Runtime/Launch/Resources/Version.h"
#include <unordered_map>

#include "HoloMeshModule.h"

#if (ENGINE_MAJOR_VERSION == 5)
typedef FBufferRHIRef FHoloMeshBufferRHIRef;
typedef FBufferRHIRef FHoloMeshVertexBufferRHIRef;
typedef FBufferRHIRef FHoloMeshIndexBufferRHIRef;
typedef FVector2f FHoloMeshVec2;
typedef FVector3f FHoloMeshVec3;
typedef FVector4f FHoloMeshVec4;
#else
typedef FVertexBufferRHIRef FHoloMeshBufferRHIRef;
typedef FVertexBufferRHIRef FHoloMeshVertexBufferRHIRef;
typedef FIndexBufferRHIRef  FHoloMeshIndexBufferRHIRef;
typedef FVector2D FHoloMeshVec2;
typedef FVector   FHoloMeshVec3;
typedef FVector4  FHoloMeshVec4;
#endif

// Matches the definition of FRDGBufferInitialDataFreeCallback which is only availabe in 5.0+
using FHoloUploadCompleteCallback = TFunction<void(const void* InData)>;

// -- Utility Functions --
class HOLOMESH_API HoloMeshUtilities
{
public:
    // Used for uploading data into a compute buffer.
    // This function primarily exists to resolve differences between 4.27 and 5.0+
    static void UploadBuffer(FRDGBuilder& GraphBuilder, FRDGBufferRef Buffer, void* DataPtr, uint32_t SizeInBytes, ERDGInitialDataFlags initialDataFlags);
    static void UploadBuffer(FRDGBuilder& GraphBuilder, FRDGBufferRef Buffer, void* DataPtr, uint32_t SizeInBytes, FHoloUploadCompleteCallback&& UploadCompleteCallback);

    static bool UploadVertexBuffer(FHoloMeshVertexBufferRHIRef BufferRHI, const void* Data, uint32 SizeInBytes, FRHICommandListImmediate* RHICmdList = nullptr);
    static bool UploadIndexBuffer(FHoloMeshIndexBufferRHIRef BufferRHI, const void* Data, uint32 SizeInBytes, FRHICommandListImmediate* RHICmdList = nullptr);

    // ConvertToExternalBuffer but works on both 4.27 and 5.0+
    static void ConvertToPooledBuffer(FRDGBuilder& GraphBuilder, FRDGBufferRef Buffer, TRefCountPtr<FRDGPooledBuffer>& OutPooledBuffer);

    static void ClearUAVUInt(FRDGBuilder& GraphBuilder, FUnorderedAccessViewRHIRef ClearTarget);
    static void ClearUAVFloat(FRDGBuilder& GraphBuilder, FUnorderedAccessViewRHIRef ClearTarget);

    // Copies an RDG texture to a non-RDG one.
    static void CopyTexture(FRDGBuilder& GraphBuilder, FIntVector Size, FRDGTextureRef SourceRDGTexture, int SourceMip, FTexture2DRHIRef DestTexture, int DestMip);
    static void CopyTexture(FRDGBuilder& GraphBuilder, FIntVector Size, FRDGTextureRef SourceRDGTexture, FIntVector SourcePosition, FRDGTextureRef DestRDGTexture, FTexture2DRHIRef DestTexture, FIntVector DestPosition);
};

// -- Priority Queue --
template <typename InElementType>
struct TPriorityQueueNode {
    InElementType Element;
    float Priority;

    TPriorityQueueNode()
    {
    }

    TPriorityQueueNode(InElementType InElement, float InPriority)
    {
        Element = InElement;
        Priority = InPriority;
    }

    bool operator<(const TPriorityQueueNode<InElementType> Other) const
    {
        // Note: changed so higher number = higher priority.
        return Priority > Other.Priority;
    }
};

template <typename InElementType>
class TPriorityQueue {
public:
    TPriorityQueue()
    {
        Array.Heapify();
    }

public:
    // Always check if IsEmpty() before Pop-ing!
    InElementType Pop()
    {
        TPriorityQueueNode<InElementType> Node;
        Array.HeapPop(Node);
        return Node.Element;
    }

    TPriorityQueueNode<InElementType> PopNode()
    {
        TPriorityQueueNode<InElementType> Node;
        Array.HeapPop(Node);
        return Node;
    }

    void Push(InElementType Element, float Priority)
    {
        Array.HeapPush(TPriorityQueueNode<InElementType>(Element, Priority));
    }

    bool IsEmpty() const
    {
        return Array.Num() == 0;
    }

    int Num() const
    {
        return Array.Num();
    }

private:
    TArray<TPriorityQueueNode<InElementType>> Array;
};

// -- Moving Average Calculator --
template <typename InElementType, unsigned int Period>
class TMovingAverage
{
public:
    TMovingAverage()
        : buffer(TCircularBuffer<InElementType>(Period))
    {
    }

    void Add(InElementType value)
    {
        lastIndex = buffer.GetNextIndex(lastIndex);
        buffer[lastIndex] = value;
    }

    InElementType GetAverage()
    {
        InElementType avg = (InElementType)0;
        for (int i = 0; i < Period; ++i)
        {
            avg += buffer[i];
        }
        return avg / Period;
    }

    InElementType GetMin()
    {
        InElementType min = (InElementType)0;
        bool firstElement = true;
        for (int i = 0; i < Period; ++i)
        {
            if (firstElement)
            {
                min = buffer[i];
                firstElement = false;
            }
            else 
            {
                min = FMath::Min(min, buffer[i]);
            }
        }
        return min;
    }

    InElementType GetMax()
    {
        InElementType max = (InElementType)0;
        bool firstElement = true;
        for (int i = 0; i < Period; ++i)
        {
            if (firstElement)
            {
                max = buffer[i];
                firstElement = false;
            }
            else
            {
                max = FMath::Max(max, buffer[i]);
            }
        }
        return max;
    }

protected:
    uint32 lastIndex = 0;
    TCircularBuffer<InElementType> buffer;
};

// -- Memory Block/Memory Pool --
constexpr int32 GHoloMemoryBlockSize = 256 * 1024; // 256KB

struct FHoloMemoryBlock 
{
    static std::atomic<SIZE_T> TotalAllocatedBytes;

    uint8_t* Data;
    SIZE_T Size;

    FHoloMemoryBlock()
        : Data(nullptr), Size(0) {}

    FHoloMemoryBlock(SIZE_T InSize)
    {
        Size = ((InSize + GHoloMemoryBlockSize - 1) / GHoloMemoryBlockSize) * GHoloMemoryBlockSize;
        Data = (uint8_t*)FPlatformMemory::BinnedAllocFromOS(Size);
        TotalAllocatedBytes += Size;
    }

    ~FHoloMemoryBlock()
    {
        if (Data)
        {
            UE_LOG(LogHoloMesh, Error, TEXT("Memory Block Leaked!"));
            Free();
        }
    }

    void Free() 
    {
        if (Data) 
        {
            FPlatformMemory::BinnedFreeToOS(Data, Size);
            TotalAllocatedBytes -= Size;
            Data = nullptr;
            Size = 0;
        }
    }
};
typedef TSharedPtr<FHoloMemoryBlock, ESPMode::ThreadSafe> FHoloMemoryBlockRef;

// Rounds requested allocation size to nearest 256kb block size.
// Stores a list of free blocks for each size. 
class FHoloMemoryPool 
{
private:
    // <Block Size, List of Free Blocks>
    TMap<SIZE_T, TArray<FHoloMemoryBlockRef>> FreeBlockMap;
    mutable FCriticalSection BlockMapMutex;
    std::unordered_map<SIZE_T, SIZE_T> BlockUsage;
    mutable FCriticalSection UsageMutex;

    std::atomic<SIZE_T> TotalAllocatedBytes = { 0 };
    std::atomic<SIZE_T> TotalUtilizedBytes = { 0 };

public:
    FHoloMemoryBlockRef Allocate(SIZE_T Size)
    {
        FScopeLock Lock(&BlockMapMutex);

        SIZE_T RoundedUpSize = ((Size + GHoloMemoryBlockSize - 1) / GHoloMemoryBlockSize) * GHoloMemoryBlockSize;
        TArray<FHoloMemoryBlockRef>& BlockList = FreeBlockMap.FindOrAdd(RoundedUpSize);

        UsageMutex.Lock();
        BlockUsage[RoundedUpSize]++;
        UsageMutex.Unlock();

        if (BlockList.Num() == 0) 
        {
            FHoloMemoryBlockRef NewBlock = MakeShared<FHoloMemoryBlock, ESPMode::ThreadSafe>(RoundedUpSize);
            BlockList.Add(NewBlock);
            TotalAllocatedBytes += RoundedUpSize;
        }

        FHoloMemoryBlockRef Block = BlockList.Last();
        BlockList.RemoveAt(BlockList.Num() - 1);
        TotalUtilizedBytes += Block->Size;
        return Block;
    }

    void Deallocate(FHoloMemoryBlockRef Block)
    {
        FScopeLock Lock(&BlockMapMutex);

        SIZE_T RoundedUpSize = Block->Size;
        TArray<FHoloMemoryBlockRef>& BlockList = FreeBlockMap.FindOrAdd(RoundedUpSize);
        BlockList.Add(Block);
        TotalUtilizedBytes -= Block->Size;
    }

    void Preallocate(SIZE_T Size, int32 Count)
    {
        FScopeLock Lock(&BlockMapMutex);

        SIZE_T RoundedUpSize = ((Size + GHoloMemoryBlockSize - 1) / GHoloMemoryBlockSize) * GHoloMemoryBlockSize;
        TArray<FHoloMemoryBlockRef>& BlockList = FreeBlockMap.FindOrAdd(RoundedUpSize);

        for (int32 i = 0; i < Count; i++) 
        {
            FHoloMemoryBlockRef NewBlock = MakeShared<FHoloMemoryBlock, ESPMode::ThreadSafe>(RoundedUpSize);
            BlockList.Add(NewBlock);

            TotalAllocatedBytes += RoundedUpSize;
        }
    }

    TArray<std::pair<SIZE_T, uint32_t>> PeekPoolContents()
    {
        FScopeLock Lock(&BlockMapMutex);

        TArray<std::pair<SIZE_T, uint32_t>> Results;
        for (auto& item : FreeBlockMap)
        {
            Results.Add({ item.Key, item.Value.Num() });
        }

        return Results;
    }

    // Can be called periodically to downsize the pool based on usage statistics.
    // Currently only applied when pool is under 50% utilization.
    void CleanUp()
    {
        if (TotalAllocatedBytes.load() == 0)
        {
            return;
        }

        // Copy block usage to analyze it.
        UsageMutex.Lock();
        std::unordered_map<SIZE_T, SIZE_T> BlockUsageCopy = BlockUsage;
        BlockUsage.clear();
        UsageMutex.Unlock();

        float poolUtilization = (float)TotalUtilizedBytes.load() / (float)TotalAllocatedBytes.load();
        if (poolUtilization < 0.5)
        {
            BlockMapMutex.Lock();
            TMap<SIZE_T, TArray<FHoloMemoryBlockRef>> FreeBlockMapCopy = FreeBlockMap;
            BlockMapMutex.Unlock();

            TArray<SIZE_T> SizesToFreeFrom;
            for (auto& item : FreeBlockMapCopy)
            {
                if (item.Value.Num() == 0)
                {
                    continue;
                }

                // This block size has excessive blocks if it has more than are being used per cycle.
                if (item.Value.Num() > BlockUsageCopy[item.Key])
                {
                    SizesToFreeFrom.Add(item.Key);
                }
            }

            SIZE_T TotalFreed = 0;
            if (SizesToFreeFrom.Num() > 0)
            {
                FScopeLock Lock(&BlockMapMutex);
                for (auto& Size : SizesToFreeFrom)
                {
                    TArray<FHoloMemoryBlockRef>& BlockList = FreeBlockMap.FindOrAdd(Size);
                    FHoloMemoryBlockRef Block = BlockList.Pop();
                    TotalFreed += Block->Size;
                    Block->Free();
                }
            }
            TotalAllocatedBytes -= TotalFreed;
        }
    }

    // Empty the pool and free all the blocks.
    void Empty()
    {
        FScopeLock Lock(&BlockMapMutex);
        for (auto& item : FreeBlockMap)
        {
            for (auto& block : item.Value)
            {
                TotalAllocatedBytes -= block->Size;
                block->Free();
            }
        }
        TotalAllocatedBytes = 0;
        FreeBlockMap.Empty();
    }
};

// Same as FScopeLock but used for an already locked criticial section.
// Only performs unlocking on destruction.
class FScopeLockHold
{
public:

    FScopeLockHold(FCriticalSection* InSynchObject)
        : SynchObject(InSynchObject)
    {
        check(SynchObject);
    }

    /** Destructor that performs a release on the synchronization object. */
    ~FScopeLockHold()
    {
        if (SynchObject)
        {
            SynchObject->Unlock();
            SynchObject = nullptr;
        }
    }

private:

    /** Default constructor (hidden on purpose). */
    FScopeLockHold();

    /** Copy constructor( hidden on purpose). */
    FScopeLockHold(const FScopeLock& InScopeLock);

    /** Assignment operator (hidden on purpose). */
    FScopeLockHold& operator=(FScopeLockHold& InScopeLock)
    {
        return *this;
    }

private:

    // Holds the synchronization object to aggregate and scope manage.
    FCriticalSection* SynchObject;
};

// A thread-safe object pool with a fixed size. It preallocates a contiguous block 
// of data for efficient access. Only initializes/destroys objects during pool 
// creation/deletion. Provides lock-free methods for acquiring and releasing objects.
template <typename T, uint32 Capacity>
class TReusableObjectPool
{
public:
    TReusableObjectPool()
    {
        // Allocate a contiguous block for all data and then populate the pool.
        Data = (T*)FMemory::Malloc(sizeof(T) * Capacity);
        for (uint32 i = 0; i < Capacity; ++i)
        {
            new (&Data[i]) T(); // Call constructor
            Pool.Push(&Data[i]);
        }
    }

    ~TReusableObjectPool()
    {
        for (uint32 i = 0; i < Capacity; ++i)
        {
            Data[i].~T(); // Call destructor
        }
        FMemory::Free(Data);
    }

    // Will return nullptr if pool is empty.
    T* Next()
    {
        return Pool.Pop();
    }

    void Return(T* ObjPtr)
    {
        Pool.Push(ObjPtr);
    }

private:
    TLockFreePointerListLIFO<T> Pool;
    T* Data;
};