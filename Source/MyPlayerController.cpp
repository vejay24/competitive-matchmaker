// Fill out your copyright notice in the Description page of Project Settings.

#include "UEtopiaCompetitive.h"
#include "MyGameInstance.h"
#include "MyPlayerController.h"


void AMyPlayerController::TravelPlayer(const FString& ServerUrl)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [AMyPlayerController] TransferPlayer - Client Side"));

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [AMyPlayerController] TransferPlayer ServerUrl: %s"), *ServerUrl);

	// Just testing to see if the client can just handle it without the server being involved
	// This does need a serverside check to prevent cheating, but for now this is fine.

	ClientTravel(ServerUrl, ETravelType::TRAVEL_Absolute);

}

void AMyPlayerController::RequestBeginPlay_Implementation()
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [AMyPlayerController] RequestBeginPlay"));

	// We just want to accept the request here from the player and have the game instance do the transfer.

	UMyGameInstance* TheGameInstance = Cast<UMyGameInstance>(GetWorld()->GetGameInstance());
	//TheGameInstance->RequestBeginPlay();

}
bool AMyPlayerController::RequestBeginPlay_Validate()
{
	return true;
}

