// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISequencerSection.h"
#include "Misc/Guid.h"
#include "MovieSceneTrackEditor.h"

#include "HoloSuitePlayerEditor.h"
#include "HoloSuitePlayerEditorStyle.h"

class UAVVSection;
class UHoloSuiteTrack;

class FHoloSuiteTrackEditor : public FMovieSceneTrackEditor
{
public:
	FHoloSuiteTrackEditor(TSharedRef<ISequencer> InSequencer);
	~FHoloSuiteTrackEditor();

	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer);

public:
	// ISequencerTrackEditor interface
	TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
	virtual bool SupportsType(TSubclassOf<class UMovieSceneTrack> TrackClass) const override;
	const FSlateBrush* GetIconBrush() const override;
	virtual void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override;
	
	virtual void OnInitialize() override;
	virtual void OnRelease() override;
	
	virtual void Tick(float DeltaTime) override;

private:
	bool HandleAddHoloSuiteTrackMenuEntryCanExecute() const;
	void HandleAddHoloSuiteTrackMenuEntryExecute();

	UHoloSuiteTrack* FindOrCreateHoloSuiteTrack(bool* bWasCreated = nullptr);

	TSharedRef<SWidget> BuildAddAVVMenu();

	FKeyPropertyResult HandleAddAVVKey(FFrameNumber AutoKeyTime);
	FKeyPropertyResult HandleAddAllAVVKey(FFrameNumber AutoKeyTime);
	FKeyPropertyResult HandleAddSelectedAVVKey(FFrameNumber AutoKeyTime);
	FKeyPropertyResult HandleAddMultipleAVVKey(FFrameNumber KeyTime, bool SelectedOnly);

	void OnGlobalTimeChanged();

private:
	FFrameTime LastCurrentTime;
	EMovieScenePlayerStatus::Type LastPlaybackStatus;

	TWeakObjectPtr<AActor> LastLockedActor;
};

class FAVVSection
	: public FSequencerSection
	, public TSharedFromThis<FAVVSection>
{
public:
	FAVVSection(TSharedPtr<ISequencer> InSequencer, UMovieSceneSection& InSection);
	virtual ~FAVVSection();
};
