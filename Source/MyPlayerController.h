// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GameFramework/PlayerController.h"
#include "MyPlayerController.generated.h"

/**
*
*/
UCLASS()
class UETOPIACOMPETITIVE_API AMyPlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "UETOPIA", Server, Reliable, WithValidation)
		void RequestBeginPlay();

	FString PlayerUniqueNetId;

	UFUNCTION(BlueprintCallable, Category = "UETOPIA")
		void TravelPlayer(const FString& ServerUrl);


};
