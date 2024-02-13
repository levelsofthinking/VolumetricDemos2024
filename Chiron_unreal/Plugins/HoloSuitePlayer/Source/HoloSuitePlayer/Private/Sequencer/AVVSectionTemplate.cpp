// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.


#include "Sequencer/AVVSectionTemplate.h"
#include "Sequencer/AVVSection.h"
#include "Sequencer/HoloSuiteTrack.h"

#include "UObject/Package.h"
#include "UObject/GCObject.h"
#include "MovieScene.h"

#include "Engine/Texture2D.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/TextureRenderTarget2D.h"

struct FAVVSequenceData : PropertyTemplate::FSectionData
{
	TSoftObjectPtr<AHoloSuitePlayer> Player;
	FGuid SpawnableGUID;
};

struct FAVVPreRollExecutionToken : IMovieSceneExecutionToken
{
	float SequenceTime;
	FAVVPreRollExecutionToken(float InSequenceTime) : SequenceTime(InSequenceTime) {}

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
		//
	}
};

struct FAVVExecutionToken : IMovieSceneExecutionToken
{
	float SectionTime;
	double DisplayRate;
	int StartFrameOffset;

	FAVVExecutionToken(float InSectionTime, double InDisplayRate, int InStartFrameOffset) 
		: SectionTime(InSectionTime), DisplayRate(InDisplayRate), StartFrameOffset(InStartFrameOffset) 
	{}

	~FAVVExecutionToken()
	{}

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
		FAVVSequenceData& SectionData = PersistentData.GetSectionData<FAVVSequenceData>();

		// Update player to current spawned instance if using Spawnable AVV
		if (SectionData.SpawnableGUID.IsValid())
		{
			UObject* SpawnedObject = Player.GetSpawnRegister().FindSpawnedObject(SectionData.SpawnableGUID, Operand.SequenceID).Get();
			SectionData.Player = Cast<AHoloSuitePlayer>(SpawnedObject);
		}

		if (SectionData.Player)
		{
			int32 FrameNumber = StartFrameOffset + FMath::RoundToInt(SectionTime * DisplayRate);

			UAVVPlayerComponent* AVVPlayerComponent = SectionData.Player->GetAVVPlayerComponent();
			if (AVVPlayerComponent == nullptr)
			{
				return;
			}

			UAVVDecoder* AVVDecoder = AVVPlayerComponent->GetDecoder();
			if (AVVDecoder)
			{
				int32 FrameCount = AVVDecoder->FrameCount;
				if (FrameCount > 0)
				{
					FrameNumber = FrameNumber % FrameCount;
				}
			}

			SectionData.Player->CurrentFrame = FrameNumber;
			AVVPlayerComponent->CurrentFrame = FrameNumber;
		}
	}
};

FAVVSectionParams::FAVVSectionParams()
	: SectionStartTime(), Player(nullptr), StartFrameOffset(0), MovieScene(nullptr)
{}

FAVVSectionParams::~FAVVSectionParams()
{}

FAVVSectionTemplate::FAVVSectionTemplate()
	: PropertyData(), Params()
{}

FAVVSectionTemplate::FAVVSectionTemplate(const UAVVSection& InSection, const UHoloSuiteTrack& InTrack) //const UHoloSuiteTrack& InTrack)
{
	if (InSection.SpawnableGUID.IsValid())
	{
		Params.SpawnableGUID = InSection.SpawnableGUID;
	}

	Params.Player = InSection.Player;
	Params.StartFrameOffset = InSection.StartFrameOffset;
	Params.SectionStartTime = InSection.GetInclusiveStartFrame();
	Params.MovieScene = InSection.GetTypedOuter<UMovieScene>();
}

FAVVSectionTemplate::~FAVVSectionTemplate()
{
	Params.Reset();
}

void FAVVSectionTemplate::Setup(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const
{
	if (Params.Player)
	{
		PropertyData.SetupTrack<FAVVSequenceData>(PersistentData);
		PersistentData.GetSectionData<FAVVSequenceData>().Player = Params.Player;
		PersistentData.GetSectionData<FAVVSequenceData>().SpawnableGUID = Params.SpawnableGUID;
	}
}

void FAVVSectionTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	if (!Params.Player || Context.IsPostRoll())
	{
		return;
	}

	if (Context.IsPreRoll())
	{
		const float SegmentTime =
			Context.HasPreRollEndTime() ? FFrameTime::FromDecimal((FFrameTime(Context.GetPreRollEndFrame()) - FFrameTime(Params.SectionStartTime)).AsDecimal()) / Context.GetFrameRate() : 0.f;

		ExecutionTokens.Add(FAVVPreRollExecutionToken(SegmentTime));
	}
	else
	{
		const float SegmentTime = FFrameTime::FromDecimal((Context.GetTime() - FFrameTime(Params.SectionStartTime)).AsDecimal()) / Context.GetFrameRate();

		const double DisplayRate = Params.MovieScene->GetDisplayRate().AsDecimal();

		ExecutionTokens.Add(FAVVExecutionToken(SegmentTime, DisplayRate, Params.StartFrameOffset));
	}
}
