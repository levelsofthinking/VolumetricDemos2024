// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Delegates/Delegate.h"
#include "Templates/Atomic.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "Components/SphereComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/BillboardComponent.h"

#include "AVV/AVVFile.h"
#include "AVV/AVVPlayerComponent.h"
#include "OMS/OMSFile.h"
#include "OMS/OMSPlayerComponent.h"
#include "HoloSuiteFile.h"
#include "HoloSuitePlayerModule.h"
#include "HoloSuitePlayerSettings.h"

#include "HoloSuitePlayer.generated.h"

/** Multicast delegate that is invoked when an event occurred in the HoloSuite volumetric player. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnHoloSuitePlayerEvent);

// Tracks the player type based on the provided source volumetric video.
UENUM(BlueprintType)
enum class EPlayerType : uint8
{
    OMS,
    AVV,
    UNKNOWN
};

/**
 * Implementation of a HoloSuite volumetric player, i.e. an actor capable of playing back volumetric data from HoloSuite.
 *
 * AHoloSuitePlayers can load OMS and AVV source files and playback them as volumetric clips.
 * Custom materials and shaders can be applied.
 * This class also supports the use of Sequencer from Unreal.
 */
UCLASS(meta = (DisplayName = "HoloSuite Player"))
class HOLOSUITEPLAYER_API AHoloSuitePlayer : public AActor
{
	GENERATED_BODY()
	
public:	

	// Source Parameters

	// Specify a volumetric file encoded using HoloEdit.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Source", meta = (DisplayName = "HoloSuite Volumetric File"))
		UHoloSuiteFile* SourceFile;

    // Specify an MP4 video or an image sequence for texture playback.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "HoloSuite Player | Source", meta = (EditCondition = "PlayerType == EPlayerType::OMS", EditConditionHides))
        UMediaSource* TextureSource;

    // Specify a Material for volumetric playback. This material should include an Arcturus Material Function for proper playback.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Source")
        UMaterialInterface* MeshMaterial;

    // An automatically generated Media Player configured with a corresponding Media Texture.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Source", meta = (EditCondition = "PlayerType == EPlayerType::OMS && MeshMaterial != nullptr", EditConditionHides))
        UMediaPlayer* MediaPlayer;

    // An automatically generated Material for the Media Player of this player.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Source", meta = (EditCondition = "PlayerType == EPlayerType::OMS && MeshMaterial != nullptr", EditConditionHides))
        UMaterialInterface* MediaPlayerMaterial;

    // Playback Parameters

    // Enable when playback timing will be controlled by Blueprint or Sequencer.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "PlayerType == EPlayerType::AVV", EditConditionHides))
        bool ExternalTiming;

    // Toggle whether the volumetric content should play automatically.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "PlayerType != EPlayerType::UNKNOWN && !ExternalTiming", EditConditionHides))
        bool PlayOnOpen;

    // Toggle whether the volumetric content should loop, i.e. should restart once it finishes playback.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "PlayerType != EPlayerType::UNKNOWN && !ExternalTiming", EditConditionHides))
        bool Loop;

    // Toggle whether the volumetric content should play back and forth. This only works when Loop is enabled. Only available for AVV playback.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "PlayerType == EPlayerType::AVV && !ExternalTiming", EditConditionHides))
        bool PingPong;

    // Toggle whether the volumetric content should playback in reverse. This is overriden if PingPong is enabled. Motion Vectors should be disabled if playback is reversed, given they're not bidirectional. Only available for AVV playback.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "PlayerType == EPlayerType::AVV && !ExternalTiming", EditConditionHides))
        bool Reverse;

    // Toggle whether audio should be enabled or not.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "PlayerType == EPlayerType::OMS", EditConditionHides))
        bool Mute;

    // Set the frame rate. If your AVV file was exported with a custom frame rate, set this parameter to match your exported frame rate.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "PlayerType != EPlayerType::UNKNOWN && !ExternalTiming", EditConditionHides, ClampMin = 1, UIMin = 1))
        float FrameRate;

    // Current frame of the volumetric video. Note: CurrentFrame is a float so it can be easily interpolated in Sequencer.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Playback", meta = (EditCondition = "PlayerType == EPlayerType::AVV", EditConditionHides, ClampMin = 0, UIMin = 0))
        float CurrentFrame;

    // Level of Detail Parameters

    // Base LOD.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Level of Detail", meta = (DisplayName = "LOD 0 Screen Size", EditCondition = "PlayerType == EPlayerType::AVV", EditConditionHides))
        float LOD0ScreenSize;

    // LOD 1.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Level of Detail", meta = (DisplayName = "LOD 1 Screen Size", EditCondition = "PlayerType == EPlayerType::AVV", EditConditionHides))
        float LOD1ScreenSize;

    // LOD 2.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Level of Detail", meta = (DisplayName = "LOD 2 Screen Size", EditCondition = "PlayerType == EPlayerType::AVV", EditConditionHides))
        float LOD2ScreenSize;

    // Specify the minimum LOD to use. Useful for when the player is expected to always be far away. Default is 0.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Level of Detail", meta = (EditCondition = "PlayerType == EPlayerType::AVV", EditConditionHides, ClampMin = 0, UIMin = 0))
        int MinimumLOD;

    // Specify the LOD to always use. Ignored if set to -1.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Level of Detail", meta = (EditCondition = "PlayerType == EPlayerType::AVV", EditConditionHides, ClampMin = -1, UIMin = -1))
        int ForceLOD;

    // Decoder Parameters

    // Toggle whether the AVV content should be decoded and loaded into the scene while working in the Editor and not playing.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Decoder", meta = (EditCondition = "PlayerType == EPlayerType::AVV", EditConditionHides))
        bool LoadInEditor;

    // Specify the amount of time in miliseconds that the player should wait for before starting playback.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Decoder", meta = (EditCondition = "PlayerType == EPlayerType::AVV", EditConditionHides, ClampMin = 0, UIMin = 0))
        int PlaybackDelay;

    // Toggle whether decoding should be done in the CPU. This is recommended for mobile target platforms such as the Quest. Value is overriden for platforms that do not support compute shaders.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Decoder", meta = (DisplayName = "Use CPU Decoder", EditCondition = "bSupportsCompute", EditConditionHides))
        bool UseCPUDecoder;

    // Specify the maximum number of sequences to buffer / pre-load during playback.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Decoder", meta = (EditCondition = "PlayerType == EPlayerType::OMS", EditConditionHides, ClampMin = 1, UIMin = 1))
        int MaxBufferedSequences;

    // Rendering Parameters

    // Toggle to enable the use of motion vectors. This will only have an effect if your AVV has been exported from HoloEdit with motion vectors.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Rendering", meta = (EditCondition = "PlayerType == EPlayerType::AVV", EditConditionHides))
        bool MotionVectors;

    // Toggle to enable responsive anti-aliasing. 
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Rendering", meta = (EditCondition = "PlayerType != EPlayerType::UNKNOWN", EditConditionHides))
        bool ResponsiveAA;

    // Toggle whether the volumetric video should receive decals.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Rendering", meta = (EditCondition = "PlayerType != EPlayerType::UNKNOWN", EditConditionHides))
        bool ReceiveDecals;

    // Skeleton Parameters

    // Toggle to enable the use of skeletons for object/particle attachment. Applicable only to source files that have been exported with rigging data.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "bHasSkeletonData && PlayerType != EPlayerType::UNKNOWN", EditConditionHides))
        bool EnableSkeleton;

    // Enable the use of skeletons for retargeting. Applicable only to source files that have been exported with rigging data.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "PlayerType == EPlayerType::OMS && EnableSkeleton", EditConditionHides))
        bool EnableRetargeting;

    // Specify a Skeletal Mesh asset for object/particle attachment. It must be created from volumetric source files that have been exported with skeleton data.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "EnableSkeleton", EditConditionHides))
        USkeletalMesh* PlayerSkeletalMesh;

    // Specify an Animation asset for Retargeting. It must be created automatically from any OMS containing rigging data.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "PlayerType == EPlayerType::OMS && EnableRetargeting", EditConditionHides))
        TSubclassOf<UAnimInstance> RetargetingAnimation;

    // Default Functions

    // Sets default values for the properties of this player.
    AHoloSuitePlayer(const FObjectInitializer& ObjectInitializer);

    // Destroys player and its components.
    virtual ~AHoloSuitePlayer() override;

    // Called after constructor.
    void OnConstruction(const FTransform& Transform) override;

    // Called when the game starts or when spawned.
    virtual void BeginPlay() override;

    // Called when the actor is about to be destroyed.
    virtual void BeginDestroy() override;

    // Called to determine whether the player should tick (update) when running in a game viewport.
    virtual bool ShouldTickIfViewportsOnly() const override;

    // Called every frame.
    virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
    // Called when one of the actor's properties is modified in the Editor.
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    // Internally Exposed Functions

    // Initializes the player component responsible for AVV or OMS decoding and playback.
    void InitializePlayerComponent(bool destroyComponents, bool avoidSync, bool avoidLoad, bool avoidReinit);

    // Retrieves the player component responsible for AVV decoding and playback.
    UAVVPlayerComponent* GetAVVPlayerComponent() { return AVVPlayerComponent; }

    // Retrieves the player component responsible for OMS decoding and playback.
    UOMSPlayerComponent* GetOMSPlayerComponent() { return OMSPlayerComponent; }

    // Configures default Material used if no MeshMaterial is assigned. Called in HoloSuitePlayerFactory.
    void SetDefaultMeshMaterial(UMaterialInterface* NewDefaultMeshMaterial) { DefaultMeshMaterial = NewDefaultMeshMaterial; }

    // Retrieves default MeshMaterial.
    UMaterialInterface* GetDefaultMeshMaterial() { return DefaultMeshMaterial; }

    // Configures default Material used if no MediaPlayerMaterial is assigned. Called in HoloSuitePlayerFactory.
    void SetDefaultMediaPlayerMaterial(UMaterialInterface* NewDefaultMediaPlayerMaterial) { DefaultMediaPlayerMaterial = NewDefaultMediaPlayerMaterial; }

    // Retrieves default MediaPlayerMaterial.
    UMaterialInterface* GetDefaultMediaPlayerMaterial() { return DefaultMediaPlayerMaterial; }

    // Configures default Material used for Retarget Animation. Called in HoloSuitePlayerFactory.
    void SetDefaultRetargetAnimMaterial(UMaterialInterface* NewDefaultRetargetAnimMaterial) { DefaultRetargetAnimMaterial = NewDefaultRetargetAnimMaterial; }

    // Retrieves default RetargetAnimMaterial.
    UMaterialInterface* GetDefaultRetargetAnimMaterial() { return DefaultRetargetAnimMaterial; }

    // Parameter Functions

    // Assigns a new volumetric file encoded using HoloEdit and opens it. Meant for AVV playback.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        void OpenVolumetricFile(UHoloSuiteFile* NewSourceFile);

    // Assigns a new volumetric file and corresponding texture file encoded using HoloEdit and opens it. Meant for OMS playback.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        void OpenVolumetricFiles(UHoloSuiteFile* NewSourceFile, UMediaSource* NewTextureSource, bool NewPlayOnOpen);

    // Assigns a new Mesh Material.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        void SetMeshMaterial(UMaterialInterface* NewMeshMaterial);

    // Returns a HoloMeshMaterial asset used to configure the material properties of the player.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Source")
        UHoloMeshMaterial* GetMeshMaterial();

    // Configures OMS playback options.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void SetOMSPlaybackParameters(bool NewPlayOnOpen, bool NewLoop, bool NewMute, float NewFrameRate);

    // Configures AVV playback options.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void SetAVVPlaybackParameters(bool NewExternalTiming, bool NewPlayOnOpen, bool NewLoop, bool NewPingPong, bool NewReverse, float NewFrameRate, float NewCurrentFrame);

    // Configures AVV LOD options.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Level of Detail")
        void SetAVVLODParameters(float NewLOD0ScreenSize, float NewLOD1ScreenSize, float NewLOD2ScreenSize, int NewMinimumLOD, int NewForceLOD);

    // Configures OMS decoder options.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Decoder")
        void SetOMSDecoderParameters(bool NewUseCPUDecoder, int NewNumBufferedSequences);

    // Configures AVV decoder options.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Decoder")
        void SetAVVDecoderParameters(bool NewLoadInEditor, int NewPlaybackDelay, bool NewUseCPUDecoder);

    // Configures OMS rendering options.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Rendering")
        void SetOMSRenderParameters(bool NewResponsiveAA, bool NewReceiveDecals);

    // Configures AVV rendering options.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Rendering")
        void SetAVVRenderParameters(bool NewMotionVectors, bool NewResponsiveAA, bool NewReceiveDecals);

    // Configures skeleton options for object attachment and/or retargeting.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        void SetSkeletonParameters(bool NewEnableSkeleton, USkeletalMesh* NewPlayerSkeletalMesh);

    // Configures OMS skeleton options for retargeting. For OMS playback with retargeting, SetSkeletonParameters needs to be called prior to this function to ensure a valid SkeletalMesh is provided.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        void SetOMSRetargetParameters(bool NewEnableRetargeting, TSubclassOf<UAnimInstance> NewRetargetingAnimation);

    // Playback Functions

    // Starts playback.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void Play();

    // Pauses playback.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void Pause();

    // Seeks to the specified playback time.
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Playback")
        void Seek(FTimespan target);

    // Skeleton Functions

    // Returns true if volumetric source file contains skeleton data.
    bool HasSkeletonData() { return bHasSkeletonData; }

    // Returns the child SkeletalMeshComponent if there is any (if EnableSkeleton is true).
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        USkeletalMeshComponent* GetSkeletalMeshComponent();

    // Attaches an Actor to one of the SkeletalMesh's bones (if EnableSkeleton is true).
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        void AttachActorToSkeleton(AActor* Actor, FName SocketName = NAME_None);

    // Detaches an Actor from one of the SkeletalMesh's bones (if EnableSkeleton is true).
    UFUNCTION(BlueprintCallable, Category = "HoloSuite Player | Skeleton")
        void DetachActorFromSkeleton(AActor* Actor);

    // Event Delegates

    // Invoked when the volumetric source file(s) has been successfully loaded.
    UPROPERTY(BlueprintAssignable, Category = "HoloSuite Player | Source", meta = (HideInDetailPanel))
        FOnHoloSuitePlayerEvent OnMediaOpened;

    // Invoked when the source AVV/OMS file failed to be loaded. Currently only available for AVV playback.
    UPROPERTY(BlueprintAssignable, Category = "HoloSuite Player | Source", meta = (HideInDetailPanel))
        FOnHoloSuitePlayerEvent OnMediaOpenFailed;

    // Invoked when the volumetric source file(s) are ready for playback. Only applicable to OMS playback.
    UPROPERTY(BlueprintAssignable, Category = "HoloSuite Player | Playback", meta = (HideInDetailPanel))
        FOnHoloSuitePlayerEvent OnMediaReady;

    // Invoked when playback starts or is resumed.
    UPROPERTY(BlueprintAssignable, Category = "HoloSuite Player | Playback", meta = (HideInDetailPanel))
        FOnHoloSuitePlayerEvent OnPlaybackResumed;

    // Invoked when playback is stopped / suspended / paused.
    UPROPERTY(BlueprintAssignable, Category = "HoloSuite Player | Playback", meta = (HideInDetailPanel))
        FOnHoloSuitePlayerEvent OnPlaybackSuspended;

    // Invoked when playback has finished.
    UPROPERTY(BlueprintAssignable, Category = "HoloSuite Player | Playback", meta = (HideInDetailPanel))
        FOnHoloSuitePlayerEvent OnEndReached;

    // Invoked when loop point is reached and playback restarts from the beginning. Currently only available for AVV playback.
    UPROPERTY(BlueprintAssignable, Category = "HoloSuite Player | Playback", meta = (HideInDetailPanel))
        FOnHoloSuitePlayerEvent OnLoop;

protected:

    // Tracks the player type based on the provided source volumetric video.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Source", meta = (EditCondition = "false", EditConditionHides)) // this meta forces the property to be hidden but still usable as an EditCondition
        EPlayerType PlayerType = { EPlayerType::UNKNOWN };

    // Used to avoid duplicate initialization of player components.
    bool bInitialized;

    // Used to configure Custom Icon for the HoloSuite volumetric video player.
    UBillboardComponent* SpriteComponent;

    // Empty sphere used to configure player transform properties.
    USphereComponent* root;

    // Player component responsible for AVV decoding and playback.
    UAVVPlayerComponent* AVVPlayerComponent;

    // Player component responsible for OMS decoding and playback.
	UOMSPlayerComponent* OMSPlayerComponent;

    // Used to determine if CPU decoding should be forced.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Decoder", meta = (EditCondition = "false", EditConditionHides)) // this meta forces the property to be hidden but still usable as an EditCondition
        bool bSupportsCompute;

    // True if volumetric source file contains skeleton data.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Interp, Category = "HoloSuite Player | Skeleton", meta = (EditCondition = "false", EditConditionHides)) // this meta forces the property to be hidden but still usable as an EditCondition
        bool bHasSkeletonData;

    // Default Material used if MeshMaterial isn't assigned.
    UPROPERTY()
        UMaterialInterface* DefaultMeshMaterial;

    // Default Material used if MediaPlayerMaterial isn't assigned.
    UPROPERTY()
        UMaterialInterface* DefaultMediaPlayerMaterial;

    // Default material used for Retarget Animation Material. 
    UPROPERTY()
        UMaterialInterface* DefaultRetargetAnimMaterial;

    // Holds an instance of the default Retarget Animation Material. This isn't exposed to the user.
    UPROPERTY()
        UMaterialInterface* RetargetAnimMaterial;

    // Actors to attach to bones of the volumetric video.
    TMap<AActor*, FName> ActorsToBeAttached;

    // Internal Functions

    // Sets the type of HoloSuite player (AVV or OMS).
    void SetPlayerType();

    // Synchronizes player component's parameters with its own.
	void SyncPlayerComponent();

    // Synchronizes HoloSuitePlayer's parameters with those of the player component. Used after opening a new source volumetric file on the player component.
    void SyncPostOpen();

    // Event Handlers

    // Listens to OnMediaOpen events from whichever player component is being used and triggers corresponding event of HoloSuitePlayer.
    UFUNCTION()
        void HandleOnMediaOpened();

    // Listens to OnMediaOpenFailed events from whichever player component is being used and triggers corresponding event of HoloSuitePlayer.
    UFUNCTION()
        void HandleOnMediaOpenFailed();

    // Listens to OnPlayerReady events from whichever player component is being used and triggers corresponding event of HoloSuitePlayer.
    UFUNCTION()
        void HandleOnPlayerReady();

    // Listens to OnPlaybackResumed events from whichever player component is being used and triggers corresponding event of HoloSuitePlayer.
    UFUNCTION()
        void HandleOnPlaybackResumed();

    // Listens to OnPlaybackSuspended events from whichever player component is being used and triggers corresponding event of HoloSuitePlayer.
    UFUNCTION()
        void HandleOnPlaybackSuspended();

    // Listens to OnEndReached events from whichever player component is being used and triggers corresponding event of HoloSuitePlayer.
    UFUNCTION()
        void HandleOnEndReached();

    // Listens to OnLoop events from whichever player component is being used and triggers corresponding event of HoloSuitePlayer.
    UFUNCTION()
        void HandleOnLoop();
};