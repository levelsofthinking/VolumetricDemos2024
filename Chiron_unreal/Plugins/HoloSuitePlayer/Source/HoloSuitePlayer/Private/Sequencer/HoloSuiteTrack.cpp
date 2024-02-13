// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "Sequencer/HoloSuiteTrack.h"
#include "Sequencer/AVVSection.h"
#include "Sequencer/AVVSectionTemplate.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "MovieScene.h"

#define LOCTEXT_NAMESPACE "HoloSuiteTrack"

UHoloSuiteTrack::UHoloSuiteTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedBlendTypes.Add(EMovieSceneBlendType::Absolute);
}

UHoloSuiteTrack::~UHoloSuiteTrack()
{
}

void UHoloSuiteTrack::AddSection(UMovieSceneSection& Section)
{
	if (UAVVSection* avvSection = Cast<UAVVSection>(&Section))
	{
		Sections.Add(avvSection);
	}
}

bool UHoloSuiteTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UAVVSection::StaticClass();
}

UMovieSceneSection* UHoloSuiteTrack::CreateNewSection()
{
	return NewObject<UAVVSection>(this, NAME_None, RF_Transactional);
}

const TArray<UMovieSceneSection*>& UHoloSuiteTrack::GetAllSections() const
{
	return Sections;
}

bool UHoloSuiteTrack::HasSection(const UMovieSceneSection& Section) const
{
	return Sections.Contains(&Section);
}


bool UHoloSuiteTrack::IsEmpty() const
{
	return Sections.Num() == 0;
}

void UHoloSuiteTrack::RemoveSection(UMovieSceneSection& Section)
{
	Sections.Remove(&Section);
}

void UHoloSuiteTrack::RemoveSectionAt(int32 SectionIndex)
{
	Sections.RemoveAt(SectionIndex);
}

void UHoloSuiteTrack::RemoveAllAnimationData()
{
	Sections.Empty();
}

UAVVSection* UHoloSuiteTrack::AddNewAVVSection(FFrameNumber KeyTime)
{
	UAVVSection* NewSection = Cast<UAVVSection>(CreateNewSection());

	const UMovieScene* OwnerScene = GetTypedOuter<UMovieScene>();
	const TRange<FFrameNumber> PlaybackRange = OwnerScene->GetPlaybackRange();
	NewSection->InitialPlacement(Sections, KeyTime, PlaybackRange.Size<FFrameNumber>().Value, true);

	AddSection(*NewSection);
	Modify();

	return NewSection;
}

FMovieSceneEvalTemplatePtr UHoloSuiteTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FAVVSectionTemplate(*CastChecked<const UAVVSection>(&InSection), *this);
}

#if WITH_EDITORONLY_DATA

FText UHoloSuiteTrack::GetDisplayName() const
{
	return LOCTEXT("TrackName", "Volumetric Video");
}

FText UHoloSuiteTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("TrackName", "Volumetric Video");
}

#endif

#undef LOCTEXT_NAMESPACE
