// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "AVV/AVVPlayerComponent.h"
#include "HoloSuitePlayer.h"

/* Profiling Declarations */

DECLARE_CYCLE_STAT(TEXT("AVVPlayerComponent.Constructor"),      STAT_AVVPlayerComponent_Constructor,     STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVPlayerComponent.Destructor"),       STAT_AVVPlayerComponent_Destructor,      STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVPlayerComponent.TickComponent"),    STAT_AVVPlayerComponent_TickComponent,   STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVPlayerComponent.OpenAVVFile"),      STAT_AVVPlayerComponent_OpenAVVFile,     STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVPlayerComponent.Close"),            STAT_AVVPlayerComponent_Close,           STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("AVVPlayerComponent.LoadAVV"),          STAT_AVVPlayerComponent_LoadAVV,         STATGROUP_HoloSuitePlayer);

/* Default Functions */

UAVVPlayerComponent::UAVVPlayerComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVPlayerComponent_Constructor);

    avvDecoder              = nullptr;

    ExternalTiming          = false;
    PlayOnOpen              = true;
    Loop                    = true;
    PingPong                = false;
    Reverse                 = false;
    FrameRate               = 30.0f;
    CurrentFrame            = 0;

    LOD0ScreenSize          = 1.0f;
    LOD1ScreenSize          = 0.5f;
    LOD2ScreenSize          = 0.25f;
    MinimumLOD              = 0;
    ForceLOD                = -1;

    PlaybackDelay           = 0;
    UseCPUDecoder           = false;
    LoadInEditor            = true;

    MotionVectors           = true;
    ResponsiveAA            = false;
    ReceiveDecals           = true;

    EnableSkeleton          = false;

    bFirstRun               = true;
    bShouldPlay             = false;
    bAVVLoaded              = false;
    bHasSkeletonData        = false;
    CurrentEngineFrame      = 0;
    FrameTimer              = 0.0f;

	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
}

UAVVPlayerComponent::~UAVVPlayerComponent()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVPlayerComponent_Destructor);

}

void UAVVPlayerComponent::OnConstruction(const FTransform& Transform)
{
    if (IsCustomPlayer())
    {
        OpenAVVFile(AVVFile);
    }
}

void UAVVPlayerComponent::BeginPlay()
{
	Super::BeginPlay();

    if (IsCustomPlayer())
    {
        OpenAVVFile(AVVFile);
    }
}

void UAVVPlayerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DeleteSkeletalMeshComponent();

    Super::EndPlay(EndPlayReason);
}
#if WITH_EDITOR
void UAVVPlayerComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{

    if (PropertyChangedEvent.Property == nullptr)
    {
        Super::PostEditChangeProperty(PropertyChangedEvent);
        return;
    }

    FString propertyName = PropertyChangedEvent.GetPropertyName().GetPlainNameString();

    if (propertyName == "MeshMaterial")
    {
        SetMeshMaterial(MeshMaterial);
    }

    if (propertyName == "ExternalTiming" || propertyName == "PlayOnOpen" || propertyName == "Loop"
        || propertyName == "PingPong" || propertyName == "Reverse"
        || propertyName == "FrameRate" || propertyName == "CurrentFrame")
    {
        SetPlaybackParameters(ExternalTiming, PlayOnOpen, Loop, PingPong, Reverse, FrameRate, CurrentFrame);
    }

    if (propertyName == "LOD0ScreenSize" || propertyName == "LOD1ScreenSize" || propertyName == "LOD2ScreenSize"
        || propertyName == "MinimumLOD" || propertyName == "ForceLOD")
    {
        SetLODParameters(LOD0ScreenSize, LOD1ScreenSize, LOD2ScreenSize, MinimumLOD, ForceLOD);
    }

    if (propertyName == "NumBufferedSequences" || propertyName == "LoadInEditor" || propertyName == "PlaybackDelay" || propertyName == "UseCPUDecoder")
    {
        SetDecoderParameters(LoadInEditor, PlaybackDelay, UseCPUDecoder);
    }

    if (propertyName == "MotionVectors" || propertyName == "ResponsiveAA" || propertyName == "ReceiveDecals")
    {
        SetRenderingParameters(MotionVectors, ResponsiveAA, ReceiveDecals);
    }

    if (propertyName == "EnableSkeleton" || propertyName == "PlayerSkeletalMesh")
    {
        SetSkeletonParameters(EnableSkeleton, PlayerSkeletalMesh);
    }

    RefreshFrame();

    Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UAVVPlayerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVPlayerComponent_TickComponent);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!avvDecoder || !bAVVLoaded)
    {
        if (!IsCustomPlayer() || !OpenAVVFile(AVVFile))
        {
            return;
        }
    }

    UWorld* world = GetWorld();
    if (!GIsEditor || (world != nullptr && world->IsPlayInEditor()))
    {
        // -- Playback only tick functions --

        if (!ExternalTiming && (CurrentEngineFrame >= PlaybackDelay))
        {
            if (bShouldPlay || (PlayOnOpen && bFirstRun))
            {
                UpdateFrame(DeltaTime);
            }
        }
        else
        {
            int frame = FMath::Clamp((int)CurrentFrame, 0, avvDecoder->FrameCount - 1);
            avvDecoder->SetFrame(frame);
        }
    }
    else
    {
        // -- Editor only tick functions --

        int frame = FMath::Clamp((int)CurrentFrame, 0, avvDecoder->FrameCount - 1);
        avvDecoder->SetFrame(frame);
    }

    avvDecoder->Update(DeltaTime);

    // Attach new Actors to Skeleton if any
    // We do this here to allow AttachActorToSkeleton() to be called even before the SkeletalMeshComponent has been generated
    // And it's after Update() to ensure the first call to this function occurs and provide the opportunity for the Actors to not be attached to a garbage bone position
    if (ActorsToBeAttached.Num() > 0 && PlayerSkeletalMeshComponent)
    {
        FAttachmentTransformRules rules(EAttachmentRule::KeepWorld, true);
        for (auto& Entry : ActorsToBeAttached)
        {
            Entry.Key->AttachToComponent(PlayerSkeletalMeshComponent, rules, Entry.Value);
        }
        ActorsToBeAttached.Empty();
    }

    CurrentEngineFrame++;
}

/* Parameter Functions */

void UAVVPlayerComponent::SyncWithHoloSuitePlayer(AHoloSuitePlayer* HoloSuitePlayer)
{
    AVVFile             = Cast<UAVVFile>(HoloSuitePlayer->SourceFile);
    MeshMaterial        = HoloSuitePlayer->MeshMaterial;
    ExternalTiming      = HoloSuitePlayer->ExternalTiming;
    PlayOnOpen          = HoloSuitePlayer->PlayOnOpen;
    Loop                = HoloSuitePlayer->Loop;
    PingPong            = HoloSuitePlayer->PingPong;
    Reverse             = HoloSuitePlayer->Reverse;
    FrameRate           = HoloSuitePlayer->FrameRate;
    CurrentFrame        = HoloSuitePlayer->CurrentFrame;
    LOD0ScreenSize      = HoloSuitePlayer->LOD0ScreenSize;
    LOD1ScreenSize      = HoloSuitePlayer->LOD1ScreenSize;
    LOD2ScreenSize      = HoloSuitePlayer->LOD2ScreenSize;
    MinimumLOD          = HoloSuitePlayer->MinimumLOD;
    ForceLOD            = HoloSuitePlayer->ForceLOD;
    LoadInEditor        = HoloSuitePlayer->LoadInEditor;
    PlaybackDelay       = HoloSuitePlayer->PlaybackDelay;
    UseCPUDecoder       = HoloSuitePlayer->UseCPUDecoder;
    MotionVectors       = HoloSuitePlayer->MotionVectors;
    ResponsiveAA        = HoloSuitePlayer->ResponsiveAA;
    ReceiveDecals       = HoloSuitePlayer->ReceiveDecals;
    EnableSkeleton      = HoloSuitePlayer->EnableSkeleton;
    PlayerSkeletalMesh  = HoloSuitePlayer->PlayerSkeletalMesh;

    DefaultMeshMaterial = HoloSuitePlayer->GetDefaultMeshMaterial();
}

bool UAVVPlayerComponent::OpenAVVFile(UAVVFile* NewAVV)
{
    SCOPE_CYCLE_COUNTER(STAT_AVVPlayerComponent_OpenAVVFile);

    AVVFile = NewAVV;

    UWorld* world = GetWorld();
    if (!GIsEditor || (world != nullptr && world->IsPlayInEditor()) || LoadInEditor)
    {
        return LoadAVV();
    }
    return false;
}

void UAVVPlayerComponent::Close()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVPlayerComponent_Close);

    UnloadAVV();
}

bool UAVVPlayerComponent::LoadAVV()
{
    SCOPE_CYCLE_COUNTER(STAT_AVVPlayerComponent_LoadAVV);

    UnloadAVV();

    bAVVLoaded = false;

    FrameTimer = 0.0f;
    CurrentEngineFrame = 0;
    bFirstRun = true;

    UWorld* world = GetWorld();
    if (!GIsEditor || (world != nullptr && world->IsPlayInEditor()))
    {
        CurrentFrame = 0;
    }

    if (avvDecoder == nullptr)
    {
        if (UseCPUDecoder)
        {
            avvDecoder = (UAVVDecoder*)NewObject<UAVVDecoderCPU>(this, TEXT("AVVPlayerCPU"));
        }
        else
        {
            avvDecoder = (UAVVDecoder*)NewObject<UAVVDecoderCompute>(this, TEXT("AVVPlayerCompute"));
        }
    }

    // Initialize component
    if (GetWorld())
    {
        avvDecoder->RegisterComponent();
    }
    avvDecoder->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);

    avvDecoder->SetRenderingOptions(MotionVectors, ResponsiveAA, ReceiveDecals);
    avvDecoder->SetLODOptions({ LOD0ScreenSize, LOD1ScreenSize, LOD2ScreenSize }, MinimumLOD, ForceLOD);

    // Set Mesh Material
    if (!MeshMaterial)
    {
        if (DefaultMeshMaterial == nullptr)
        {
            UE_LOG(LogHoloSuitePlayer, Error, TEXT("AVVPlayerComponent: failed to load player, no default Mesh Material was found."));
            OnAVVOpenFailed.Broadcast();
            return false;
        }
        MeshMaterial = DefaultMeshMaterial;
    }

    // Open file.
    bAVVLoaded = avvDecoder->OpenAVV(AVVFile, MeshMaterial);

    if (bAVVLoaded)
    {
        const UHoloSuitePlayerSettings* avvSettings = GetDefault<UHoloSuitePlayerSettings>(UHoloSuitePlayerSettings::StaticClass());

        // Apply settings.
        GHoloMeshManager.Configure(avvSettings->FrameUpdateLimit, avvSettings->FrustumCulling, avvSettings->ImmediateMode);
        avvDecoder->Configure(avvSettings->ImmediateMode);
        avvDecoder->SetCachingDirection(Reverse);

        // Determine if actors can be attached (i.e. source has skeleton data)
        bHasSkeletonData = avvDecoder->HasSkeletonData();

        if (bHasSkeletonData && EnableSkeleton)
        {
            CreateSkeletalMeshComponent(true);
            avvDecoder->SetHoloMeshSkeleton(PlayerSkeletalMeshComponent);
        }
        else
        {
            avvDecoder->SetHoloMeshSkeleton(nullptr);
        }

        // Load first frame so we display something.
        avvDecoder->SetFrame(0, true);
        OnAVVOpened.Broadcast();
        return true;
    }
    else
    {
        OnAVVOpenFailed.Broadcast();
        return false;
    }
}

void UAVVPlayerComponent::UnloadAVV()
{
    if (avvDecoder == nullptr)
    {
        return;
    }

    if (bAVVLoaded)
    {
        avvDecoder->Close();
    }

    avvDecoder->DestroyComponent();
    avvDecoder = nullptr;
}

void UAVVPlayerComponent::SetMeshMaterial(UMaterialInterface* NewMeshMaterial)
{
    MeshMaterial = NewMeshMaterial;
    if (bAVVLoaded)
    {
        avvDecoder->SetMeshMaterial(MeshMaterial);
    }
    else if (AVVFile && MeshMaterial)
    {
        LoadAVV();
    }
}

void UAVVPlayerComponent::SetPlaybackParameters(bool NewExternalTiming, bool NewPlayOnOpen, bool NewLoop, bool NewPingPong, bool NewReverse, float NewFrameRate, float NewCurrentFrame)
{
    ExternalTiming = NewExternalTiming;
    PlayOnOpen = NewPlayOnOpen;
    Loop = NewLoop;
    PingPong = NewPingPong;
    Reverse = NewReverse;

    if (NewFrameRate > 0.0f)
    {
        FrameRate = NewFrameRate;
    }

    if (NewCurrentFrame >= 0)
    {
        CurrentFrame = NewCurrentFrame;
    }

    if (avvDecoder)
    {
        avvDecoder->SetCachingDirection(Reverse);
    }

    bShouldPlay = false;
}

void UAVVPlayerComponent::SetLODParameters(float NewLOD0ScreenSize, float NewLOD1ScreenSize, float NewLOD2ScreenSize, int NewMinimumLOD, int NewForceLOD)
{
    LOD0ScreenSize = NewLOD0ScreenSize;
    LOD1ScreenSize = NewLOD1ScreenSize;
    LOD2ScreenSize = NewLOD2ScreenSize;
    MinimumLOD = NewMinimumLOD;
    ForceLOD = NewForceLOD;

    if (avvDecoder != nullptr)
    {
        avvDecoder->SetLODOptions({ LOD0ScreenSize, LOD1ScreenSize, LOD2ScreenSize }, MinimumLOD, ForceLOD);
    }
}

void UAVVPlayerComponent::SetDecoderParameters(bool NewLoadInEditor, int NewPlaybackDelay, bool NewUseCPUDecoder)
{
    LoadInEditor = NewLoadInEditor;
    PlaybackDelay = NewPlaybackDelay;
    UseCPUDecoder = NewUseCPUDecoder;

    UWorld* world = GetWorld();
    if (!GIsEditor || (world != nullptr && world->IsPlayInEditor()) || LoadInEditor)
    {
        LoadAVV();
    }
}

void UAVVPlayerComponent::SetRenderingParameters(bool NewMotionVectors, bool NewResponsiveAA, bool NewReceiveDecals)
{
    MotionVectors = NewMotionVectors;
    ResponsiveAA = NewResponsiveAA;
    ReceiveDecals = NewReceiveDecals;

    if (avvDecoder != nullptr)
    {
        avvDecoder->SetRenderingOptions(MotionVectors, ResponsiveAA, ReceiveDecals);
    }
}

void UAVVPlayerComponent::SetSkeletonParameters(bool NewEnableSkeleton, USkeletalMesh* NewPlayerSkeletalMesh)
{
    if (EnableSkeleton == NewEnableSkeleton && PlayerSkeletalMesh == NewPlayerSkeletalMesh)
    {
        return;
    }

    EnableSkeleton = NewEnableSkeleton;
    PlayerSkeletalMesh = NewPlayerSkeletalMesh;

    if (EnableSkeleton)
    {
        CreateSkeletalMeshComponent(true);
    }
    else
    {
        PlayerSkeletalMeshComponent = nullptr;
    }
    avvDecoder->SetHoloMeshSkeleton(PlayerSkeletalMeshComponent);
}

void UAVVPlayerComponent::UpdateFrame(float DeltaTime)
{
    FrameTimer += DeltaTime;
    int computedFrame = 0;

    if (Reverse)
    {
        computedFrame = avvDecoder->FrameCount - FrameTimer / (1.0f / FrameRate);
        if (computedFrame < 0)
        {
            FrameTimer -= avvDecoder->FrameCount * (1.0f / FrameRate);
            if (PingPong)
            {
                computedFrame = 0 + computedFrame;
                Reverse = false;
                avvDecoder->SetCachingDirection(Reverse);
            }
            else
            {
                computedFrame = avvDecoder->FrameCount - computedFrame;

                if (Loop)
                {
                    OnLoop.Broadcast();
                }
                else
                {
                    bShouldPlay = false;
                    OnEndReached.Broadcast();
                }
            }
        }
    }
    else
    {
        computedFrame = FrameTimer / (1.0f / FrameRate);
        if (computedFrame >= avvDecoder->FrameCount)
        {
            FrameTimer -= avvDecoder->FrameCount * (1.0f / FrameRate);
            if (PingPong)
            {
                computedFrame = 0 + computedFrame;
                Reverse = true;
                avvDecoder->SetCachingDirection(Reverse);
            }
            else
            {
                computedFrame = avvDecoder->FrameCount - computedFrame;

                if (Loop)
                {
                    OnLoop.Broadcast();
                }
                else
                {
                    bShouldPlay = false;
                    OnEndReached.Broadcast();
                }
            }
        }
    }

    if (CurrentFrame != computedFrame)
    {
        CurrentFrame = FMath::Clamp(computedFrame, 0, avvDecoder->FrameCount - 1);

        if (bFirstRun)
        {
            OnPlaybackResumed.Broadcast();
            bShouldPlay = true;
            bFirstRun = false;
        }

        avvDecoder->SetFrame(CurrentFrame);
    }
}


void UAVVPlayerComponent::RefreshFrame()
{
    if (avvDecoder != nullptr)
    {
        int frame = FMath::Clamp((int)CurrentFrame, 0, avvDecoder->FrameCount - 1);
        avvDecoder->SetFrame(frame, true);
    }
}

/* Playback Functions */

void UAVVPlayerComponent::Play()
{
    bShouldPlay = true;
    bFirstRun = false;
    OnPlaybackResumed.Broadcast();
}

void UAVVPlayerComponent::Pause()
{
    bShouldPlay = false;
    OnPlaybackSuspended.Broadcast();
}

void UAVVPlayerComponent::Seek(FTimespan target)
{
    if (target.GetSeconds() * FrameRate > avvDecoder->FrameCount || target.GetSeconds() * FrameRate < 0)
    {
        // print warning message
        return;
    }
    FrameTimer = target.GetSeconds() * FrameRate;
    CurrentFrame = FrameTimer;
}

/* Skeleton Functions */

void UAVVPlayerComponent::AttachActorToSkeleton(AActor* Actor, FName SocketName)
{
    ActorsToBeAttached.Emplace(Actor, SocketName);
}

void UAVVPlayerComponent::CreateSkeletalMeshComponent(bool shouldDeleteFirst)
{
    if (PlayerSkeletalMesh)
    {
        if (shouldDeleteFirst)
        {
            DeleteSkeletalMeshComponent();
        }

        FAttachmentTransformRules rules(EAttachmentRule::KeepWorld, true);
        PlayerSkeletalMeshComponent = NewObject<USkeletalMeshComponent>(this);

#if (ENGINE_MAJOR_VERSION >= 5) && (ENGINE_MINOR_VERSION >= 1)
        PlayerSkeletalMeshComponent->SetSkeletalMesh(PlayerSkeletalMesh);
#else
        PlayerSkeletalMeshComponent->SkeletalMesh = PlayerSkeletalMesh;
#endif

        PlayerSkeletalMeshComponent->SetWorldTransform(GetOwner()->GetTransform());
        if (GetWorld())
        {
            PlayerSkeletalMeshComponent->RegisterComponent();
        }
        PlayerSkeletalMeshComponent->AttachToComponent(this, rules);
    }
    else
    {
        UE_LOG(LogHoloSuitePlayer, Display, TEXT("HoloSuitePlayer: AVVPlayerComponent: The ability to attach actors to the player's skeleton requires the assignment of a valid Skeletal Mesh and one wasn't provided. Disabling Skeleton."));
        EnableSkeleton = false;
    }
}

void UAVVPlayerComponent::DeleteSkeletalMeshComponent()
{
    if (PlayerSkeletalMeshComponent)
    {
        FDetachmentTransformRules rules(FDetachmentTransformRules::KeepWorldTransform);
        TArray<USceneComponent*> attachedChildren = PlayerSkeletalMeshComponent->GetAttachChildren();
        for (int i = 0; i < attachedChildren.Num(); i++)
        {
            attachedChildren[i]->DetachFromComponent(rules);
        }
        PlayerSkeletalMeshComponent->DetachFromComponent(rules);
        PlayerSkeletalMeshComponent->DestroyComponent();
    }
}

bool UAVVPlayerComponent::IsCustomPlayer()
{
    if (Cast<AHoloSuitePlayer>(GetOwner()))
    {
        return false;
    }
    return true;
}