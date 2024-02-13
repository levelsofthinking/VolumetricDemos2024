// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "ISequencerSection.h"

class UAVVSection;

class FAVVSectionEditor : public ISequencerSection
{
public:
	FAVVSectionEditor(UMovieSceneSection& InSection);

	//~ ISequencerSection interface
	virtual UMovieSceneSection* GetSectionObject() override;
	virtual int32 OnPaintSection(FSequencerSectionPainter& InPainter) const override;
	virtual FText GetSectionTitle() const override;

private:
	UAVVSection* Section;
};