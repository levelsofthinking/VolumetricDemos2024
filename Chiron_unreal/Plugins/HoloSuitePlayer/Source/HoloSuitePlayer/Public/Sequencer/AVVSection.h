// Copyright 2023 Arcturus Studios Holdings, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneSpawnable.h"
#include "HoloSuitePlayer.h"
#include "AVVSection.generated.h"

UCLASS()
class HOLOSUITEPLAYER_API UAVVSection : public UMovieSceneSection
{
public:

	GENERATED_BODY()

	UAVVSection(const FObjectInitializer& ObjInitializer);
	~UAVVSection();

	UPROPERTY(EditAnywhere, Category = "AVV")
	TSoftObjectPtr<AHoloSuitePlayer> Player;

	UPROPERTY(EditAnywhere, Category = "AVV")
	FGuid SpawnableGUID;

	UPROPERTY(EditAnywhere, Category = "AVV")
	int StartFrameOffset;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
