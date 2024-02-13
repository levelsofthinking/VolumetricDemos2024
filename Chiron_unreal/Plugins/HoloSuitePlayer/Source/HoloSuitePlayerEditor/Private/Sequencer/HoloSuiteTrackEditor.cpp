// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "Sequencer/HoloSuiteTrackEditor.h"
#include "Sequencer/AVVSectionEditor.h"
#include "Sequencer/AVVSection.h"
#include "Sequencer/HoloSuiteTrack.h"

#include "Editor.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "Kismet/GameplayStatics.h"
#include "LevelEditorViewport.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneCommonHelpers.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "SequencerUtilities.h"

#define LOCTEXT_NAMESPACE "HoloSuiteTrackEditor"

FHoloSuiteTrackEditor::FHoloSuiteTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
}

FHoloSuiteTrackEditor::~FHoloSuiteTrackEditor()
{
}

TSharedRef<ISequencerTrackEditor> FHoloSuiteTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FHoloSuiteTrackEditor(InSequencer));
}

TSharedPtr<SWidget> FHoloSuiteTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			FSequencerUtilities::MakeAddButton(LOCTEXT("AddAVVButton", "Volumetric Video"), FOnGetContent::CreateSP(this, &FHoloSuiteTrackEditor::BuildAddAVVMenu), Params.NodeIsHovered, GetSequencer())
		];
}

TSharedRef<ISequencerSection> FHoloSuiteTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	checkf(SectionObject.GetClass()->IsChildOf<UAVVSection>(), TEXT("Unsupported section."));
	return MakeShareable(new FAVVSectionEditor(SectionObject));
}

bool FHoloSuiteTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UHoloSuiteTrack::StaticClass();
}

const FSlateBrush* FHoloSuiteTrackEditor::GetIconBrush() const
{
	return FHoloSuitePlayerEditorStyle::Get()->GetBrush("Sequencer.Tracks.VolumetricVideo");
}

void FHoloSuiteTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddHoloSuiteTrack", "Volumetric Video Track"),
		LOCTEXT("AddHoloSuiteTrackTooltip", "Adds an Arcturus Volumetric Video track."),
		FSlateIcon(FHoloSuitePlayerEditorStyle::Get()->GetStyleSetName(), "Sequencer.Tracks.VolumetricVideo"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FHoloSuiteTrackEditor::HandleAddHoloSuiteTrackMenuEntryExecute),
			FCanExecuteAction::CreateRaw(this, &FHoloSuiteTrackEditor::HandleAddHoloSuiteTrackMenuEntryCanExecute)
		)
	);
}

bool FHoloSuiteTrackEditor::HandleAddHoloSuiteTrackMenuEntryCanExecute() const
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2
	return ((FocusedMovieScene != nullptr) && (FocusedMovieScene->FindTrack<UHoloSuiteTrack>() == nullptr));
#else
	return ((FocusedMovieScene != nullptr) && (FocusedMovieScene->FindMasterTrack<UHoloSuiteTrack>() == nullptr));
#endif
}

void FHoloSuiteTrackEditor::HandleAddHoloSuiteTrackMenuEntryExecute()
{
	UHoloSuiteTrack* HoloSuiteTrack = FindOrCreateHoloSuiteTrack();

	if (HoloSuiteTrack)
	{
		if (GetSequencer().IsValid())
		{
			GetSequencer()->OnAddTrack(HoloSuiteTrack, FGuid());
		}
	}
}

UHoloSuiteTrack* FHoloSuiteTrackEditor::FindOrCreateHoloSuiteTrack(bool* bWasCreated)
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();
	if (FocusedMovieScene == nullptr || FocusedMovieScene->IsReadOnly())
	{
		return nullptr;
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2
	UHoloSuiteTrack* HoloSuiteTrack = FocusedMovieScene->FindTrack<UHoloSuiteTrack>();
#else
	UHoloSuiteTrack* HoloSuiteTrack = FocusedMovieScene->FindMasterTrack<UHoloSuiteTrack>();
#endif

	if (HoloSuiteTrack == nullptr)
	{
		const FScopedTransaction Transaction(LOCTEXT("AddHoloSuiteTrack_Transaction", "Add Volumetric Video Track"));
		FocusedMovieScene->Modify();
		
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2
		HoloSuiteTrack = FocusedMovieScene->AddTrack<UHoloSuiteTrack>();
#else 
		HoloSuiteTrack = FocusedMovieScene->AddMasterTrack<UHoloSuiteTrack>();
#endif

		if (bWasCreated)
		{
			*bWasCreated = true;
		}
	}

	return HoloSuiteTrack;
}

TSharedRef<SWidget> FHoloSuiteTrackEditor::BuildAddAVVMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddAVVSection", "Add AVV"),
		LOCTEXT("AddAVVSectionTooltip", "Adds an AVV section."),
		//FSlateIcon(FHoloSuitePlayerEditorStyle::Get()->GetStyleSetName(), "Sequencer.Tracks.VolumetricVideo"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { 
				AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FHoloSuiteTrackEditor::HandleAddAVVKey));
			})
		)
	);

	MenuBuilder.AddMenuSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddAllAVVSection", "Add All AVVs"),
		LOCTEXT("AddAllAVVSectionTooltip", "Adds an AVV section per HoloSuitePlayer actor configured for AVV playback in the scene."),
		//FSlateIcon(FHoloSuitePlayerEditorStyle::Get()->GetStyleSetName(), "Sequencer.Tracks.VolumetricVideo"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() {
				AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FHoloSuiteTrackEditor::HandleAddAllAVVKey));
			})
		)
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddSelectedAVVSection", "Add Selected AVVs"),
		LOCTEXT("AddSelectedAVVSectionTooltip", "Adds an AVV section per selected HoloSuitePlayer actor configured for AVV playback in the scene."),
		//FSlateIcon(FHoloSuitePlayerEditorStyle::Get()->GetStyleSetName(), "Sequencer.Tracks.VolumetricVideo"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() {
		AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FHoloSuiteTrackEditor::HandleAddSelectedAVVKey));
	})
		)
	);

	return MenuBuilder.MakeWidget();
}

FKeyPropertyResult FHoloSuiteTrackEditor::HandleAddAVVKey(FFrameNumber KeyTime)
{
	FKeyPropertyResult KeyPropertyResult;

	UHoloSuiteTrack* HoloSuiteTrack = FindOrCreateHoloSuiteTrack(&KeyPropertyResult.bTrackCreated);
	if (HoloSuiteTrack != nullptr)
	{
		const TArray<UMovieSceneSection*>& AllSections = HoloSuiteTrack->GetAllSections();

		UAVVSection* NewSection = HoloSuiteTrack->AddNewAVVSection(KeyTime);
		KeyPropertyResult.bTrackModified = true;
		KeyPropertyResult.SectionsCreated.Add((UMovieSceneSection*)NewSection);

		GetSequencer()->EmptySelection();
		GetSequencer()->SelectSection((UMovieSceneSection*)NewSection);
		GetSequencer()->ThrobSectionSelection();
		GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	}

	return KeyPropertyResult;
}

FKeyPropertyResult FHoloSuiteTrackEditor::HandleAddAllAVVKey(FFrameNumber KeyTime)
{
	return HandleAddMultipleAVVKey(KeyTime, false);
}

FKeyPropertyResult FHoloSuiteTrackEditor::HandleAddSelectedAVVKey(FFrameNumber KeyTime)
{
	return HandleAddMultipleAVVKey(KeyTime, true);
}

FKeyPropertyResult FHoloSuiteTrackEditor::HandleAddMultipleAVVKey(FFrameNumber KeyTime, bool SelectedOnly)
{
	FKeyPropertyResult KeyPropertyResult;

	// Validate track
	UHoloSuiteTrack* HoloSuiteTrack = FindOrCreateHoloSuiteTrack(&KeyPropertyResult.bTrackCreated);
	if (!HoloSuiteTrack)
	{
		UE_LOG(LogHoloSuitePlayerEditor, Error, TEXT("HoloSuiteTrackEditor: invalid HoloSuiteTrack. Try recreating your volumetric video track or contact our support team."));
		return KeyPropertyResult;
	}

	// Iterate existing sections to avoid adding already added AVVs
	TArray<AHoloSuitePlayer*> existingPlayers;
	const TArray<UMovieSceneSection*>& AllSections = HoloSuiteTrack->GetAllSections();
	for (int i = 0; i < AllSections.Num(); i++)
	{
		UAVVSection* section = Cast<UAVVSection>(AllSections[i]);
		if (section)
		{
			AHoloSuitePlayer* player = section->Player.Get();
			if (player)
			{
				existingPlayers.Add(player);
			}
		}
	}

	// Find AVV actors
	TArray<AHoloSuitePlayer*> newPlayers;
	TArray<AActor*> foundActors;
	UGameplayStatics::GetAllActorsOfClass(GWorld, AHoloSuitePlayer::StaticClass(), foundActors);
	for (int i = 0; i < foundActors.Num(); i++)
	{
		AHoloSuitePlayer* player = Cast<AHoloSuitePlayer>(foundActors[i]);
		if (player && !existingPlayers.Contains(player) && player->GetAVVPlayerComponent() != nullptr)
		{
			if (SelectedOnly)
			{
				if (player->IsSelectedInEditor() || player->IsSelected())
				{
					newPlayers.Add(player);
				}
			}
			else
			{
				newPlayers.Add(player);
			}
		}
	}

	// Check if there are any players to be added to the sequencer
	if (newPlayers.Num() == 0)
	{
		UE_LOG(LogHoloSuitePlayerEditor, Warning, TEXT("HoloSuiteTrackEditor: no new HoloSuitePlayers with AVV playback configured were found."));
		return KeyPropertyResult;
	}

	// Update sequencer
	GetSequencer()->EmptySelection();
	for (int i = 0; i < newPlayers.Num(); i++)
	{
		// Configure players for sequencer playback
		AHoloSuitePlayer* player = newPlayers[i];
		player->SetAVVPlaybackParameters(true, player->PlayOnOpen, player->Loop, player->PingPong, player->Reverse, player->FrameRate, player->CurrentFrame);

		// Create and configure sections
		UAVVSection* NewSection = HoloSuiteTrack->AddNewAVVSection(KeyTime);
		NewSection->Player = player;
		KeyPropertyResult.bTrackModified = true;
		KeyPropertyResult.SectionsCreated.Add((UMovieSceneSection*)NewSection);
		GetSequencer()->SelectSection((UMovieSceneSection*)NewSection);
	}
	GetSequencer()->ThrobSectionSelection();
	GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);

	return KeyPropertyResult;
}

void FHoloSuiteTrackEditor::OnInitialize()
{
}

void FHoloSuiteTrackEditor::OnRelease()
{
}

void FHoloSuiteTrackEditor::Tick(float DeltaTime)
{

}

FAVVSection::FAVVSection(TSharedPtr<ISequencer> InSequencer, UMovieSceneSection& InSection)
	: FSequencerSection(InSection)
{
}

FAVVSection::~FAVVSection()
{
}

#undef LOCTEXT_NAMESPACE
