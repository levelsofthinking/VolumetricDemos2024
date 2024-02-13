// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloMeshUtilities.h"
#include "HoloMeshManager.h"

#if (ENGINE_MAJOR_VERSION < 5)
BEGIN_SHADER_PARAMETER_STRUCT(FUploadBufferParameters, )
	SHADER_PARAMETER_RDG_BUFFER_UPLOAD(UploadBuffer)
END_SHADER_PARAMETER_STRUCT()
#endif

BEGIN_SHADER_PARAMETER_STRUCT(FClearTargetParameters, )
	SHADER_PARAMETER_UAV(RWTexture2D<float2>, ClearTarget)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FCopyTextureParameters, )
	RDG_TEXTURE_ACCESS(Input, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

#if (ENGINE_MAJOR_VERSION == 5)
#define LOCK_VERT_BUFFER LockBuffer
#define UNLOCK_VERT_BUFFER UnlockBuffer
#define LOCK_INDEX_BUFFER LockBuffer
#define UNLOCK_INDEX_BUFFER UnlockBuffer
#define RHI_LOCK_VERT_BUFFER RHILockBuffer
#define RHI_UNLOCK_VERT_BUFFER RHIUnlockBuffer
#define RHI_LOCK_INDEX_BUFFER RHILockBuffer
#define RHI_UNLOCK_INDEX_BUFFER RHIUnlockBuffer
#else
#define LOCK_VERT_BUFFER LockVertexBuffer
#define UNLOCK_VERT_BUFFER UnlockVertexBuffer
#define LOCK_INDEX_BUFFER LockIndexBuffer
#define UNLOCK_INDEX_BUFFER UnlockIndexBuffer
#define RHI_LOCK_VERT_BUFFER RHILockVertexBuffer
#define RHI_UNLOCK_VERT_BUFFER RHIUnlockVertexBuffer
#define RHI_LOCK_INDEX_BUFFER RHILockIndexBuffer
#define RHI_UNLOCK_INDEX_BUFFER RHIUnlockIndexBuffer
#endif

std::atomic<SIZE_T> FHoloMemoryBlock::TotalAllocatedBytes = { 0 };

void HoloMeshUtilities::UploadBuffer(FRDGBuilder& GraphBuilder, FRDGBufferRef Buffer, void* DataPtr, uint32_t SizeInBytes, ERDGInitialDataFlags initialDataFlags)
{
	if (SizeInBytes == 0)
	{
		return;
	}

#if (ENGINE_MAJOR_VERSION >= 5)
    GraphBuilder.QueueBufferUpload(Buffer, DataPtr, SizeInBytes, initialDataFlags);
#else
	FUploadBufferParameters* UploadParameters = GraphBuilder.AllocParameters<FUploadBufferParameters>();
	UploadParameters->UploadBuffer = Buffer;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HoloMesh.UploadBuffer"),
		UploadParameters,
		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
		[UploadParameters, DataPtr, SizeInBytes](FRHICommandListImmediate& RHICmdList)
		{
			void* Dest = RHILockVertexBuffer(UploadParameters->UploadBuffer->GetRHIVertexBuffer(), 0, SizeInBytes, RLM_WriteOnly);
			FPlatformMemory::Memcpy(Dest, DataPtr, SizeInBytes);
			RHIUnlockVertexBuffer(UploadParameters->UploadBuffer->GetRHIVertexBuffer());
		});
#endif

	GHoloMeshManager.AddUploadBytes(SizeInBytes);
}

void HoloMeshUtilities::UploadBuffer(FRDGBuilder& GraphBuilder, FRDGBufferRef Buffer, void* DataPtr, uint32_t SizeInBytes, FHoloUploadCompleteCallback&& UploadCompleteCallback)
{
	if (SizeInBytes == 0)
	{
		return;
	}

#if (ENGINE_MAJOR_VERSION >= 5)
	GraphBuilder.QueueBufferUpload(Buffer, DataPtr, SizeInBytes, MoveTemp(UploadCompleteCallback));
#else
	FUploadBufferParameters* UploadParameters = GraphBuilder.AllocParameters<FUploadBufferParameters>();
	UploadParameters->UploadBuffer = Buffer;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HoloMesh.UploadBuffer"),
		UploadParameters,
		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
		[UploadParameters, UploadCompleteCallback, DataPtr, SizeInBytes](FRHICommandListImmediate& RHICmdList)
		{
			void* Dest = RHILockVertexBuffer(UploadParameters->UploadBuffer->GetRHIVertexBuffer(), 0, SizeInBytes, RLM_WriteOnly);
			FPlatformMemory::Memcpy(Dest, DataPtr, SizeInBytes);
			RHIUnlockVertexBuffer(UploadParameters->UploadBuffer->GetRHIVertexBuffer());
			UploadCompleteCallback(DataPtr);
		});
#endif

	GHoloMeshManager.AddUploadBytes(SizeInBytes);
}

bool HoloMeshUtilities::UploadVertexBuffer(FHoloMeshVertexBufferRHIRef BufferRHI, const void* Data, uint32 SizeInBytes, FRHICommandListImmediate* RHICmdList)
{
	void* Buffer = nullptr;

	if (RHICmdList != nullptr)
	{
		Buffer = RHICmdList->LOCK_VERT_BUFFER(BufferRHI, 0, SizeInBytes, RLM_WriteOnly);
	}
	else
	{
		Buffer = RHI_LOCK_VERT_BUFFER(BufferRHI, 0, SizeInBytes, RLM_WriteOnly);
	}

	if (Buffer != nullptr)
	{
		FMemory::Memcpy(Buffer, Data, SizeInBytes);
	}

	if (RHICmdList != nullptr)
	{
		RHICmdList->UNLOCK_VERT_BUFFER(BufferRHI);
	}
	else
	{
		RHI_UNLOCK_VERT_BUFFER(BufferRHI);
	}

	return true;
}

bool HoloMeshUtilities::UploadIndexBuffer(FHoloMeshIndexBufferRHIRef BufferRHI, const void* Data, uint32 SizeInBytes, FRHICommandListImmediate* RHICmdList)
{
	void* Buffer = nullptr;

	if (RHICmdList != nullptr)
	{
		Buffer = RHICmdList->LOCK_INDEX_BUFFER(BufferRHI, 0, SizeInBytes, RLM_WriteOnly);
	}
	else
	{
		Buffer = RHI_LOCK_INDEX_BUFFER(BufferRHI, 0, SizeInBytes, RLM_WriteOnly);
	}

	if (Buffer != nullptr)
	{
		FMemory::Memcpy(Buffer, Data, SizeInBytes);
	}

	if (RHICmdList != nullptr)
	{
		RHICmdList->UNLOCK_INDEX_BUFFER(BufferRHI);
	}
	else
	{
		RHI_UNLOCK_INDEX_BUFFER(BufferRHI);
	}

	return true;
}

void HoloMeshUtilities::ConvertToPooledBuffer(FRDGBuilder& GraphBuilder, FRDGBufferRef Buffer, TRefCountPtr<FRDGPooledBuffer>& OutPooledBuffer)
{
#if ENGINE_MAJOR_VERSION == 5
	OutPooledBuffer = GraphBuilder.ConvertToExternalBuffer(Buffer);
#else
	ConvertToExternalBuffer(GraphBuilder, Buffer, OutPooledBuffer);
#endif
}

void HoloMeshUtilities::ClearUAVUInt(FRDGBuilder& GraphBuilder, FUnorderedAccessViewRHIRef ClearTarget)
{
	FClearTargetParameters* ClearParameters = GraphBuilder.AllocParameters<FClearTargetParameters>();
	ClearParameters->ClearTarget = ClearTarget;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HoloMesh.ClearUAV"),
		ClearParameters,
		ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
		[ClearParameters](FRHICommandListImmediate& RHICmdList)
		{
			const FUintVector4 clearColor(0, 0, 0, 0);
			RHICmdList.Transition(FRHITransitionInfo(ClearParameters->ClearTarget, ERHIAccess::SRVGraphics, ERHIAccess::UAVCompute));
			RHICmdList.ClearUAVUint(ClearParameters->ClearTarget, clearColor);
			RHICmdList.Transition(FRHITransitionInfo(ClearParameters->ClearTarget, ERHIAccess::UAVCompute, ERHIAccess::SRVGraphics));
		});
}

void HoloMeshUtilities::ClearUAVFloat(FRDGBuilder& GraphBuilder, FUnorderedAccessViewRHIRef ClearTarget)
{
	FClearTargetParameters* ClearParameters = GraphBuilder.AllocParameters<FClearTargetParameters>();
	ClearParameters->ClearTarget = ClearTarget;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HoloMesh.ClearUAV"),
		ClearParameters,
		ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
		[ClearParameters](FRHICommandListImmediate& RHICmdList)
		{
			const FHoloMeshVec4 clearColor(0, 0, 0, 0);
			RHICmdList.Transition(FRHITransitionInfo(ClearParameters->ClearTarget, ERHIAccess::SRVGraphics, ERHIAccess::UAVCompute));
			RHICmdList.ClearUAVFloat(ClearParameters->ClearTarget, clearColor);
			RHICmdList.Transition(FRHITransitionInfo(ClearParameters->ClearTarget, ERHIAccess::UAVCompute, ERHIAccess::SRVGraphics));
		});
}

void HoloMeshUtilities::CopyTexture(FRDGBuilder& GraphBuilder, FIntVector Size, FRDGTextureRef SourceRDGTexture, int SourceMip, FTexture2DRHIRef DestTexture, int DestMip)
{
	FCopyTextureParameters* Parameters = GraphBuilder.AllocParameters<FCopyTextureParameters>();
	Parameters->Input = SourceRDGTexture;

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size = Size;
	CopyInfo.SourceMipIndex = SourceMip;
	CopyInfo.DestMipIndex = DestMip;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HoloMeshUtilities.CopyTexture"),
		Parameters,
		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
		[SourceRDGTexture, DestTexture, CopyInfo](FRHICommandList& RHICmdList)
		{
			RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::SRVGraphics, ERHIAccess::CopyDest));
			RHICmdList.CopyTexture(SourceRDGTexture->GetRHI(), DestTexture, CopyInfo);
			RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::CopyDest, ERHIAccess::SRVGraphics));
		});
}

void HoloMeshUtilities::CopyTexture(FRDGBuilder& GraphBuilder, FIntVector Size, FRDGTextureRef SourceRDGTexture, FIntVector SourcePosition, FRDGTextureRef DestRDGTexture, FTexture2DRHIRef DestTexture, FIntVector DestPosition)
{
	FCopyTextureParameters* Parameters = GraphBuilder.AllocParameters<FCopyTextureParameters>();
	Parameters->Input = SourceRDGTexture;

	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size = Size;
	CopyInfo.SourcePosition = SourcePosition;
	CopyInfo.DestPosition = DestPosition;

#if ENGINE_MAJOR_VERSION == 4
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HoloMeshUtilities.CopyTexture"),
		Parameters,
		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
		[SourceRDGTexture, DestRDGTexture, CopyInfo](FRHICommandList& RHICmdList)
		{
			RHICmdList.Transition(FRHITransitionInfo(DestRDGTexture->GetRHI(), ERHIAccess::SRVMask, ERHIAccess::CopyDest));
			RHICmdList.CopyTexture(SourceRDGTexture->GetRHI(), DestRDGTexture->GetRHI(), CopyInfo);
			RHICmdList.Transition(FRHITransitionInfo(DestRDGTexture->GetRHI(), ERHIAccess::CopyDest, ERHIAccess::SRVMask));
		});
#else
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HoloMeshUtilities.CopyTexture"),
		Parameters,
		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
		[SourceRDGTexture, DestTexture, CopyInfo](FRHICommandList& RHICmdList)
		{
			RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::SRVGraphics, ERHIAccess::CopyDest));
			RHICmdList.CopyTexture(SourceRDGTexture->GetRHI(), DestTexture, CopyInfo);
			RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::CopyDest, ERHIAccess::SRVGraphics));
		});
#endif
}