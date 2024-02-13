// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "Evaluation/MovieSceneEvalTemplate.h"
#include "Evaluation/MovieScenePropertyTemplate.h"
#include "Misc/FrameNumber.h"
//#include "AVV/AVVPlayer.h"
#include "HoloSuitePlayer.h"
#include "AVVSectionTemplate.generated.h"

class UAVVSection;
class UHoloSuiteTrack;
class AAVVPlayer;

USTRUCT()
struct FAVVSectionParams
{
	GENERATED_BODY()

	FAVVSectionParams();
	~FAVVSectionParams();

	UPROPERTY()
	FFrameNumber SectionStartTime;

	UPROPERTY()
	TSoftObjectPtr<AHoloSuitePlayer> Player;

	UPROPERTY()
	FGuid SpawnableGUID;

	UPROPERTY()
	int StartFrameOffset;

	UPROPERTY()
	UMovieScene* MovieScene;

	void Reset()
	{
		Player.Reset();
		SpawnableGUID.Invalidate();
		MovieScene = nullptr;
	};
};

USTRUCT()
struct FAVVSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()

	FAVVSectionTemplate();
	FAVVSectionTemplate(const UAVVSection& InSection, const UHoloSuiteTrack& InTrack); // const UHoloSuiteTrack& InTrack);
	~FAVVSectionTemplate();

	virtual void SetupOverrides() override
	{
		EnableOverrides(RequiresSetupFlag);
	}
	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }

	virtual void Setup(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;

	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;

private:

	UPROPERTY()
	FMovieScenePropertySectionData PropertyData;

	UPROPERTY()
	FAVVSectionParams Params;
};
