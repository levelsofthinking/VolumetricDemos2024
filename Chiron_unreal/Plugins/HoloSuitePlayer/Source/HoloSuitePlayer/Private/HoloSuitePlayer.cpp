// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "HoloSuitePlayer.h"

/* Profiling Declarations */

DECLARE_CYCLE_STAT(TEXT("HoloSuitePlayer.Constructor"),                 STAT_HoloSuitePlayer_Constructor,                STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("HoloSuitePlayer.Destructor"),                  STAT_HoloSuitePlayer_Destructor,                 STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("HoloSuitePlayer.OnConstruction"),              STAT_HoloSuitePlayer_OnConstruction,             STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("HoloSuitePlayer.BeginPlay"),                   STAT_HoloSuitePlayer_BeginPlay,                  STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("HoloSuitePlayer.BeginDestroy"),                STAT_HoloSuitePlayer_BeginDestroy,               STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("HoloSuitePlayer.PostEditChangeProperty"),      STAT_HoloSuitePlayer_PostEditChangeProperty,     STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("HoloSuitePlayer.InitializePlayerComponent"),   STAT_HoloSuitePlayer_InitializePlayerComponent,  STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("HoloSuitePlayer.SyncPlayerComponent"),         STAT_HoloSuitePlayer_SyncPlayerComponent,        STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("HoloSuitePlayer.OpenVolumetricFile"),          STAT_HoloSuitePlayer_OpenVolumetricFile,         STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("HoloSuitePlayer.OpenVolumetricFiles"),         STAT_HoloSuitePlayer_OpenVolumetricFiles,        STATGROUP_HoloSuitePlayer);

/* Default Functions */

AHoloSuitePlayer::AHoloSuitePlayer(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SCOPE_CYCLE_COUNTER(STAT_HoloSuitePlayer_Constructor);
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: Constructor"));

    root = CreateDefaultSubobject<USphereComponent>(TEXT("HoloMesh"));
    SetRootComponent(root);

#if WITH_EDITORONLY_DATA
    SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));

    if (!IsRunningCommandlet())
    {
        // Structure to hold one-time initialization
        struct FConstructorStatics
        {
            ConstructorHelpers::FObjectFinderOptional<UTexture2D> HoloSuiteTextureObject;
            FName ID_HoloSuite;
            FText NAME_HoloSuite;
            FConstructorStatics()
                : HoloSuiteTextureObject(TEXT("/HoloSuitePlayer/Arcturus/Icon128"))
                , ID_HoloSuite(TEXT("HoloSuite"))
                , NAME_HoloSuite(NSLOCTEXT("SpriteCategory", "HoloSuite", "HoloSuite"))
            {
            }
        };
        static FConstructorStatics ConstructorStatics;

        if (SpriteComponent)
        {
            SpriteComponent->Sprite = ConstructorStatics.HoloSuiteTextureObject.Get();
            SpriteComponent->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));
            SpriteComponent->SpriteInfo.Category = ConstructorStatics.ID_HoloSuite;
            SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_HoloSuite;
            SpriteComponent->Mobility = EComponentMobility::Movable;
            SpriteComponent->SetupAttachment(RootComponent);
        }
    }
#endif

    bInitialized            = false;
    bSupportsCompute        = false;

    // Components
    AVVPlayerComponent      = nullptr;
    OMSPlayerComponent      = nullptr;

    // Playback
    ExternalTiming          = false;
    PlayOnOpen              = true;
    Loop                    = true;
    PingPong                = false;
    Reverse                 = false;
    Mute                    = false;
    FrameRate               = 30.0f;
    CurrentFrame            = 0;

    // Level of Detail (LOD)
    MinimumLOD              = 0;
    ForceLOD                = -1;

    // Decoder
    MaxBufferedSequences    = 20;
    LoadInEditor            = true;
    PlaybackDelay           = 0;
    UseCPUDecoder           = false;
    bSupportsCompute        = false;

    // Skeleton
    EnableSkeleton          = false;
    EnableRetargeting       = false;
    bHasSkeletonData        = false;

    // Load global settings to get defaults.
    const UHoloSuitePlayerSettings* settings = GetDefault<UHoloSuitePlayerSettings>(UHoloSuitePlayerSettings::StaticClass());
    if (settings != nullptr)
    {
        // Rendering
        MotionVectors = settings->MotionVectors;
        ResponsiveAA  = settings->ResponsiveAA;
        ReceiveDecals = settings->ReceiveDecals;

        // Level of Detail (LOD)
        LOD0ScreenSize = settings->LOD0ScreenSize;
        LOD1ScreenSize = settings->LOD1ScreenSize;
        LOD2ScreenSize = settings->LOD2ScreenSize;

        // OMS
        MaxBufferedSequences = settings->MaxBufferedSequences;
    }
    else
    {
        // Rendering
        MotionVectors = true;
        ResponsiveAA  = false;
        ReceiveDecals = true;

        // Level of Detail (LOD)
        LOD0ScreenSize = 1.0f;
        LOD1ScreenSize = 0.5f;
        LOD2ScreenSize = 0.25f;
    }

    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
}

AHoloSuitePlayer::~AHoloSuitePlayer()
{
    SCOPE_CYCLE_COUNTER(STAT_HoloSuitePlayer_Destructor);
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: Destructor"));

}

void AHoloSuitePlayer::OnConstruction(const FTransform& Transform)
{
    SCOPE_CYCLE_COUNTER(STAT_HoloSuitePlayer_OnConstruction);
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: OnConstruction"));

    InitializePlayerComponent(false, true, true, true);
}

// Called when the game starts or when spawned
void AHoloSuitePlayer::BeginPlay()
{
    SCOPE_CYCLE_COUNTER(STAT_HoloSuitePlayer_BeginPlay);
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: BeginPlay"));

	Super::BeginPlay();

    InitializePlayerComponent(false, false, false, true);
}

void AHoloSuitePlayer::BeginDestroy()
{
    SCOPE_CYCLE_COUNTER(STAT_HoloSuitePlayer_BeginDestroy);
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: BeginDestroy"));

    Super::BeginDestroy();

    bInitialized = false;
}

#if WITH_EDITOR
void AHoloSuitePlayer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    SCOPE_CYCLE_COUNTER(STAT_HoloSuitePlayer_PostEditChangeProperty);

    if (PropertyChangedEvent.Property == nullptr)
    {
        Super::PostEditChangeProperty(PropertyChangedEvent);
        return;
    }

    FString propertyName = PropertyChangedEvent.GetPropertyName().GetPlainNameString();
    //UE_LOG(LogHoloSuitePlayer, Warning, TEXT("HoloSuitePlayer: Property changed: %s"), *propertyName);

    if (propertyName == "SourceFile")
    {
        bInitialized = false;
        InitializePlayerComponent(true, false, true, true);
    }
    
    if (propertyName == "TextureSource" || propertyName == "MediaPlayer" || propertyName == "MediaPlayerMaterial")
    {
        if (PlayerType == EPlayerType::OMS)
        {
            SyncPlayerComponent();
            if (OMSPlayerComponent->OpenOMSFiles(Cast<UOMSFile>(SourceFile), TextureSource, PlayOnOpen))
            {
                SyncPostOpen();
            }
        }
    }
    
    if (propertyName == "MeshMaterial")
    {
        if (PlayerType == EPlayerType::AVV)
        {
            AVVPlayerComponent->SetMeshMaterial(MeshMaterial);
        }
        else if (PlayerType == EPlayerType::OMS)
        {
            OMSPlayerComponent->SetMeshMaterial(MeshMaterial);
        }
    }

    if (propertyName == "ExternalTiming" || propertyName == "PlayOnOpen" || propertyName == "Loop" 
        || propertyName == "PingPong" || propertyName == "Reverse" || propertyName == "Mute"
        || propertyName == "FrameRate" || propertyName == "CurrentFrame")
    {
        if (PlayerType == EPlayerType::AVV)
        {
            AVVPlayerComponent->SetPlaybackParameters(ExternalTiming, PlayOnOpen, Loop, PingPong, Reverse, FrameRate, CurrentFrame);
        }
        else if (PlayerType == EPlayerType::OMS)
        {
            OMSPlayerComponent->SetPlaybackParameters(PlayOnOpen, Loop, Mute, FrameRate);
        }
    }

    if (propertyName == "LOD0ScreenSize" || propertyName == "LOD1ScreenSize" || propertyName == "LOD2ScreenSize"
        || propertyName == "MinimumLOD" || propertyName == "ForceLOD")
    {
        if (PlayerType == EPlayerType::AVV)
        {
            AVVPlayerComponent->SetLODParameters(LOD0ScreenSize, LOD1ScreenSize, LOD2ScreenSize, MinimumLOD, ForceLOD);
        }
    }

    if (propertyName == "NumBufferedSequences" || propertyName == "LoadInEditor" || propertyName == "PlaybackDelay"
        || propertyName == "UseCPUDecoder")
    {
        if (PlayerType == EPlayerType::AVV)
        {
            AVVPlayerComponent->SetDecoderParameters(LoadInEditor, PlaybackDelay, UseCPUDecoder);
        }
        else if (PlayerType == EPlayerType::OMS)
        {
            OMSPlayerComponent->SetDecoderParameters(UseCPUDecoder, MaxBufferedSequences);

            // TODO: warn the user if parameters were overriden?
            UseCPUDecoder = OMSPlayerComponent->GetUseCPUDecoder();
            MaxBufferedSequences = OMSPlayerComponent->GetMaxBufferedSequences();
        }
    }

    if (propertyName == "MotionVectors" || propertyName == "ResponsiveAA"
        || propertyName == "ReceiveDecals")
    {
        if (PlayerType == EPlayerType::AVV)
        {
            AVVPlayerComponent->SetRenderingParameters(MotionVectors, ResponsiveAA, ReceiveDecals);
        }
        else if (PlayerType == EPlayerType::OMS)
        {
            OMSPlayerComponent->SetRenderingParameters(ResponsiveAA, ReceiveDecals);
        }
    }

    if (propertyName == "EnableSkeleton" || propertyName == "PlayerSkeletalMesh")
    {
        if (PlayerType == EPlayerType::AVV)
        {
            AVVPlayerComponent->SetSkeletonParameters(EnableSkeleton, PlayerSkeletalMesh);
        }
        else if (PlayerType == EPlayerType::OMS)
        {
            OMSPlayerComponent->SetSkeletonParameters(EnableSkeleton, PlayerSkeletalMesh);
        }
    }

    if (propertyName == "EnableRetargeting" || propertyName == "RetargetingAnimation")
    {
        if (PlayerType == EPlayerType::OMS)
        {
            OMSPlayerComponent->SetRetargetParameters(EnableRetargeting, RetargetingAnimation);
        }
    }

    if (PlayerType == EPlayerType::AVV && AVVPlayerComponent)
    {
        AVVPlayerComponent->RefreshFrame();
    }

    Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

bool AHoloSuitePlayer::ShouldTickIfViewportsOnly() const
{
    if (GetWorld() != nullptr && GetWorld()->WorldType == EWorldType::Editor && LoadInEditor)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void AHoloSuitePlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

    if (ActorsToBeAttached.Num() > 0 && (OMSPlayerComponent || AVVPlayerComponent))
    {
        if (PlayerType == EPlayerType::AVV)
        {
            for (auto& Entry : ActorsToBeAttached)
            {
                AVVPlayerComponent->AttachActorToSkeleton(Entry.Key, Entry.Value);
            }
        }
        else if (PlayerType == EPlayerType::OMS)
        {
            for (auto& Entry : ActorsToBeAttached)
            {
                OMSPlayerComponent->AttachActorToSkeleton(Entry.Key, Entry.Value);
            }
        }
        ActorsToBeAttached.Empty();
    }
}

void AHoloSuitePlayer::SetPlayerType()
{
    PlayerType = EPlayerType::UNKNOWN;
    if (SourceFile)
    {
        // AVV
        UAVVFile* avvSource = Cast<UAVVFile>(SourceFile);
        if (avvSource) 
        {
            PlayerType = EPlayerType::AVV;
            return;
        }

        // OMS
        UOMSFile* omsSource = Cast<UOMSFile>(SourceFile);
        if (omsSource) 
        {
            PlayerType = EPlayerType::OMS;
            return;
        }
    }
}

void AHoloSuitePlayer::InitializePlayerComponent(bool destroyComponents, bool avoidSync, bool avoidLoad, bool avoidReinit)
{
    SCOPE_CYCLE_COUNTER(STAT_HoloSuitePlayer_InitializePlayerComponent);
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: InitializePlayerComponent"));

    if (avoidReinit && bInitialized)
    {
        return;
    }

    FDetachmentTransformRules rules(FDetachmentTransformRules::KeepWorldTransform);
    bool componentAlreadyExisted = true;
    bSupportsCompute = false;
    bHasSkeletonData = false;

    SetPlayerType();
    if (PlayerType == EPlayerType::AVV)
    {
        if (destroyComponents && OMSPlayerComponent)
        {
            OMSPlayerComponent->Close();
            TArray<USceneComponent*> attachedChildren = OMSPlayerComponent->GetAttachChildren();
            for (int i = 0; i < attachedChildren.Num(); i++)
            {
                attachedChildren[i]->DetachFromComponent(rules);
            }
            OMSPlayerComponent->DetachFromComponent(rules);
            OMSPlayerComponent->DestroyComponent();
            OMSPlayerComponent = nullptr;
            MeshMaterial = nullptr;
        }

        if (!AVVPlayerComponent)
        {
            AVVPlayerComponent = (UAVVPlayerComponent*)NewObject<UAVVPlayerComponent>(this, TEXT("AVVPlayerComponent"));
            if (GetWorld())
            {
                AVVPlayerComponent->RegisterComponent();
            }
            AVVPlayerComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            bSupportsCompute = true;
            componentAlreadyExisted = false;
        }

        if (!avoidSync || !componentAlreadyExisted)
        {
            SyncPlayerComponent();

            if (!avoidLoad || LoadInEditor)
            {
                if (AVVPlayerComponent->OpenAVVFile(Cast<UAVVFile>(SourceFile)))
                {
                    SyncPostOpen();
                }
            }
        }
        bInitialized = true;
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        if (destroyComponents && AVVPlayerComponent)
        {
            AVVPlayerComponent->Close();
            TArray<USceneComponent*> attachedChildren = AVVPlayerComponent->GetAttachChildren();
            for (int i = 0; i < attachedChildren.Num(); i++)
            {
                attachedChildren[i]->DetachFromComponent(rules);
            }
            AVVPlayerComponent->DetachFromComponent(rules);
            AVVPlayerComponent->DestroyComponent();
            AVVPlayerComponent = nullptr;
            MeshMaterial = nullptr;
        }

        if (!OMSPlayerComponent)
        {
            OMSPlayerComponent = (UOMSPlayerComponent*)NewObject<UOMSPlayerComponent>(this, TEXT("OMSPlayerComponent"));
            if (GetWorld())
            {
                OMSPlayerComponent->RegisterComponent();
            }
            OMSPlayerComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            bSupportsCompute = OMSPlayerComponent->CheckComputeSupport();
            componentAlreadyExisted = false;
        }

        if (!avoidSync || !componentAlreadyExisted)
        {
            SyncPlayerComponent();
            if (OMSPlayerComponent->OpenOMSFiles(Cast<UOMSFile>(SourceFile), TextureSource, PlayOnOpen))
            {
                SyncPostOpen();
            }
        }
        bInitialized = true;
    }
}

void AHoloSuitePlayer::SyncPlayerComponent()
{
    SCOPE_CYCLE_COUNTER(STAT_HoloSuitePlayer_SyncPlayerComponent);
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: SyncPlayerComponent"));

    if (PlayerType == EPlayerType::AVV)
    {
        if (AVVPlayerComponent == nullptr)
        {
            return;
        }

        AVVPlayerComponent->SyncWithHoloSuitePlayer(this);

        AVVPlayerComponent->OnAVVOpened.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnMediaOpened);
        AVVPlayerComponent->OnAVVOpenFailed.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnMediaOpenFailed);
        AVVPlayerComponent->OnPlaybackResumed.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnPlaybackResumed);
        AVVPlayerComponent->OnPlaybackSuspended.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnPlaybackSuspended);
        AVVPlayerComponent->OnEndReached.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnEndReached);
        AVVPlayerComponent->OnLoop.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnLoop);
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        if (OMSPlayerComponent == nullptr)
        {
            return;
        }

        UMaterialInterface* OMSMeshMaterial = OMSPlayerComponent->GetMeshMaterial();
        if (OMSMeshMaterial)
        {
            MeshMaterial = OMSMeshMaterial;
        }

        OMSPlayerComponent->SyncWithHoloSuitePlayer(this);

        OMSPlayerComponent->OnOMSOpened.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnMediaOpened);
        // OMSPlayerComponent->OnOMSOpenFailed.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnMediaOpenFailed);
        OMSPlayerComponent->OnPlayerReady.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnPlayerReady);
        OMSPlayerComponent->OnPlaybackResumed.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnPlaybackResumed);
        OMSPlayerComponent->OnPlaybackSuspended.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnPlaybackSuspended);
        OMSPlayerComponent->OnEndReached.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnEndReached);
        // OMSPlayerComponent->OnLoop.AddUniqueDynamic(this, &AHoloSuitePlayer::HandleOnLoop);
    }
}

void AHoloSuitePlayer::SyncPostOpen()
{
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: SyncPostOpen"));

    if (PlayerType == EPlayerType::AVV)
    {
        MeshMaterial     = AVVPlayerComponent->GetMeshMaterial();
        bHasSkeletonData = AVVPlayerComponent->HasSkeletonData();
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        MeshMaterial         = OMSPlayerComponent->GetMeshMaterial();
        MediaPlayer          = OMSPlayerComponent->GetMediaPlayer();
        MediaPlayerMaterial  = OMSPlayerComponent->GetMediaPlayerMaterial();
        UseCPUDecoder        = OMSPlayerComponent->GetUseCPUDecoder();
        MaxBufferedSequences = OMSPlayerComponent->GetMaxBufferedSequences();
        bHasSkeletonData     = OMSPlayerComponent->HasSkeletonData();
        RetargetAnimMaterial = OMSPlayerComponent->GetRetargetAnimMaterial();
    }
}

/* Parameter Functions */

void AHoloSuitePlayer::OpenVolumetricFile(UHoloSuiteFile* NewSourceFile)
{
    SCOPE_CYCLE_COUNTER(STAT_HoloSuitePlayer_OpenVolumetricFile);
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: OpenVolumetricFile"));

    if (!NewSourceFile)
    {
        // To Do: provide warning message
        return;
    }

    if (SourceFile == NewSourceFile)
    {
        // To Do: provide warning message
        return;
    }

    SourceFile = NewSourceFile;
    if (PlayerType == EPlayerType::AVV)
    {
        if (AVVPlayerComponent->OpenAVVFile(Cast<UAVVFile>(SourceFile)))
        {
            SyncPostOpen();
        }
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        if (!TextureSource)
        {
            // To Do: provide warning message
            return;
        }

        if (OMSPlayerComponent->OpenOMSFiles(Cast<UOMSFile>(SourceFile), TextureSource, PlayOnOpen))
        {
            SyncPostOpen();
        }
    }
    else
    {
        // To Do: provide warning message
        return;
    }
}

void AHoloSuitePlayer::OpenVolumetricFiles(UHoloSuiteFile* NewSourceFile, UMediaSource* NewTextureSource, bool NewPlayOnOpen)
{
    SCOPE_CYCLE_COUNTER(STAT_HoloSuitePlayer_OpenVolumetricFiles);
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: OpenVolumetricFiles"));

    if (PlayerType == EPlayerType::AVV)
    {
        // To Do: provide warning message
        return;
    }

    if (!NewSourceFile)
    {
        // To Do: provide warning message
        return;
    }

    if (!NewTextureSource)
    {
        // To Do: provide warning message
        return;
    }

    if (SourceFile == NewSourceFile && TextureSource == NewTextureSource)
    {
        // To Do: provide warning message
        return;
    }

    SourceFile = NewSourceFile;
    TextureSource = NewTextureSource;
    PlayOnOpen = NewPlayOnOpen;
    if (PlayerType == EPlayerType::OMS)
    {
        if (OMSPlayerComponent->OpenOMSFiles(Cast<UOMSFile>(SourceFile), TextureSource, PlayOnOpen))
        {
            SyncPostOpen();
        }
    }
    else
    {
        // To Do: provide warning message
        return;
    }
}

void AHoloSuitePlayer::SetMeshMaterial(UMaterialInterface* NewMeshMaterial)
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: SetMeshMaterial"));

    MeshMaterial = NewMeshMaterial;
    if (PlayerType == EPlayerType::AVV)
    {
        AVVPlayerComponent->SetMeshMaterial(MeshMaterial);
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        OMSPlayerComponent->SetMeshMaterial(MeshMaterial);
    }
}

UHoloMeshMaterial* AHoloSuitePlayer::GetMeshMaterial()
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: GetMeshMaterial"));

    if (PlayerType == EPlayerType::AVV)
    {
        return AVVPlayerComponent->GetHoloMaterial();
    }
    if (PlayerType == EPlayerType::OMS)
    {
        return OMSPlayerComponent->GetHoloMaterial();
    }
    UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to attempting to retrieve the player's Mesh Material."));
    return nullptr;
}

void AHoloSuitePlayer::SetOMSPlaybackParameters(bool NewPlayOnOpen, bool NewLoop, bool NewMute, float NewFrameRate)
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: SetOMSPlaybackParameters"));

    if (PlayerType == EPlayerType::OMS)
    {
        PlayOnOpen = NewPlayOnOpen;
        Loop = NewLoop;
        Mute = NewMute;
        FrameRate = NewFrameRate;
        if (OMSPlayerComponent)
        {
            OMSPlayerComponent->SetPlaybackParameters(PlayOnOpen, Loop, Mute, FrameRate);
        }
    }
    else if(PlayerType == EPlayerType::AVV)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: SetOMSPlaybackParameters should only be used for OMS playback. If you wish to configure playback parameters for AVV playback, please use the SetAVVPlaybackParameters function."));
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to setting any parameters."));
    }
}

void AHoloSuitePlayer::SetAVVPlaybackParameters(bool NewExternalTiming, bool NewPlayOnOpen, bool NewLoop, bool NewPingPong, bool NewReverse, float NewFrameRate, float NewCurrentFrame)
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: SetAVVPlaybackParameters"));

    if (PlayerType == EPlayerType::AVV)
    {
        ExternalTiming = NewExternalTiming;
        PlayOnOpen = NewPlayOnOpen;
        Loop = NewLoop;
        PingPong = NewPingPong;
        Reverse = NewReverse;
        FrameRate = NewFrameRate;
        CurrentFrame = NewCurrentFrame;
        if (AVVPlayerComponent)
        {
            AVVPlayerComponent->SetPlaybackParameters(ExternalTiming, PlayOnOpen, Loop, PingPong, Reverse, FrameRate, CurrentFrame);
        }
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: SetAVVPlaybackParameters should only be used for AVV playback. If you wish to configure playback parameters for OMS playback, please use the SetOMSPlaybackParameters function."));
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to setting any parameters."));
    }
}

void AHoloSuitePlayer::SetAVVLODParameters(float NewLOD0ScreenSize, float NewLOD1ScreenSize, float NewLOD2ScreenSize, int NewMinimumLOD, int NewForceLOD)
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: SetAVVLODParameters"));

    if (PlayerType == EPlayerType::AVV)
    {
        LOD0ScreenSize = NewLOD0ScreenSize;
        LOD1ScreenSize = NewLOD1ScreenSize;
        LOD2ScreenSize = NewLOD2ScreenSize;
        MinimumLOD = NewMinimumLOD;
        ForceLOD = NewForceLOD;
        if (AVVPlayerComponent)
        {
            AVVPlayerComponent->SetLODParameters(LOD0ScreenSize, LOD1ScreenSize, LOD2ScreenSize, MinimumLOD, ForceLOD);
        }
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: SetAVVLODParameters should only be used for AVV playback."));
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to setting any parameters."));
    }
}

void AHoloSuitePlayer::SetOMSDecoderParameters(bool NewUseCPUDecoder, int NewNumBufferedSequences)
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: SetOMSDecoderParameters"));

    if (PlayerType == EPlayerType::OMS)
    {
        if (OMSPlayerComponent)
        {
            OMSPlayerComponent->SetDecoderParameters(NewUseCPUDecoder, NewNumBufferedSequences);

            // TODO: warn the user if parameters were overriden?
            UseCPUDecoder = OMSPlayerComponent->GetUseCPUDecoder();
            MaxBufferedSequences = OMSPlayerComponent->GetMaxBufferedSequences();
        }
    }
    else if (PlayerType == EPlayerType::AVV)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: SetOMSDecoderParameters should only be used for OMS playback. If you wish to configure decoder parameters for AVV playback, please use the SetAVVDecoderParameters function."));
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to setting any parameters."));
    }
}

void AHoloSuitePlayer::SetAVVDecoderParameters(bool NewLoadInEditor, int NewPlaybackDelay, bool NewUseCPUDecoder)
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: SetAVVDecoderParameters"));

    if (PlayerType == EPlayerType::AVV)
    {
        LoadInEditor = NewLoadInEditor;
        PlaybackDelay = NewPlaybackDelay;
        UseCPUDecoder = NewUseCPUDecoder;
        if (AVVPlayerComponent)
        {
            AVVPlayerComponent->SetDecoderParameters(LoadInEditor, PlaybackDelay, UseCPUDecoder);
        }
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: SetAVVDecoderParameters should only be used for AVV playback. If you wish to configure decoder parameters for OMS playback, please use the SetOMSRenderParameters function."));
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to setting any parameters."));
    }
}

void AHoloSuitePlayer::SetOMSRenderParameters(bool NewResponsiveAA, bool NewReceiveDecals)
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: SetOMSRenderParameters"));

    if (PlayerType == EPlayerType::OMS)
    {
        ResponsiveAA = NewResponsiveAA;
        ReceiveDecals = NewReceiveDecals;
        if (OMSPlayerComponent)
        {
            OMSPlayerComponent->SetRenderingParameters(ResponsiveAA, ReceiveDecals);
        }
    }
    else if (PlayerType == EPlayerType::AVV)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: SetOMSRenderParameters should only be used for OMS playback. If you wish to configure rendering parameters for AVV playback, please use the SetAVVRenderParameters function."));
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to setting any parameters."));
    }
}

void AHoloSuitePlayer::SetAVVRenderParameters(bool NewMotionVectors, bool NewResponsiveAA, bool NewReceiveDecals)
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: SetAVVRenderParameters"));

    if (PlayerType == EPlayerType::AVV)
    {
        MotionVectors = NewMotionVectors;
        ResponsiveAA = NewResponsiveAA;
        ReceiveDecals = NewReceiveDecals;
        if (AVVPlayerComponent)
        {
            AVVPlayerComponent->SetRenderingParameters(MotionVectors, ResponsiveAA, ReceiveDecals);
        }
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: SetAVVRenderParameters should only be used for AVV playback. If you wish to configure rendering parameters for OMS playback, please use the SetOMSRenderParameters function."));
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to setting any parameters."));
    }
}

void AHoloSuitePlayer::SetSkeletonParameters(bool NewEnableSkeleton, USkeletalMesh* NewPlayerSkeletalMesh)
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: SetSkeletonParameters"));

    if (PlayerType == EPlayerType::AVV)
    {
        EnableSkeleton = NewEnableSkeleton;
        PlayerSkeletalMesh = NewPlayerSkeletalMesh;
        if (AVVPlayerComponent)
        {
            AVVPlayerComponent->SetSkeletonParameters(EnableSkeleton, PlayerSkeletalMesh);
        }
    }
    if (PlayerType == EPlayerType::OMS)
    {
        EnableSkeleton = NewEnableSkeleton;
        PlayerSkeletalMesh = NewPlayerSkeletalMesh;
        if (OMSPlayerComponent)
        {
            OMSPlayerComponent->SetSkeletonParameters(EnableSkeleton, PlayerSkeletalMesh);
        }
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to setting any parameters."));
    }
}

void AHoloSuitePlayer::SetOMSRetargetParameters(bool NewEnableRetargeting, TSubclassOf<UAnimInstance> NewRetargetingAnimation)
{
    if (PlayerType == EPlayerType::OMS)
    {
        if (PlayerSkeletalMesh == nullptr)
        {
            UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: please configure Skeleton parameters before configuring Retarget, using the SetSkeletonParameters function."));
            return;
        }
        EnableRetargeting = NewEnableRetargeting;
        if (EnableRetargeting)
        {
            EnableSkeleton = true;
        }
        RetargetingAnimation = NewRetargetingAnimation;
        if (OMSPlayerComponent)
        {
            OMSPlayerComponent->SetSkeletonParameters(EnableSkeleton, PlayerSkeletalMesh);
            OMSPlayerComponent->SetRetargetParameters(EnableRetargeting, RetargetingAnimation);
        }
    }
    else if (PlayerType == EPlayerType::AVV)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: SetOMSRetargetParameters should only be used for OMS playback. AVV playback does not yet support retargeting."));
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to setting any parameters."));
    }
}

/* Playback Functions */

void AHoloSuitePlayer::Play()
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: Play"));

    if (PlayerType == EPlayerType::AVV)
    {
        AVVPlayerComponent->Play();
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        OMSPlayerComponent->Play();
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to playback."));
    }
}

void AHoloSuitePlayer::Pause()
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: Pause"));

    if (PlayerType == EPlayerType::AVV)
    {
        AVVPlayerComponent->Pause();
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        OMSPlayerComponent->Pause();
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to playback."));
    }
}

void AHoloSuitePlayer::Seek(FTimespan target)
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: Seek"));

    if (PlayerType == EPlayerType::AVV)
    {
        AVVPlayerComponent->Seek(target);
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        OMSPlayerComponent->Seek(target);
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to playback."));
    }
}

/* Skeleton Functions */

USkeletalMeshComponent* AHoloSuitePlayer::GetSkeletalMeshComponent() 
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: GetSkeletalMeshComponent"));

    if (PlayerType == EPlayerType::AVV)
    {
        return AVVPlayerComponent->GetSkeletalMeshComponent();
    }
    else if (PlayerType == EPlayerType::OMS)
    {
        return OMSPlayerComponent->GetSkeletalMeshComponent();
    }

    UE_LOG(LogHoloSuitePlayer, Error, TEXT("HoloSuitePlayer: Please configure your source volumetric asset prior to retrieving its SkeletalMesh."));
    return nullptr;
}

void AHoloSuitePlayer::AttachActorToSkeleton(AActor* Actor, FName SocketName)
{
    UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: AttachActorToSkeleton"));

    ActorsToBeAttached.Emplace(Actor, SocketName);
}

void AHoloSuitePlayer::DetachActorFromSkeleton(AActor* Actor)
{
    if (Actor)
    {
        if (Actor->GetAttachParentActor() != NULL)
        {
            UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: DetachActorFromSkeleton"));
            Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        }
        else
        {
            UE_LOG(LogHoloSuitePlayer, Warning, TEXT("HoloSuitePlayer: The actor you are trying to detach is not attached to another actor"));
        }
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("HoloSuitePlayer: The actor you are trying to detach does not exist"));
    }
}


/* Event Handler Functions */

void AHoloSuitePlayer::HandleOnMediaOpened()
{
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: OnMediaOpened"));

    OnMediaOpened.Broadcast();
}

void AHoloSuitePlayer::HandleOnMediaOpenFailed()
{
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: OnMediaOpenFailed"));

    OnMediaOpenFailed.Broadcast();
}

void AHoloSuitePlayer::HandleOnPlayerReady()
{
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: OnPlayerReady"));

    OnMediaReady.Broadcast();
}

void AHoloSuitePlayer::HandleOnPlaybackResumed()
{
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: OnPlaybackResumed"));

    OnPlaybackResumed.Broadcast();
}

void AHoloSuitePlayer::HandleOnPlaybackSuspended()
{
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: OnPlaybackSuspended"));

    OnPlaybackSuspended.Broadcast();
}

void AHoloSuitePlayer::HandleOnEndReached()
{
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: OnEndReached"));

    OnEndReached.Broadcast();
}

void AHoloSuitePlayer::HandleOnLoop()
{
    //UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: OnLoop"));

    OnLoop.Broadcast();
}