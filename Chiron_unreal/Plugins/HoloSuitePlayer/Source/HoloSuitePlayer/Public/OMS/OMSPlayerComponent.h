// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Templates/SharedPointer.h"
#include "Delegates/Delegate.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Runtime/Engine/Classes/Engine/TextureRenderTarget2D.h"
#include "HardwareInfo.h"

#if WITH_EDITOR
#include "Interfaces/IPluginManager.h"
#endif

#include <vector>
#include <utility>
#include <atomic>
#include <mutex>

#include "OMS/oms.h"
#include "OMS/OMSFile.h"
#include "OMS/OMSDecoder.h"
#include "OMS/OMSSkeleton.h"
#include "HoloMeshComponent.h"
#include "HoloSuitePlayerModule.h"
#include "HoloSuitePlayerSettings.h"

#include "OMSPlayerComponent.generated.h"

class AHoloSuitePlayer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnOMSPlayerComponentEvent);

UCLASS(hidecategories = (Object, LOD), meta = (BlueprintSpawnableComponent), ClassGroup = "HoloSuite")
class HOLOSUITEPLAYER_API UOMSPlayerComponent : public USceneComponent
{
	GENERATED_BODY()

public:	

	/* Source Parameters */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Source", meta = (DisplayName = "HoloSuite OMS File"))
	    UOMSFile* OMS;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "HoloSuite Player | Source")
        UMediaSource* TextureSource;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Source")
        UMaterialInterface* MeshMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Source", meta = (EditCondition = "MeshMaterial != nullptr", EditConditionHides))
        UMediaPlayer* MediaPlayer;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Source", meta = (EditCondition = "MeshMaterial != nullptr", EditConditionHides))
        UMaterialInterface* MediaPlayerMaterial;

    /* Playback Parameters */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback")
        bool PlayOnOpen;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback")
        bool Loop;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback")
        bool Mute;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback")
        float FrameRate;

    /* Decoder Parameters */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Decoder", meta = (EditCondition = "bSupportsCompute", EditConditionHides))
        bool UseCPUDecoder;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Decoder")
        int MaxBufferedSequences;

    /* Rendering Parameters */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Rendering")
        bool ResponsiveAA;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Rendering")
        bool ReceiveDecals;

    /* Skeleton Parameters */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "bHasSkeletonData", EditConditionHides))
        bool EnableSkeleton;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "EnableSkeleton", EditConditionHides))
        bool EnableRetargeting;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "EnableSkeleton", EditConditionHides))
        USkeletalMesh* PlayerSkeletalMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "EnableRetargeting", EditConditionHides))
        TSubclassOf<UAnimInstance> RetargetingAnimation;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "EnableRetargeting", EditConditionHides))
        UMaterialInterface* RetargetAnimMaterial;

    /* Default Material Parameters */

    UMaterialInterface* DefaultMeshMaterial;
    UMaterialInterface* DefaultMediaPlayerMaterial;
    UMaterialInterface* DefaultRetargetAnimMaterial;

    /* Default Functions */

	UOMSPlayerComponent(const FObjectInitializer& ObjectInitializer);
	~UOMSPlayerComponent();
    virtual void OnConstruction(const FTransform& Transform);
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    /* Internal Functions */

    void SyncWithHoloSuitePlayer(AHoloSuitePlayer* HoloSuitePlayer);
    bool CheckComputeSupport() { return UOMSDecoder::CheckComputeSupport(); }
    UMaterialInterface* GetMeshMaterial() { return MeshMaterial; }
    bool GetUseCPUDecoder();
    int GetMaxBufferedSequences();
    void SetFrame(int frameNumber);
    bool TrySetFrame(int frameNumber);

    /* Parameter Functions */

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        bool OpenOMSFiles(UOMSFile* newOMS, UMediaSource* newTextureSource, bool NewPlayOnOpen);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        void Close();

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        void SetMeshMaterial(UMaterialInterface* NewMeshMaterial);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        UHoloMeshMaterial* GetHoloMaterial() { return Decoder->GetHoloMaterial(); }

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void SetPlaybackParameters(bool NewPlayOnOpen, bool NewLoop, bool NewMute, float NewFrameRate);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        UMediaPlayer* GetMediaPlayer() { return MediaPlayer; }
    
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        UMaterialInterface* GetMediaPlayerMaterial() { return MediaPlayerMaterial; }

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        UMediaSoundComponent* GetMediaSoundComponent() { return MediaSoundComponent; }

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Decoder")
        void SetDecoderParameters(bool NewUseCPUDecoder, int NewMaxBufferedSequences);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Rendering")
        void SetRenderingParameters(bool NewResponsiveAA, bool NewReceiveDecals);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        void SetSkeletonParameters(bool NewEnableSkeleton, USkeletalMesh* NewPlayerSkeletalMesh, bool PrepareSkeleton = true);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        void SetRetargetParameters(bool NewEnableRetargeting, TSubclassOf<UAnimInstance> NewRetargetingAnimation, bool PrepareSkeleton = true);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        void SetRetargetingAnimationMaterial(UMaterialInterface* NewRetargetAnimMaterial);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        UMaterialInterface* GetRetargetAnimMaterial() { return RetargetAnimMaterial; }
    
    /* Playback Functions */

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void Play();

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void Pause();

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void SetMuted(bool shouldMute);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void Seek(FTimespan target);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        bool IsPlaying();

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        bool IsMuted();

    /* Skeleton Functions */

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        bool HasSkeletonData() { return bHasSkeletonData; }

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        void AttachActorToSkeleton(AActor* Actor, FName SocketName = NAME_None);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        USkeletalMeshComponent* GetSkeletalMeshComponent() { return PlayerSkeletalMeshComponent; }
    
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        ASkeletalMeshActor* GetSkeletalMeshActor() { return PlayerSkeletalMeshActor; }

    /* Event Delegates */

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnOMSPlayerComponentEvent OnOMSOpened;

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnOMSPlayerComponentEvent OnPlayerReady;

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnOMSPlayerComponentEvent OnPlaybackResumed;

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnOMSPlayerComponentEvent OnPlaybackSuspended;

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnOMSPlayerComponentEvent OnEndReached;

protected:

    UPROPERTY()
    UOMSDecoder* Decoder;
    FDecodedOMSSequenceRef DecodedSequence;

    OMSSkeleton* SkeletonManager;

    ASkeletalMeshActor* PlayerSkeletalMeshActor;
    USkeletalMeshComponent* PlayerSkeletalMeshComponent;
    TMap<AActor*, FName> ActorsToBeAttached;

    UMediaSoundComponent* MediaSoundComponent;

    bool bSequenceUpdated;
    bool bFrameUpdated;
    bool bIsPlaying;
    bool bHasSkeletonData;

    bool bLoadedFirstFrame;
    bool bPlayerReady;
    int activeSequence;
    int activeFrame;
    int FrameCount;
    float frameTimer;
    float sourceFrameRate;
    float currentFrameRate;
    int lastDecodedFrameNumber;
    int lastSkippedFrameNumber;

    void UnloadOMS();
    bool LoadSequence(int index, bool waitForSequence = true);
    bool LoadSequenceFrame(int index, bool sequenceUpdated);
    void LoadMediaPlayer();
    void CheckPlayerReady();
    void PrepareSkeletonManager();
    void DeleteMediaSound();
    void DeletePlayerSkeletalMesh();

    UFUNCTION()
        void OnMediaOpened(FString DeviceUrl);
    UFUNCTION()
        void OnMediaEndReached();

#if WITH_EDITOR
    void VerifyHoloSuitePlayer();
#endif
    bool VerifyMediaPlayer();

    // Returns true if this component belongs to a non-HoloSuitePlayer owner.
    bool IsCustomPlayer();
};