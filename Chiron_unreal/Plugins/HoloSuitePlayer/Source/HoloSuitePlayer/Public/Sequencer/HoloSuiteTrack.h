// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"
#include "Misc/InlineValue.h"
#include "MovieSceneTrack.h"
#include "MovieSceneNameableTrack.h"
#include "UObject/ObjectMacros.h"
#include "HoloSuiteTrack.generated.h"

class UAVVSection;

UCLASS()
class HOLOSUITEPLAYER_API UHoloSuiteTrack : public UMovieSceneNameableTrack, public IMovieSceneTrackTemplateProducer
{
public:
	GENERATED_BODY()

	UHoloSuiteTrack(const FObjectInitializer& ObjectInitializer);
	~UHoloSuiteTrack();

	UAVVSection* AddNewAVVSection(FFrameNumber KeyTime);

	// UMovieSceneTrack interface
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual bool IsEmpty() const override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual void RemoveAllAnimationData() override;
	virtual bool SupportsMultipleRows() const override { return true; }

	// IMovieSceneTrackTemplateProducer Interface
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;

#if WITH_EDITORONLY_DATA
	virtual FText GetDisplayName() const override;
#endif

#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif
private:

	UPROPERTY()
		TArray<UMovieSceneSection*> Sections;
};

