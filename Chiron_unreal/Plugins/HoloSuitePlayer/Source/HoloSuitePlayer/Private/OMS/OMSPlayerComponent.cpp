// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "OMS/OMSPlayerComponent.h"
#include "HoloSuitePlayer.h"

/* Profiling Declarations */

DECLARE_CYCLE_STAT(TEXT("OMSPlayerComponent.TickComponent"),            STAT_OMSPlayerComponent_TickComponent,           STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSPlayerComponent.OpenOMSFiles"),             STAT_OMSPlayerComponent_OpenOMSFiles,            STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSPlayerComponent.Close"),                    STAT_OMSPlayerComponent_Close,                   STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSPlayerComponent.LoadSequence"),             STAT_OMSPlayerComponent_LoadSequence,            STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSPlayerComponent.LoadSequenceFrame"),        STAT_OMSPlayerComponent_LoadSequenceFrame,       STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSPlayerComponent.LoadMediaPlayer"),          STAT_OMSPlayerComponent_LoadMediaPlayer,         STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSPlayerComponent.SetFrame"),                 STAT_OMSPlayerComponent_SetFrame,                STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSPlayerComponent.TrySetFrame"),              STAT_OMSPlayerComponent_TrySetFrame,             STATGROUP_HoloSuitePlayer);
DECLARE_CYCLE_STAT(TEXT("OMSPlayerComponent.PrepareSkeletonManager"),   STAT_OMSPlayerComponent_PrepareSkeletonManager,  STATGROUP_HoloSuitePlayer);

/* Default Functions */

UOMSPlayerComponent::UOMSPlayerComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    Decoder                             = nullptr;

    PlayOnOpen                          = true;
    Loop                                = true;
    Mute                                = false;
    FrameRate                           = 30.0f;
    MaxBufferedSequences                = -1;
    ResponsiveAA                        = false;
    ReceiveDecals                       = true;
    EnableSkeleton                      = false;
    EnableRetargeting                   = false;

    bSequenceUpdated                    = false;
    bFrameUpdated                       = false;
    bIsPlaying                          = false;

    bHasSkeletonData                    = false;
    bLoadedFirstFrame                   = false;
    bPlayerReady                        = false;
    activeSequence                      = -1;
    activeFrame                         = -1;
    frameTimer                          = 0.0f;
    sourceFrameRate                     = -1.0f;
    currentFrameRate                    = -1.0f;
    lastDecodedFrameNumber              = -1;
    lastSkippedFrameNumber              = -1;
    FrameCount                          = -1;

    PrimaryComponentTick.bCanEverTick   = true;
    bTickInEditor                       = true;

    // Load global settings to get defaults.
    const UHoloSuitePlayerSettings* settings = GetDefault<UHoloSuitePlayerSettings>(UHoloSuitePlayerSettings::StaticClass());
    if (settings != nullptr)
    {
        MaxBufferedSequences = settings->MaxBufferedSequences;
    }
}

UOMSPlayerComponent::~UOMSPlayerComponent()
{
    UnloadOMS();
}

void UOMSPlayerComponent::OnConstruction(const FTransform& Transform)
{
    if (IsCustomPlayer())
    {
        OpenOMSFiles(OMS, TextureSource, PlayOnOpen);
    }
}

void UOMSPlayerComponent::BeginPlay()
{
    Super::BeginPlay();
    if (IsCustomPlayer())
    {
        OpenOMSFiles(OMS, TextureSource, PlayOnOpen);
    }
}

void UOMSPlayerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DeleteMediaSound();

    if (SkeletonManager)
    {
        delete SkeletonManager;
        SkeletonManager = nullptr;
    }
    DeletePlayerSkeletalMesh();

    Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void UOMSPlayerComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    if (PropertyChangedEvent.Property == nullptr)
    {
        Super::PostEditChangeProperty(PropertyChangedEvent);
        return;
    }

    FString propertyName = PropertyChangedEvent.GetPropertyName().GetPlainNameString();

    if (propertyName == "MeshMaterial")
    {
        if (Decoder)
        {
            Decoder->LoadMeshMaterial(MeshMaterial);
        }
    }

    if (propertyName == "Mute")
    {
        SetPlaybackParameters(PlayOnOpen, Loop, Mute, FrameRate);
    }

    if (propertyName == "ResponsiveAA" || propertyName == "ReceiveDecals")
    {
        if (Decoder)
        {
            Decoder->SetRenderingOptions(false, ResponsiveAA, ReceiveDecals);
        }
    }

    if (propertyName == "EnableSkeleton" || propertyName == "PlayerSkeletalMesh")
    {
        SetSkeletonParameters(EnableSkeleton, PlayerSkeletalMesh);
    }

    if (propertyName == "EnableRetargeting" || propertyName == "RetargetingAnimation")
    {
        SetRetargetParameters(EnableRetargeting, RetargetingAnimation);
    }

    if (propertyName == "RetargetAnimMaterial")
    {
        SetRetargetingAnimationMaterial(RetargetAnimMaterial);
    }

    Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UOMSPlayerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSPlayerComponent_TickComponent);

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (OMS == nullptr || TextureSource == nullptr || MeshMaterial == nullptr || MediaPlayer == nullptr)
    {
        return;
    }

#if WITH_EDITOR
    VerifyHoloSuitePlayer();
#endif

    if (MediaSoundComponent != nullptr)
    {
        MediaSoundComponent->UpdatePlayer();
    }

    if (Decoder != nullptr)
    {
        if (FrameCount < 0)
        {
            FrameCount = Decoder->GetFrameCount();
        }

        // Update Decoder
        Decoder->Update();
    }

    // There's a bug in MediaPlayer::SetRate() that if it's called before or after OpenSource() or even right after the OnMediaOpened event, it doesn't work.
    // So this is a hack to ensure that SetPlaybackSpeed() is applied successfuly.
    if (sourceFrameRate > 0.0f && currentFrameRate != FrameRate)
    {
        float currentRate = MediaPlayer->GetRate();
        if (currentRate > 0.0f)
        {
            float expectedRate = abs(FrameRate / sourceFrameRate);
            if (currentRate != expectedRate)
            {
#if ENGINE_MAJOR_VERSION == 5
                // Electra Media Player (used in UE5+) doesn't play nice with SupportsRate() and SetRate() (it believes it only supports 0.0 and 1.0, although other rates like 0.5 and 2.0 also work)
                MediaPlayer->SetRate(expectedRate);
#else
                if (!MediaPlayer->SupportsRate(expectedRate, false) || !MediaPlayer->SetRate(expectedRate))
                {
                    UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSPlayerComponent: MediaPlayer does not support custom frame rate provided. Resetting frame rate to TextureSource's original frame rate %f."), sourceFrameRate);
                    FrameRate = sourceFrameRate;
                }
#endif
                currentFrameRate = FrameRate;
            }
            else
            {
                currentFrameRate = FrameRate;
            }
        }
    }

    if (!Decoder)
    {
        if (!IsCustomPlayer() || !OpenOMSFiles(OMS, TextureSource, PlayOnOpen))
        {
            return;
        }
    }

    if (Decoder->IsNewFrameReady())
    {
        int newFrameNumber = Decoder->GetNewFrameNumber();
        //UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSPlayerComponent: NewFrameNumber: %d"), newFrameNumber);

        bool skipFrame = false;
        if (newFrameNumber < 0 || newFrameNumber >= FrameCount)
        {
            skipFrame = true;
        }
        else
        {
            // If lastDecodedFrameNumber = -1 then we just started the decoder
            // it's possible we have a texture left in there from last playback.
            std::pair<int, int> newFrame = Decoder->GetFrameFromLookupTable(newFrameNumber);
            if (newFrame.first < 0 || (lastDecodedFrameNumber == -1 && newFrame.first > activeSequence))
            {
                // If the first frame we decode isn't part of the first sequence we're looking for 
                // there's a good chance this is a bogus first read, we'll just skip it.
                // If we're wrong then 1 frame is skipped at the beginning of the sequence
                // but also we're already in frame dropping territory if the first decoded
                // frame number isn't part of the first active sequence.

                skipFrame = true;
                lastSkippedFrameNumber = newFrameNumber;
            }
        }

        if (skipFrame || newFrameNumber == lastSkippedFrameNumber)
        {
            UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSPlayerComponent: Skipping frame %d. Active Sequence: %d"), newFrameNumber, activeSequence);
            Decoder->GetFrame();
            lastDecodedFrameNumber = newFrameNumber;
            return;
        }

        // Attempt to update the mesh. If the sequence is not available we'll try again next tick.
        if (TrySetFrame(newFrameNumber))
        {
            // Skeleton + Retargeting.
            if (SkeletonManager && activeSequence > -1 && activeFrame > -1)
            {
                oms_sequence_t* sequence = DecodedSequence->sequence;
                if (sequence != nullptr)
                {
                    std::pair<int, int> newFrame = Decoder->GetFrameFromLookupTable(newFrameNumber);
                    if (newFrame.second >= 0 && EnableSkeleton)
                    {
                        // Update Skeleton
                        bool skelUpdated = SkeletonManager->UpdateSkeleton(sequence, newFrameNumber, newFrame.second);

                        // Update Retargeted Mesh
                        if (EnableRetargeting && skelUpdated)
                        {
                            SkeletonManager->UpdateRetargetMesh(Decoder->GetHoloMesh(bSequenceUpdated));
                            Decoder->UpdateMeshMaterial(bSequenceUpdated, false, false, true, false, 0.0f);

                            // SkeletalMeshComponent's animation must be reinitiated when assigning its material
                            if (PlayerSkeletalMeshComponent->GetMaterial(0) == nullptr && RetargetAnimMaterial != nullptr)
                            {
                                PlayerSkeletalMeshComponent->SetMaterial(0, RetargetAnimMaterial);
                                PlayerSkeletalMeshComponent->InitAnim(true);
                            }
                        }

                        // Attach new Actors to Skeleton if any
                        // We do this here to allow AttachActorToSkeleton() to be called even before the SkeletalMeshComponent has been generated
                        // And it's after UpdateSkeleton() to ensure the first call to this function occurs and the Actors are not attached to a garbage bone position
                        if (ActorsToBeAttached.Num() > 0)
                        {
                            FAttachmentTransformRules rules(EAttachmentRule::KeepWorld, true);
                            for (auto& Entry : ActorsToBeAttached)
                            {
                                Entry.Key->AttachToComponent(PlayerSkeletalMeshComponent, rules, Entry.Value);
                            }
                            ActorsToBeAttached.Empty();
                        }
                    }
                }
            }

            // Update Mesh Material with latest frame texture.
            if (bFrameUpdated)
            {
                Decoder->UpdateMeshMaterial(bSequenceUpdated, true, false, false, false, 0.0f);
                
                if (bSequenceUpdated)
                {
                    // Swap WriteMesh with ReadMesh so it will be rendered next.
                    Decoder->SwapHoloMesh();
                }

                bSequenceUpdated = false;
                bFrameUpdated = false;
            }

            // SSDR.
            // Note: does this make sense after Swap is called?
            oms_sequence_t* sequence = DecodedSequence->sequence;
            if (sequence)
            {
                float ssdrEnabled = (sequence->ssdr_frame_count > 1 && sequence->ssdr_bone_count > 0) == true ? 1.0f : 0.0f;
                Decoder->UpdateMeshMaterial(bSequenceUpdated, false, false, false, true, ssdrEnabled);
            }
        }

        lastDecodedFrameNumber = newFrameNumber;
        lastSkippedFrameNumber = -1;
    }

    frameTimer += DeltaTime;
    if (frameTimer > (1.0f / FrameRate))
    {
        // Tell the frame decoder to decode the next frame.
#if WITH_EDITOR
        Decoder->DecodeFrameNumber();
#else 
        UWorld* world = GetWorld();
        if (world && world->bBegunPlay)
        {
            Decoder->DecodeFrameNumber();
        }
#endif
        frameTimer = 0.0f;
    }

    if (activeSequence == -1 && activeFrame == -1)
    {
        SetFrame(0);
        bLoadedFirstFrame = true;
    }
}

/* Parameter Functions */

void UOMSPlayerComponent::SyncWithHoloSuitePlayer(AHoloSuitePlayer* HoloSuitePlayer)
{
    OMS                         = Cast<UOMSFile>(HoloSuitePlayer->SourceFile);
    TextureSource               = HoloSuitePlayer->TextureSource;
    MeshMaterial                = HoloSuitePlayer->MeshMaterial;
    MediaPlayer                 = HoloSuitePlayer->MediaPlayer;
    MediaPlayerMaterial         = HoloSuitePlayer->MediaPlayerMaterial;
    PlayOnOpen                  = HoloSuitePlayer->PlayOnOpen;
    Loop                        = HoloSuitePlayer->Loop;
    Mute                        = HoloSuitePlayer->Mute;
    FrameRate                   = HoloSuitePlayer->FrameRate;
    UseCPUDecoder               = HoloSuitePlayer->UseCPUDecoder;
    MaxBufferedSequences        = HoloSuitePlayer->MaxBufferedSequences;
    ResponsiveAA                = HoloSuitePlayer->ResponsiveAA;
    ReceiveDecals               = HoloSuitePlayer->ReceiveDecals;
    EnableSkeleton              = HoloSuitePlayer->EnableSkeleton;
    EnableRetargeting           = HoloSuitePlayer->EnableRetargeting;
    PlayerSkeletalMesh          = HoloSuitePlayer->PlayerSkeletalMesh;
    RetargetingAnimation        = HoloSuitePlayer->RetargetingAnimation;

    DefaultMeshMaterial         = HoloSuitePlayer->GetDefaultMeshMaterial();
    DefaultMediaPlayerMaterial  = HoloSuitePlayer->GetDefaultMediaPlayerMaterial();
    DefaultRetargetAnimMaterial = HoloSuitePlayer->GetDefaultRetargetAnimMaterial();

    DeleteMediaSound();
}

void UOMSPlayerComponent::Close()
{
    SCOPE_CYCLE_COUNTER(STAT_OMSPlayerComponent_Close);

    if (Decoder != nullptr)
    {
        Decoder->DestroyComponent();
        Decoder = nullptr;
    }
}

bool UOMSPlayerComponent::OpenOMSFiles(UOMSFile* NewOMS, UMediaSource* NewTextureSource, bool NewPlayOnOpen)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSPlayerComponent_OpenOMSFiles);

    if (NewOMS == nullptr || NewTextureSource == nullptr)
    {
        return false;
    }

    OMS = NewOMS;
    TextureSource = NewTextureSource;
    PlayOnOpen = NewPlayOnOpen;
    bPlayerReady = false;

    if (MeshMaterial == nullptr)
    {
        if (DefaultMeshMaterial == nullptr)
        {
            UE_LOG(LogHoloSuitePlayer, Error, TEXT("OMSPlayerComponent: failed to load player, no default Mesh Material was found."));
            return false;
        }
        MeshMaterial = DefaultMeshMaterial;
    }

    UnloadOMS();

    // Initialize component
    if (!Decoder)
    {
        Decoder = NewObject<UOMSDecoder>(this, TEXT("OMSDecoder"));
        if (GetWorld())
        {
            Decoder->RegisterComponent();
        }
        Decoder->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
    }

    if (!Decoder->OpenOMS(OMS, MeshMaterial))
    {
        return false;
    }
    Decoder->Configure(this, UseCPUDecoder, MaxBufferedSequences);

    LoadMediaPlayer();

    // Check if source has skeleton data
    PrepareSkeletonManager();

    Decoder->SetRenderingOptions(false, ResponsiveAA, ReceiveDecals);

    OnOMSOpened.Broadcast();
    return true;
}

void UOMSPlayerComponent::UnloadOMS()
{
    activeSequence = -1;
    activeFrame = -1;
    bLoadedFirstFrame = false;

    FrameCount = -1;
    frameTimer = 0.0f;
    lastDecodedFrameNumber = -1;
    lastSkippedFrameNumber = -1;

    if (SkeletonManager)
    {
        SkeletonManager->Reset();
    }

    if (Decoder)
    {
        Decoder->Close();
    }

    DecodedSequence = nullptr;
}

bool UOMSPlayerComponent::LoadSequence(int index, bool waitForSequence)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSPlayerComponent_LoadSequence);

    if (!GetWorld()) // If actor is a subclass, GetWorld returns null when changing OMS property, so load sequence should be skipped.
    {
        return false;
    }

    if (DecodedSequence.IsValid() && DecodedSequence->sequenceIndex == index)
    {
        return true;
    }

    FDecodedOMSSequenceRef decodedQuery = Decoder->GetSequence(index, waitForSequence);
    if (!decodedQuery.IsValid())
    {
        return false;
    }

    DecodedSequence = decodedQuery;

    // Sequence updates are double buffered. Whichever mesh is the 
    // ReadIndex mesh is what's currently rendering. Here we fetch
    // WriteIndex mesh and pass in the sequence render data.
    FHoloMesh* writeMesh = Decoder->GetHoloMesh(true);

    // Update bounding box.
    writeMesh->LocalBox = DecodedSequence->holoMesh->LocalBox;

    // Update buffers.
    writeMesh->UpdateFromSource(DecodedSequence->holoMesh);

    ERHIFeatureLevel::Type FeatureLevel;
    if (GetWorld())
    {
        FeatureLevel = GetWorld()->Scene->GetFeatureLevel();
    }
    else
    {
        FeatureLevel = ERHIFeatureLevel::Num;
    }
    writeMesh->InitOrUpdate(FeatureLevel);
    writeMesh->Update();
    Decoder->UpdateHoloMesh();

    // This should be the only place that sets this variable other than construct/unload.
    activeSequence = index;
    return true;
}

bool UOMSPlayerComponent::LoadSequenceFrame(int index, bool sequenceUpdated)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSPlayerComponent_LoadSequenceFrame);

    if (activeSequence < 0 || !DecodedSequence.IsValid())
    {
        return false;
    }

    if (DecodedSequence->sequenceIndex != activeSequence)
    {
        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSPlayerComponent: Sequence frame not ready for playback: %d"), activeSequence);
        return false;
    }

    activeFrame = index;

    // If the sequence was updated we target the WriteIndex mesh.
    FHoloMesh* holoMesh = Decoder->GetHoloMesh(sequenceUpdated);

    if (!holoMesh->SSDRBoneTexture.IsValid())
    {
        holoMesh->SSDRBoneTexture.Create(512);
        holoMesh->SSDRBoneTexture.SetToIdentity();
    }

    // Upload SSDR data.
    if (DecodedSequence->sequence->ssdr_frame_count > 1)
    {
        holoMesh->SSDRBoneTexture.SetData(0, DecodedSequence->sequence->ssdr_bone_count * 4, DecodedSequence->sequence->ssdr_frames[activeFrame].matrices[0].m);
        holoMesh->SSDRBoneTexture.Update();
    }
    else 
    {
        holoMesh->SSDRBoneTexture.SetToIdentity();
    }

    if (holoMesh->Material != nullptr)
    {
        Decoder->UpdateMeshMaterial(sequenceUpdated, false, true, false, false, 0.0f);
    }

    return true;
}

void UOMSPlayerComponent::LoadMediaPlayer()
{
    SCOPE_CYCLE_COUNTER(STAT_OMSPlayerComponent_LoadMediaPlayer);

    // If user is creating the HoloSuitePlayer from scratch, the MediaPlayer and MediaPlayerMaterial assets will have to be created and assigned manually.
    if (DefaultMediaPlayerMaterial == nullptr && (MediaPlayer == nullptr || MediaPlayerMaterial == nullptr))
    {
        return;
    }

    if (MediaPlayer == nullptr)
    {
        MediaPlayer = NewObject<UMediaPlayer>(GetTransientPackage());
    }

    if (MediaPlayerMaterial == nullptr)
    {
        UMaterialInstanceDynamic* materialInstance = UMaterialInstanceDynamic::Create((UMaterialInterface*)DefaultMediaPlayerMaterial, GetTransientPackage());

        UMediaTexture* MediaTexture = NewObject<UMediaTexture>(GetTransientPackage());
        MediaTexture->NewStyleOutput = true;
        MediaTexture->SetMediaPlayer(MediaPlayer);
        MediaTexture->UpdateResource();
        materialInstance->SetTextureParameterValue(FName("BaseTexture"), MediaTexture);

        MediaPlayerMaterial = materialInstance;
    }
    else
    {
        UMediaTexture* MediaTexture = NewObject<UMediaTexture>(GetTransientPackage());
        MediaTexture->NewStyleOutput = true;
        MediaTexture->SetMediaPlayer(MediaPlayer);
        MediaTexture->UpdateResource();

        UMaterialInstanceDynamic* materialInstance = Cast<UMaterialInstanceDynamic>(MediaPlayerMaterial);
        if (materialInstance)
        {
            materialInstance->SetTextureParameterValue(FName("BaseTexture"), MediaTexture);
        }
    }

    UWorld* world = GetWorld();
    if (!GIsEditor || (world != nullptr && world->IsPlayInEditor()))
    {
        MediaPlayer->OnMediaOpened.AddUniqueDynamic(this, &UOMSPlayerComponent::OnMediaOpened);
        MediaPlayer->OnEndReached.AddUniqueDynamic(this, &UOMSPlayerComponent::OnMediaEndReached);
        MediaPlayer->PlayOnOpen = PlayOnOpen;
    }
    else
    {
        MediaPlayer->PlayOnOpen = false;
    }
    
    MediaPlayer->OpenSource(TextureSource);
    MediaPlayer->SetLooping(Loop);

    sourceFrameRate = -1.0f;
    bIsPlaying = false; // it should only be considered playing when it actually starts playing.
}

void UOMSPlayerComponent::SetMeshMaterial(UMaterialInterface* NewMeshMaterial)
{
    MeshMaterial = NewMeshMaterial;
    if (Decoder)
    {
        Decoder->LoadMeshMaterial(MeshMaterial);
    }
}

void UOMSPlayerComponent::SetPlaybackParameters(bool NewPlayOnOpen, bool NewLoop, bool NewMute, float NewFrameRate)
{
    PlayOnOpen = NewPlayOnOpen;
    Loop = NewLoop;

    if (NewFrameRate > 0.0f)
    {
        FrameRate = NewFrameRate;
    }

    if (Mute != NewMute)
    {
        Mute = NewMute;
        if (MediaSoundComponent)
        {
            SetMuted(Mute);
        }
    }
}

void UOMSPlayerComponent::SetRenderingParameters(bool NewResponsiveAA, bool NewReceiveDecals)
{
    ResponsiveAA = NewResponsiveAA;
    ReceiveDecals = NewReceiveDecals;

    if (Decoder)
    {
        Decoder->SetRenderingOptions(false, ResponsiveAA, ReceiveDecals);
    }
}

void UOMSPlayerComponent::SetDecoderParameters(bool NewUseCPUDecoder, int NewMaxBufferedSequences)
{
    if (NewUseCPUDecoder != UseCPUDecoder)
    {
        UseCPUDecoder = NewUseCPUDecoder;
    }
    if (NewMaxBufferedSequences != MaxBufferedSequences)
    {
        MaxBufferedSequences = NewMaxBufferedSequences;
    }

    Decoder->Configure(this, UseCPUDecoder, NewMaxBufferedSequences);

    UseCPUDecoder = Decoder->GetUseCPUDecoder();
    MaxBufferedSequences = Decoder->GetMaxBufferedSequences();
}

void UOMSPlayerComponent::SetSkeletonParameters(bool NewEnableSkeleton, USkeletalMesh* NewPlayerSkeletalMesh, bool PrepareSkeleton)
{
    if(EnableSkeleton == NewEnableSkeleton && PlayerSkeletalMesh == NewPlayerSkeletalMesh)
    {
        return;
    }

    if (SkeletonManager)
    {
        delete SkeletonManager;
    }
    DeletePlayerSkeletalMesh();

    EnableSkeleton = NewEnableSkeleton;
    if (EnableSkeleton)
    {
        if (NewPlayerSkeletalMesh != nullptr)
        {
            PlayerSkeletalMesh = NewPlayerSkeletalMesh;
            if (PrepareSkeleton)
            {
                PrepareSkeletonManager();
            }
        }
    }
    else
    {
        EnableRetargeting = false;
    }

}

void UOMSPlayerComponent::SetRetargetParameters(bool NewEnableRetargeting, TSubclassOf<UAnimInstance> NewRetargetingAnimation, bool PrepareSkeleton)
{
    if (EnableRetargeting == NewEnableRetargeting && RetargetingAnimation == NewRetargetingAnimation)
    {
        return;
    }

    if (SkeletonManager)
    {
        delete SkeletonManager;
    }
    DeletePlayerSkeletalMesh();

    EnableRetargeting = NewEnableRetargeting;
    RetargetingAnimation = NewRetargetingAnimation;
    if (EnableSkeleton && PlayerSkeletalMesh != nullptr && RetargetingAnimation != nullptr && PrepareSkeleton)
    {
        PrepareSkeletonManager();
    }
}

void UOMSPlayerComponent::SetRetargetingAnimationMaterial(UMaterialInterface* NewRetargetAnimMaterial)
{
    if (RetargetAnimMaterial == NewRetargetAnimMaterial || NewRetargetAnimMaterial == nullptr)
    {
        return;
    }

    if (SkeletonManager)
    {
        delete SkeletonManager;
    }
    DeletePlayerSkeletalMesh();

    RetargetAnimMaterial = NewRetargetAnimMaterial;
    PrepareSkeletonManager();
}

bool UOMSPlayerComponent::GetUseCPUDecoder()
{
    UseCPUDecoder = Decoder->GetUseCPUDecoder();
    return UseCPUDecoder;
}

int UOMSPlayerComponent::GetMaxBufferedSequences()
{
    MaxBufferedSequences = Decoder->GetMaxBufferedSequences();
    return MaxBufferedSequences;
}

/* Playback Functions */

void UOMSPlayerComponent::SetFrame(int frameNumber)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSPlayerComponent_SetFrame);

    if (frameNumber < 0 || frameNumber >= FrameCount)
    {
        return;
    }

    std::pair<int, int> frame = Decoder->GetFrameFromLookupTable(frameNumber);

    if (frame.first < 0)
    {
        return;
    }

    int oldActiveSequence = activeSequence;
    LoadSequence(frame.first, true);
    bool sequenceUpdated = activeSequence != oldActiveSequence;

    LoadSequenceFrame(frame.second, sequenceUpdated);

    if (sequenceUpdated)
    {
        Decoder->SwapHoloMesh();
    }

    CheckPlayerReady();
}

bool UOMSPlayerComponent::TrySetFrame(int frameNumber)
{
    SCOPE_CYCLE_COUNTER(STAT_OMSPlayerComponent_TrySetFrame);

    if (frameNumber < 0 || frameNumber >= FrameCount)
    {
        return false;
    }

    // If requested sequence is not in cache we can early out.
    std::pair<int, int> newFrame = Decoder->GetFrameFromLookupTable(frameNumber);

    if (newFrame.first < 0)
    {
        return false;
    }

    // Inform the worker thread what sequence we need if we don't already have it.
    int requestedSequence = newFrame.first;
    if (DecodedSequence->sequenceIndex != requestedSequence)
    {
        Decoder->RequestSequence(requestedSequence);
    }

    // Try to load sequence.
    int oldActiveSequence = activeSequence;
    bool sequenceReady = LoadSequence(newFrame.first, false);
    if (!sequenceReady)
    {
        return false;
    }
    bSequenceUpdated = requestedSequence != oldActiveSequence;

    // Try to load frame.
    if (!LoadSequenceFrame(newFrame.second, bSequenceUpdated))
    {
        return false;
    }

    bFrameUpdated = true;

    CheckPlayerReady();
    return true;
}

void UOMSPlayerComponent::Play()
{
    if (VerifyMediaPlayer())
    {
        MediaPlayer->PlayOnOpen = PlayOnOpen;
        MediaPlayer->Play();
        OnPlaybackResumed.Broadcast();
        bIsPlaying = true;
    }
}

void UOMSPlayerComponent::Pause()
{
    if (VerifyMediaPlayer())
    {
        MediaPlayer->PlayOnOpen = PlayOnOpen;
        MediaPlayer->Pause();
        OnPlaybackSuspended.Broadcast();
        bIsPlaying = false;
    }
}

void UOMSPlayerComponent::SetMuted(bool shouldMute)
{
    Mute = shouldMute;
    if (MediaSoundComponent)
    {
        if (Mute)
        {
            MediaSoundComponent->Stop();
        }
        else {
            MediaSoundComponent->Start();
        }
    }
}

void UOMSPlayerComponent::Seek(FTimespan target)
{
    if (VerifyMediaPlayer())
    {
        MediaPlayer->Seek(target);
    }
}

bool UOMSPlayerComponent::IsPlaying()
{
    return bIsPlaying;
}

bool UOMSPlayerComponent::IsMuted()
{
    return Mute;
}

void UOMSPlayerComponent::CheckPlayerReady()
{
    //UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSPlayerComponent: CheckPlayerReady: activeSequence=%d, activeFrame=%d, playerReady=%d"), activeSequence, activeFrame, playerReady);
    if (((activeSequence == 0 && activeFrame >= 1) || activeSequence >= 1) && !bPlayerReady)
    {
        //UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSPlayerComponent: Player ready"));
        OnPlayerReady.Broadcast();
        bPlayerReady = true;
    }
}

#if WITH_EDITOR
// Provides editor debug messages informing users of incorrect configuration.
void UOMSPlayerComponent::VerifyHoloSuitePlayer()
{
    // ARCTURUS in ASCII.
    const int32 ArcturusDebugMessageKey = 65 + 82 + 67 + 84 + 85 + 82 + 85 + 83;

    // When using D3D12 we require ElectraPlayer to be installed.
    bool usingD3D12 = FHardwareInfo::GetHardwareInfo(NAME_RHI) == "D3D12";
    if (usingD3D12)
    {
        bool electraEnabled = false;

        TSharedPtr<IPlugin> ElectraPlugin = IPluginManager::Get().FindPlugin("ElectraPlayer");
        if (ElectraPlugin.IsValid())
        {
            electraEnabled = ElectraPlugin->IsEnabled();
        }

        if (!electraEnabled)
        {
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 0, 15.0f, FColor::Red, TEXT("HoloSuite Player requires Electra Player plugin in DirectX 12."));
            }
        }
    }

    // Check material validity
    if (MeshMaterial != nullptr)
    {
        UMaterial* baseMaterial = MeshMaterial->GetBaseMaterial();
        if (baseMaterial != nullptr && baseMaterial->bTangentSpaceNormal)
        {
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(ArcturusDebugMessageKey + 1, 15.0f, FColor::Yellow, TEXT("The HoloSuite Player OMS material has tangent space normals enabled. Lighting will not work correctly."));
            }
        }
    }
}
#endif

bool UOMSPlayerComponent::VerifyMediaPlayer()
{
    if (!MediaPlayer)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("OMSPlayerComponent: a MediaPlayer was not configured for playback."));
        return false;
    }
    if (!MediaPlayerMaterial)
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("OMSPlayerComponent: a MediaPlayer Material was not configured for playback."));
        return false;
    }
    if (!MediaPlayer->IsReady())
    {
        UE_LOG(LogHoloSuitePlayer, Error, TEXT("OMSPlayerComponent: the MediaPlayer is not yet ready for playback."));
        return false;
    }
    return true;
}

bool UOMSPlayerComponent::IsCustomPlayer()
{
    if (Cast<AHoloSuitePlayer>(GetOwner()))
    {
        return false;
    }
    return true;
}

void UOMSPlayerComponent::PrepareSkeletonManager()
{
    SCOPE_CYCLE_COUNTER(STAT_OMSPlayerComponent_PrepareSkeletonManager);

    FStreamableOMSData* OMSStreamableData = &(FStreamableOMSData&)OMS->GetStreamableData();
    oms_header_t omsHeader = {};
    OMSStreamableData->ReadHeaderSync(&omsHeader);
    if (omsHeader.has_retarget_data)
    {
        bHasSkeletonData = true;

        UWorld* world = GetWorld();
        if (!GIsEditor || (world != nullptr && world->IsPlayInEditor()))
        {
            if (EnableSkeleton)
            {
                FAttachmentTransformRules rules(EAttachmentRule::SnapToTarget, true);
                if (EnableRetargeting)
                {
                    if (PlayerSkeletalMesh)
                    {
                        if (RetargetingAnimation)
                        {
                            if (DefaultRetargetAnimMaterial || RetargetAnimMaterial)
                            {
                                AActor* Owner = GetOwner();
                                TArray<AActor*> attachedActors;
                                Owner->GetAttachedActors(attachedActors);
                                PlayerSkeletalMeshActor = nullptr;

                                for (int i = 0; i < attachedActors.Num(); i++)
                                {
                                    if (attachedActors[i]->IsA(ASkeletalMeshActor::StaticClass()))
                                    {
                                        PlayerSkeletalMeshActor = Cast<ASkeletalMeshActor>(attachedActors[i]);
                                    }
                                }

                                if (PlayerSkeletalMeshActor == nullptr)
                                {
                                    FActorSpawnParameters params;
                                    params.Owner = Owner;
                                    PlayerSkeletalMeshActor = world->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), Owner->GetTransform(), params); 
                                    PlayerSkeletalMeshActor->SetActorTransform(Owner->GetTransform());
                                    PlayerSkeletalMeshActor->AttachToComponent(this, rules);
                                }

                                PlayerSkeletalMeshComponent = PlayerSkeletalMeshActor->GetSkeletalMeshComponent();
                                PlayerSkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
                                PlayerSkeletalMeshComponent->SetAnimClass(RetargetingAnimation);

#if (ENGINE_MAJOR_VERSION >= 5) && (ENGINE_MINOR_VERSION >= 1)
                                PlayerSkeletalMeshComponent->SetSkeletalMesh(PlayerSkeletalMesh);
#else
                                PlayerSkeletalMeshComponent->SkeletalMesh = PlayerSkeletalMesh;
#endif

                                if (!DefaultRetargetAnimMaterial)
                                {
                                    DefaultRetargetAnimMaterial = RetargetAnimMaterial;
                                }
                                RetargetAnimMaterial = UMaterialInstanceDynamic::Create((UMaterialInterface*)DefaultRetargetAnimMaterial, GetTransientPackage());
                                SkeletonManager = new OMSSkeleton(PlayerSkeletalMeshComponent);
                            }
                            else
                            {
                                UE_LOG(LogHoloSuitePlayer, Error, TEXT("OMSPlayerComponent: Internal Error: retargeting requires the assignment of a valid Animation Material and the default HoloSuite Material wasn't found. Disabling Retargeting."));
                                EnableRetargeting = false;
                                EnableSkeleton = false;
                            }
                        }
                        else
                        {
                            UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSPlayerComponent: Retargeting requires the assignment of a valid Animation and one wasn't provided. Disabling Retargeting."));
                            EnableRetargeting = false;
                            EnableSkeleton = false;
                        }
                    }
                    else
                    {
                        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSPlayerComponent: Retargeting requires the assignment of a valid Skeletal Mesh and one wasn't provided. Disabling Retargeting."));
                        EnableRetargeting = false;
                        EnableSkeleton = false;
                    }
                }
                else
                {            
                    if (PlayerSkeletalMesh)
                    {
                        AActor* Owner = GetOwner();
                        PlayerSkeletalMeshComponent = NewObject<USkeletalMeshComponent>(Owner);

#if (ENGINE_MAJOR_VERSION >= 5) && (ENGINE_MINOR_VERSION >= 1)
                        PlayerSkeletalMeshComponent->SetSkeletalMesh(PlayerSkeletalMesh);
#else
                        PlayerSkeletalMeshComponent->SkeletalMesh = PlayerSkeletalMesh;
#endif

                        PlayerSkeletalMeshComponent->SetWorldTransform(Owner->GetTransform());
                        if (world)
                        {
                            PlayerSkeletalMeshComponent->AttachToComponent(this, rules);
                            PlayerSkeletalMeshComponent->RegisterComponent();
                        }

                        SkeletonManager = new OMSSkeleton(PlayerSkeletalMeshComponent);
                    }
                    else
                    {
                        UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSPlayerComponent: The ability to attach actors to the player's skeleton requires the assignment of a valid Skeletal Mesh and one wasn't provided. Disabling Skeleton."));
                        EnableSkeleton = false;
                    }
                }
            }
        }
    }
    else
    {
        bHasSkeletonData = false;
        if (SkeletonManager)
        {
            delete SkeletonManager;
        }
    }
}

void UOMSPlayerComponent::DeleteMediaSound()
{
    FDetachmentTransformRules rules(FDetachmentTransformRules::KeepWorldTransform);

    if (MediaSoundComponent)
    {
        MediaSoundComponent->DetachFromComponent(rules);
        MediaSoundComponent->DestroyComponent();
    }
}

void UOMSPlayerComponent::DeletePlayerSkeletalMesh()
{
    FDetachmentTransformRules rules(FDetachmentTransformRules::KeepWorldTransform);

    if (PlayerSkeletalMeshComponent)
    {
        TArray<USceneComponent*> attachedChildren = PlayerSkeletalMeshComponent->GetAttachChildren();
        for (int i = 0; i < attachedChildren.Num(); i++)
        {
            attachedChildren[i]->DetachFromComponent(rules);
        }
        PlayerSkeletalMeshComponent->DetachFromComponent(rules);
        PlayerSkeletalMeshComponent->DestroyComponent();
    }

    if (PlayerSkeletalMeshActor)
    {
        PlayerSkeletalMeshActor = nullptr;
    }

    if (PlayerSkeletalMesh)
    {
        PlayerSkeletalMesh = nullptr;
    }
}

/* Skeleton Functions */

void UOMSPlayerComponent::AttachActorToSkeleton(AActor* Actor, FName SocketName)
{
    ActorsToBeAttached.Emplace(Actor, SocketName);
}

/* Event Handler Functions */

void UOMSPlayerComponent::OnMediaOpened(FString DeviceUrl)
{
    // Check playback status
    UWorld* world = GetWorld();
    if (!GIsEditor || (world != nullptr && world->IsPlayInEditor()))
    {
        if (PlayOnOpen)
        {
            bIsPlaying = true;
        }
    }

    // Retrieve MediaPlayer's video track frame rate.
    sourceFrameRate = MediaPlayer->GetVideoTrackFrameRate(INDEX_NONE, INDEX_NONE);

    // Setup MediaSoundComponent.
    if (MediaSoundComponent == nullptr)
    {
        AActor* Owner = GetOwner();
        MediaSoundComponent = NewObject<UMediaSoundComponent>(Owner, TEXT("MediaSoundComponent"));
        MediaSoundComponent->SetWorldTransform(Owner->GetTransform());
        MediaSoundComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
        if (GetWorld())
        {
            MediaSoundComponent->RegisterComponent();
        }
    }

    /*
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 0
    if (MediaSoundComponent == nullptr)
    {
        MediaSoundComponent = NewObject<UMediaSoundComponent>(GetOwner());
        //FAttachmentTransformRules rules = FAttachmentTransformRules(EAttachmentRule::SnapToTarget,true);
        //MediaSoundComponent->AttachToComponent(GetOwner()->RootComponent, rules);
        MediaSoundComponent->SetWorldTransform(GetOwner()->GetTransform());
        MediaSoundComponent->RegisterComponent();
    }
#endif
    */

    if (MediaSoundComponent != nullptr)
    {
        MediaSoundComponent->SetMediaPlayer(MediaPlayer);

#if WITH_EDITOR
        if (world == nullptr || !world->IsPlayInEditor())
        {
            MediaSoundComponent->SetDefaultMediaPlayer(MediaPlayer);
        }
#endif

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 0
        MediaSoundComponent->Initialize();
        if (!Mute)
        {
            MediaSoundComponent->Start();
        }
#endif
        if (Mute)
        {
            MediaSoundComponent->Stop();
        }
    }
}

void UOMSPlayerComponent::OnMediaEndReached()
{
    // UE_LOG(LogHoloSuitePlayer, Warning, TEXT("OMSPlayerComponent: OnMediaEndReached"));
    OnEndReached.Broadcast();
}
