// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Animation/SkeletalMeshActor.h"
#include "Runtime/CoreUObject/Public/UObject/ConstructorHelpers.h"

#include "AVV/AVVFile.h"
#include "AVV/AVVDecoder.h"
#include "AVV/AVVDecoderCPU.h"
#include "AVV/AVVDecoderCompute.h"
#include "HoloMeshComponent.h"
#include "HoloMeshManager.h"
#include "HoloSuitePlayerModule.h"
#include "HoloSuitePlayerSettings.h"

#include "AVVPlayerComponent.generated.h"

class AHoloSuitePlayer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAVVPlayerComponentEvent);

UCLASS(hidecategories = (Object, LOD), meta = (BlueprintSpawnableComponent), ClassGroup = "HoloSuite")
class HOLOSUITEPLAYER_API UAVVPlayerComponent : public USceneComponent
{
	GENERATED_BODY()

public:	

    /* Source Parameters */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Source", meta = (DisplayName = "HoloSuite AVV File"))
        UAVVFile* AVVFile;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Source")
        UMaterialInterface* MeshMaterial;

    /* Playback Parameters */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback")
        bool ExternalTiming;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "!ExternalTiming", EditConditionHides))
        bool PlayOnOpen;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "!ExternalTiming", EditConditionHides))
        bool Loop;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "!ExternalTiming", EditConditionHides))
        bool PingPong;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "!ExternalTiming", EditConditionHides))
        bool Reverse;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "!ExternalTiming", EditConditionHides))
        float FrameRate;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback")
        float CurrentFrame;

    /* Level of Detail Parameters */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Level of Detail")
        float LOD0ScreenSize;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Level of Detail")
        float LOD1ScreenSize;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Level of Detail")
        float LOD2ScreenSize;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Level of Detail")
        int MinimumLOD;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Level of Detail")
        int ForceLOD;

    /* Decoder Parameters */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Decoder")
        bool LoadInEditor;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Decoder")
        int PlaybackDelay;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Decoder")
        bool UseCPUDecoder;

    /* Rendering Parameters */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Rendering")
        bool MotionVectors;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Rendering")
        bool ResponsiveAA;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Rendering")
        bool ReceiveDecals;

    /* Skeleton Parameters */

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "bHasSkeletonData", EditConditionHides))
        bool EnableSkeleton;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "EnableSkeleton", EditConditionHides))
        USkeletalMesh* PlayerSkeletalMesh;

    /* Default Material Parameters */

    UMaterialInterface* DefaultMeshMaterial;

    /* Default Functions */

    UAVVPlayerComponent(const FObjectInitializer& ObjectInitializer);
    ~UAVVPlayerComponent();
    virtual void OnConstruction(const FTransform& Transform);
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    /* Internal Functions */

    void SyncWithHoloSuitePlayer(AHoloSuitePlayer* HoloSuitePlayer);
    UMaterialInterface* GetMeshMaterial() { return MeshMaterial; }
    UAVVDecoder* GetDecoder() { return avvDecoder; }
    void UpdateFrame(float DeltaTime);
    void RefreshFrame();

    /* Parameter Functions */

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        bool OpenAVVFile(UAVVFile* NewAVV);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        void Close();

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        void SetMeshMaterial(UMaterialInterface* NewMeshMaterial);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        UHoloMeshMaterial* GetHoloMaterial() { return avvDecoder->GetHoloMaterial(); }

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void SetPlaybackParameters(bool NewExternalTiming, bool NewPlayOnOpen, bool NewLoop, bool NewPingPong, bool NewReverse, float NewFrameRate, float NewCurrentFrame);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Level of Detail")
        void SetLODParameters(float NewLOD0ScreenSize, float NewLOD1ScreenSize, float NewLOD2ScreenSize, int NewMinimumLOD, int NewForceLOD);
    
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Decoder")
        void SetDecoderParameters(bool NewLoadInEditor, int NewPlaybackDelay, bool NewUseCPUDecoder);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Rendering")
        void SetRenderingParameters(bool NewMotionVectors, bool NewResponsiveAA, bool NewReceiveDecals);

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        void SetSkeletonParameters(bool NewEnableSkeleton, USkeletalMesh* NewPlayerSkeletalMesh);
    
    /* Playback Functions */

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void Play();

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void Pause();

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void Seek(FTimespan target);

    /* Skeleton Functions */

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        bool HasSkeletonData() { return bHasSkeletonData; }

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        USkeletalMeshComponent* GetSkeletalMeshComponent() { return PlayerSkeletalMeshComponent; }

    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        void AttachActorToSkeleton(AActor* Actor, FName SocketName = NAME_None);

    /* Event Delegates */

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnAVVPlayerComponentEvent OnDecoderCreated;

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnAVVPlayerComponentEvent OnAVVOpened;

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnAVVPlayerComponentEvent OnAVVOpenFailed;

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnAVVPlayerComponentEvent OnPlaybackResumed;

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnAVVPlayerComponentEvent OnPlaybackSuspended;

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnAVVPlayerComponentEvent OnEndReached;

    UPROPERTY(BlueprintAssignable, meta = (HideInDetailPanel))
        FOnAVVPlayerComponentEvent OnLoop;

protected:

    UAVVDecoder* avvDecoder;

    USkeletalMeshComponent* PlayerSkeletalMeshComponent;
    TMap<AActor*, FName> ActorsToBeAttached;

    bool bFirstRun;
    bool bShouldPlay;
    bool bAVVLoaded;
    bool bHasSkeletonData;
    float FrameTimer;
    int CurrentEngineFrame;

    bool LoadAVV();
    void UnloadAVV();
    void CreateSkeletalMeshComponent(bool shouldDeleteFirst);
    void DeleteSkeletalMeshComponent();

    // Returns true if this component belongs to a non-HoloSuitePlayer owner.
    bool IsCustomPlayer();
};