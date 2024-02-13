// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#include "Sequencer/AVVSection.h"
#include "Misc/MessageDialog.h"

UAVVSection::UAVVSection(const FObjectInitializer& ObjInitializer)
	: Super(ObjInitializer)
{
	StartFrameOffset = 0;
}

UAVVSection::~UAVVSection()
{

}

#if WITH_EDITOR
void UAVVSection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == "Player")
	{
		if (Player)
		{
			//*
			// If you selected a spawnable instance for the HoloSuitePlayer, change out the soft reference with that 
			// of the template for the spawnable and set the GUID for the spawnable. This way a valid reference is serialised. 
			// The actual instance being affected is reevaluted in FAVVExecutionToken::Execute()

			UMovieScene* MovieScene = Cast<UMovieScene>(GetTypedOuter<UMovieScene>());			
			SpawnableGUID = FGuid();

			for (int i = 0; i < MovieScene->GetSpawnableCount(); ++i)
			{
				FMovieSceneSpawnable* Spawnable = &MovieScene->GetSpawnable(i);

				// Check if a spawnable is compatible with the player instance
				// TODO: Find a cheaper way to check which spawnable generated the instance
				FString Label = Player->GetActorLabel();
				if (!Label.IsEmpty() && Label.Equals(Spawnable->GetName()))
				{
					SpawnableGUID = Spawnable->GetGuid();
					Player = Spawnable->GetObjectTemplate();
				}
			}
			//Player->ExternalTiming = true;
			Player->SetAVVPlaybackParameters(true, Player->PlayOnOpen, Player->Loop, Player->PingPong, Player->Reverse, Player->FrameRate, Player->CurrentFrame);
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif