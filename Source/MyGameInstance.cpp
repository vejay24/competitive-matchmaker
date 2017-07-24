// Fill out your copyright notice in the Description page of Project Settings.

#include "Comp.h"
#include "MyGameState.h"
#include "MyGameSession.h"
#include "MyPlayerState.h"
#include "MyPlayerController.h"
//#include "MyServerPortalActor.h"
//#include "MyTravelApprovedActor.h"
//#include "MyRewardItemActor.h"
#include "MyGameInstance.h"





namespace MyGameInstanceState
{
	const FName None = FName(TEXT("None"));
	const FName PendingInvite = FName(TEXT("PendingInvite"));
	const FName WelcomeScreen = FName(TEXT("WelcomeScreen"));
	const FName MainMenu = FName(TEXT("MainMenu"));
	const FName MessageMenu = FName(TEXT("MessageMenu"));
	const FName Playing = FName(TEXT("Playing"));
}

UMyGameInstance::UMyGameInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bIsOnline(true) // Default to online
	, bIsLicensed(true) // Default to licensed (should have been checked by OS on boot)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE CONSTRUCTOR"));
	CurrentState = MyGameInstanceState::None;

	//Trying to see if some kind of init is needed
	// does not seem to make a difference
	//PlayerRecord.ActivePlayers.Init();

	ServerSessionHostAddress = NULL;
	ServerSessionID = NULL;

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE INIT"));

	if (!GConfig) return;

	_configPath = FPaths::SourceConfigDir();
	_configPath += TEXT("UEtopiaConfig.ini");
	// This is deceiving because it looks like we're loading these settings from the UEtopiaConfig.ini config file,
	// but really it's loading from DefaultGame.ini

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] set up path"));


	GConfig->GetString(
		TEXT("UEtopia.Client"),
		TEXT("UEtopiaMode"),
		UEtopiaMode,
		GGameIni
		);

	GConfig->GetString(
		TEXT("UEtopia.Client"),
		TEXT("APIURL"),
		APIURL,
		GGameIni
		);

	GConfig->GetString(
		TEXT("UEtopia.Client"),
		TEXT("ServerSecret"),
		ServerAPISecret,
		GGameIni
		);

	GConfig->GetString(
		TEXT("UEtopia.Client"),
		TEXT("ServerAPIKey"),
		ServerAPIKey,
		GGameIni
		);

	GConfig->GetString(
		TEXT("UEtopia.Client"),
		TEXT("GameKey"),
		GameKey,
		GGameIni
		);

	bRequestBeginPlayStarted = false;
	// I don't think we want this here.
	//GetServerInfo();

	// set up our reward spawning values
	bSpawnRewardsEnabled = true;
	SpawnRewardServerMinimumBalanceRequired = 100000;
	SpawnRewardChance = .50f;
	SpawnRewardValue = 250;
	SpawnRewardTimerSeconds = 30.0f;

	SpawnRewardLocationXMin = -1610.0f;
	SpawnRewardLocationXMax = 1000.0f;
	SpawnRewardLocationYMin = -1350.0f;
	SpawnRewardLocationYMax = 1350.0f;
	SpawnRewardLocationZMin = 130.0f;
	SpawnRewardLocationZMax = 1350.0f;

	CubeStoreCost = 100;

	MinimumKillsBeforeResultsSubmit = 3; // When a player has a score of x, submit match results

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE CONSTRUCTOR - DONE"));
}

void UMyGameInstance::Init()
{
	Super::Init();

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE INIT"));

	//IgnorePairingChangeForControllerId = -1;
	CurrentConnectionStatus = EOnlineServerConnectionStatus::Connected;

	// game requires the ability to ID users.
	const auto OnlineSub = IOnlineSubsystem::Get();
	check(OnlineSub);
	const auto IdentityInterface = OnlineSub->GetIdentityInterface();
	check(IdentityInterface.IsValid());

	const auto SessionInterface = OnlineSub->GetSessionInterface();
	check(SessionInterface.IsValid());

	const auto FriendsInterface = OnlineSub->GetFriendsInterface();
	check(FriendsInterface.IsValid());

	// bind any OSS delegates we needs to handle
	for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i)
	{
		IdentityInterface->AddOnLoginStatusChangedDelegate_Handle(i, FOnLoginStatusChangedDelegate::CreateUObject(this, &UMyGameInstance::HandleUserLoginChanged));
		IdentityInterface->AddOnLoginCompleteDelegate_Handle(i, FOnLoginCompleteDelegate::CreateUObject(this, &UMyGameInstance::HandleUserLoginComplete));
		// moved this to player controller.  We don't want it here.
		//FriendsInterface->AddOnFriendsChangeDelegate_Handle(i, FOnFriendsChangeDelegate::CreateUObject(this, &UMyGameInstance::OnFriendsChange));
	}

	SessionInterface->AddOnSessionFailureDelegate_Handle(FOnSessionFailureDelegate::CreateUObject(this, &UMyGameInstance::HandleSessionFailure));

	

	OnEndSessionCompleteDelegate = FOnEndSessionCompleteDelegate::CreateUObject(this, &UMyGameInstance::OnEndSessionComplete);
	//OnCreateSessionCompleteDelegate = FOnCreateSessionCompleteDelegate::CreateUObject(this, &UMyGameInstance::OnCreateSessionComplete);

	// Trying to cancel matchmaking when the world is destroyed...  Can't get it.
	//OnPreWorldFinishDestroy.AddDynamic(this, &AMyPlayerController::TestFunction);
	//FWorldDelegates::OnPreWorldFinishDestroy.Add( &UMyGameInstance::CancelMatchmaking);

	// TODO:  Bind to the FCoreDelegates::OnPreExit multicast delegate

	// Not sure if we want this here, but we need to run a serverside check for active sessions
	// does not work without the user - overrode it
	// this still does not work because we don't have a game session for some reason.

	/*
	if (IsRunningDedicatedServer()) {
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE RUNNING ON DEDICATED SERVER"));
	FindSessions(false);
	}
	*/

	GetWorld()->GetTimerManager().SetTimer(ServerLinksTimerHandle, this, &UMyGameInstance::GetServerLinks, 20.0f, true);
	GetWorld()->GetTimerManager().SetTimer(RewardSpawnTimerHandle, this, &UMyGameInstance::AttemptSpawnReward, SpawnRewardTimerSeconds, true);




}

AMyGameSession* UMyGameInstance::GetGameSession() const
{
	//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::GetGameSession"));
	UWorld* const World = GetWorld();
	if (World)
	{
		//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] World Found"));
		AGameMode* const Game = Cast<AGameMode>(World->GetAuthGameMode());
		if (Game)
		{
			//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] Game Found"));
			return Cast<AMyGameSession>(Game->GameSession);
		}
	}

	return nullptr;
}

// prototype http function
bool UMyGameInstance::PerformHttpRequest(void(UMyGameInstance::*delegateCallback)(FHttpRequestPtr, FHttpResponsePtr, bool), FString APIURI, FString ArgumentString)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] PerformHttpRequest"));

	FHttpModule* Http = &FHttpModule::Get();
	if (!Http) { return false; }
	if (!Http->IsHttpEnabled()) { return false; }

	FString TargetHost = "http://" + APIURL + APIURI;

	UE_LOG(LogTemp, Log, TEXT("TargetHost: %s"), *TargetHost);
	//UE_LOG(LogTemp, Log, TEXT("ServerAPIKey: %s"), *ServerAPIKey);
	//UE_LOG(LogTemp, Log, TEXT("ServerAPISecret: %s"), *ServerAPISecret);

	TSharedRef < IHttpRequest > Request = Http->CreateRequest();
	Request->SetVerb("POST");
	Request->SetURL(TargetHost);
	Request->SetHeader("User-Agent", "UETOPIA_UE4_API_CLIENT/1.0");
	Request->SetHeader("Content-Type", "application/x-www-form-urlencoded");
	Request->SetHeader("Key", ServerAPIKey);
	Request->SetHeader("Sign", "RealSignatureComingIn411");
	Request->SetContentAsString(ArgumentString);

	Request->OnProcessRequestComplete().BindUObject(this, delegateCallback);
	if (!Request->ProcessRequest()) { return false; }

	return true;
}

bool UMyGameInstance::PerformJsonHttpRequest(void(UMyGameInstance::*delegateCallback)(FHttpRequestPtr, FHttpResponsePtr, bool), FString APIURI, FString ArgumentString)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] PerformHttpRequest"));

	FHttpModule* Http = &FHttpModule::Get();
	if (!Http) { return false; }
	if (!Http->IsHttpEnabled()) { return false; }

	FString TargetHost = "http://" + APIURL + APIURI;

	UE_LOG(LogTemp, Log, TEXT("TargetHost: %s"), *TargetHost);
	UE_LOG(LogTemp, Log, TEXT("ServerAPIKey: %s"), *ServerAPIKey);
	UE_LOG(LogTemp, Log, TEXT("ServerAPISecret: %s"), *ServerAPISecret);

	TSharedRef < IHttpRequest > Request = Http->CreateRequest();
	Request->SetVerb("POST");
	Request->SetURL(TargetHost);
	Request->SetHeader("User-Agent", "UETOPIA_UE4_API_CLIENT/1.0");
	//Request->SetHeader("Content-Type", "application/x-www-form-urlencoded");
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	Request->SetHeader("Key", ServerAPIKey);
	Request->SetHeader("Sign", "RealSignatureComingIn411");
	Request->SetContentAsString(ArgumentString);

	Request->OnProcessRequestComplete().BindUObject(this, delegateCallback);
	if (!Request->ProcessRequest()) { return false; }

	return true;
}

bool UMyGameInstance::GetServerInfo()
{

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] GetServerInfo"));
	FString nonceString = "10951350917635";
	FString encryption = "off";  // Allowing unencrypted on sandbox for now.  
	FString OutputString = "nonce=" + nonceString + "&encryption=" + encryption;

	if (ServerSessionHostAddress.Len() > 1) {
		//UE_LOG(LogTemp, Log, TEXT("ServerSessionHostAddress: %s"), *ServerSessionHostAddress);
		//UE_LOG(LogTemp, Log, TEXT("ServerSessionID: %s"), *ServerSessionID);
		OutputString = OutputString + "&session_host_address=" + ServerSessionHostAddress + "&session_id=" + ServerSessionID;
	}
	FString APIURI = "/api/v1/server/info";
	bool requestSuccess = PerformHttpRequest(&UMyGameInstance::GetServerInfoComplete, APIURI, OutputString);
	return requestSuccess;
}

void UMyGameInstance::GetServerInfoComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());

		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization True"));
				// Set up our instance variables

				if (JsonParsed->GetIntegerField("incrementCurrency")) {
					incrementCurrency = JsonParsed->GetIntegerField("incrementCurrency");
				}
				if (JsonParsed->GetIntegerField("minimumCurrencyRequired")) {
					minimumCurrencyRequired = JsonParsed->GetIntegerField("minimumCurrencyRequired");
				}
				if (JsonParsed->GetIntegerField("serverCurrency")) {
					serverCurrency = JsonParsed->GetIntegerField("serverCurrency");
				}


				ServerTitle = JsonParsed->GetStringField("title");

				AGameState* gameState = Cast<AGameState>(GetWorld()->GetGameState());
				AMyGameState* uetopiaGameState = Cast<AMyGameState>(gameState);
				//AMyPlayerState* playerS = Cast<AMyPlayerState>(thisPlayerState);
				uetopiaGameState->serverTitle = ServerTitle;

				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetServerInfoComplete] ServerTitle: %s"), *ServerTitle);

			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization False"));
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetServerInfoComplete] Done!"));
}


bool UMyGameInstance::GetMatchInfo()
{

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] GetMatchInfo"));
	FString nonceString = "10951350917635";
	FString encryption = "off";  // Allowing unencrypted on sandbox for now.  
	FString OutputString = "nonce=" + nonceString + "&encryption=" + encryption;

	if (ServerSessionHostAddress.Len() > 1) {
		//UE_LOG(LogTemp, Log, TEXT("ServerSessionHostAddress: %s"), *ServerSessionHostAddress);
		//UE_LOG(LogTemp, Log, TEXT("ServerSessionID: %s"), *ServerSessionID);
		OutputString = OutputString + "&session_host_address=" + ServerSessionHostAddress + "&session_id=" + ServerSessionID;
	}
	FString APIURI = "/api/v1/matchmaker/info";
	bool requestSuccess = PerformHttpRequest(&UMyGameInstance::GetMatchInfoComplete, APIURI, OutputString);
	return requestSuccess;
}

void UMyGameInstance::GetMatchInfoComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());

		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization True"));
				// Set up our instance variables

				if (JsonParsed->GetIntegerField("admissionFee")) {
					admissionFee = JsonParsed->GetIntegerField("admissionFee");
				}


				// Put all of our player information in our MatchInfo TArray

				FJsonObjectConverter::JsonObjectStringToUStruct<FMyMatchInfo>(
					JsonRaw,
					&MatchInfo,
					0, 0);

				// Put replicated data in game state
				AGameState* gameState = Cast<AGameState>(GetWorld()->GetGameState());
				AMyGameState* uetopiaGameState = Cast<AMyGameState>(gameState);

				// Also build out the team struct for display purposes
				// Empty anything that might have been there
				uetopiaGameState->TeamList.teams.Empty();
				// First, we need to know how many teams there are

				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetMatchInfo] Building Team list"));
				for (int32 b = 0; b < MatchInfo.players.Num(); b++) {
					if (teamCount < MatchInfo.players[b].teamId) {
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetMatchInfo] Found team with higher Id"));
						teamCount = MatchInfo.players[b].teamId;
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetMatchInfo] teamCount %d"), teamCount);
					}
				}

				// Then, we can start to build the struct, by team.
				// b is our team index
				for (int32 b = 0; b <= teamCount; b++) {
					UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetMatchInfo] Found team %d"), b);
					FMyTeamInfo thisTeam;
					// Get the players that are in this team
					// TODO make this a function instead
					for (int32 i = 0; i < MatchInfo.players.Num(); i++) {
						if (b == MatchInfo.players[i].teamId) {
							UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetMatchInfo] Found player in this team"));
							thisTeam.players.Add(MatchInfo.players[i]);
							thisTeam.number = b;
							thisTeam.title = MatchInfo.players[i].teamTitle;
						}
					}
					uetopiaGameState->TeamList.teams.Add(thisTeam);
				}

				MatchTitle = JsonParsed->GetStringField("title");

				//AMyPlayerState* playerS = Cast<AMyPlayerState>(thisPlayerState);
				uetopiaGameState->MatchTitle = MatchTitle;

				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetMatchInfo] MatchTitle: %s"), *MatchTitle);

			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization False"));
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetMatchInfo] Done!"));
}

void  UMyGameInstance::GetServerLinks()
{
	if (IsRunningDedicatedServer()) {
		if (bRequestBeginPlayStarted) {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] GetServerLinks"));
			FString nonceString = "10951350917635";
			FString encryption = "off";  // Allowing unencrypted on sandbox for now.  
			FString OutputString = "nonce=" + nonceString + "&encryption=" + encryption;
			FString APIURI = "/api/v1/server/links";
			bool requestSuccess = PerformHttpRequest(&UMyGameInstance::GetServerLinksComplete, APIURI, OutputString);
			// tell the game state to spawn them
			//AMyGameState* const MyGameState = GetWorld() != NULL ? GetWorld()->GetGameState<AMyGameState>() : NULL;
			//MyGameState->SpawnServerPortals();
		}

	}
}

/*
bool UMyGameInstance::GetServerLinks()
{

UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] GetServerLinks"));
FString nonceString = "10951350917635";
FString encryption = "off";  // Allowing unencrypted on sandbox for now.
FString OutputString = "nonce=" + nonceString + "&encryption=" + encryption;
FString APIURI = "/api/v1/server/links";
bool requestSuccess = PerformHttpRequest(&UMyGameInstance::GetServerLinksComplete, APIURI, OutputString);
return requestSuccess;
}
*/


void UMyGameInstance::GetServerLinksComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());

		const FString JsonRaw = *HttpResponse->GetContentAsString();

		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			//UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				//UE_LOG(LogTemp, Log, TEXT("Authorization True"));

				// parse JSON into game instance
				bool jsonConvertSuccess = FJsonObjectConverter::JsonObjectStringToUStruct<FMyServerLinks>(*JsonRaw, &ServerLinks, 0, 0);

				if (jsonConvertSuccess) {
					//UE_LOG(LogTemp, Log, TEXT("jsonConvertSuccess"));
				}
				else {
					//UE_LOG(LogTemp, Log, TEXT("jsonConvertFAIL"));
				}

				/*
				UE_LOG(LogTemp, Log, TEXT("Found %d Server Links"), ServerLinks.links.Num());
				for (int32 b = 0; b < ServerLinks.links.Num(); b++)
				{
				UE_LOG(LogTemp, Log, TEXT("targetServerTitle: %s"), *ServerLinks.links[b].targetServerTitle);
				UE_LOG(LogTemp, Log, TEXT("hostConnectionLink: %s"), *ServerLinks.links[b].hostConnectionLink);

				}
				*/

			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization False"));
			}
		}
	}
	//SpawnServerPortals();
	//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetServerLinksComplete] Done!"));
}

bool UMyGameInstance::ActivatePlayer(class AMyPlayerController* NewPlayerController, FString playerKeyId, int32 playerID, const FUniqueNetIdRepl& UniqueId)
{

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] ActivatePlayer"));
	//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] DEBUG TEST"));

	// check to see if this player is in the active list already
	bool playerKeyIdFound = false;
	int32 ActivePlayerIndex = 0;

	// Normally, here we have a new player connecting
	// We don't have the unreal engine playerID yet.  Not sure why this is...  TODO investigate
	// So, we're going to store the player's playerKeyId in the player state itself.
	// This way we can iterate over the player controllers at a later stage, and check for the playerKeyID

	// Stick the playerKeyID in the playerState so we can find this player later.
	APlayerState* thisPlayerState = NewPlayerController->PlayerState;
	AMyPlayerState* playerS = Cast<AMyPlayerState>(thisPlayerState);
	playerS->playerKeyId = playerKeyId;

	if (UEtopiaMode == "competitive") {
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - Mode set to competitive"));

		// on competitive, if a connecting player is not in the matchInfo, just kick them.
		for (int32 b = 0; b < MatchInfo.players.Num(); b++)
		{

			if (MatchInfo.players[b].userKeyId == playerKeyId) {
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - FOUND MATCHING playerKeyId"));
				playerKeyIdFound = true;
				ActivePlayerIndex = b;
			}
		}

		if (playerKeyIdFound == false) {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - playerKeyId NOT found in matchInfo- TODO kick"));
		}
		else {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - playerKeyId found in matchInfo"));
			FString nonceString = "10951350917635";
			FString encryption = "off";  // Allowing unencrypted on sandbox for now.  
			FString OutputString = "nonce=" + nonceString + "&encryption=" + encryption;

			FString APIURI = "/api/v1/match/player/" + playerKeyId + "/activate";;

			bool requestSuccess = PerformHttpRequest(&UMyGameInstance::ActivateMatchPlayerRequestComplete, APIURI, OutputString);

			return requestSuccess;
		}




	}
	else {
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - Mode set to continuous"));
		for (int32 b = 0; b < PlayerRecord.ActivePlayers.Num(); b++)
		{
			//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [AuthorizePlayer] playerKeyId: %s"), *PlayerRecord.ActivePlayers[b].playerKeyId);
			if (PlayerRecord.ActivePlayers[b].playerKeyId == playerKeyId) {
				//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - FOUND MATCHING playerKeyId"));
				playerKeyIdFound = true;
				ActivePlayerIndex = b;
			}
		}



		if (playerKeyIdFound == false) {
			//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - No existing playerKeyId found"));

			if (isdigit(playerID)) {
				//UE_LOG(LogTemp, Log, TEXT("playerID: %s"), playerID);
			}
			else {
				//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - THERE WAS NO playerID incoming"));
				// This is sadly what we expect to see.  
			}

			// add the player to the TArray as authorized=false
			FMyActivePlayer activeplayer;
			activeplayer.playerID = playerID; // UE internal player reference
			activeplayer.playerKeyId = playerKeyId; // UEtopia player reference
													//activeplayer.playerKeyId = nullptr;
			activeplayer.playerTitle = nullptr;
			activeplayer.authorized = false;
			activeplayer.roundDeaths = 0;
			activeplayer.roundKills = 0;
			//activeplayer.killed = nullptr;
			activeplayer.Rank = 1600;
			activeplayer.currencyCurrent = 10;
			activeplayer.gamePlayerKeyId = nullptr;
			activeplayer.UniqueId = UniqueId;


			PlayerRecord.ActivePlayers.Add(activeplayer);

			// We might need to find the GetPreferredUniqueNetId, which is part of a localplayer and is used for session travel.
			//NewPlayerController->G





			//UE_LOG(LogTemp, Log, TEXT("playerID: %s"), playerID);
			//UE_LOG(LogTemp, Log, TEXT("playerKeyId: %s"), playerKeyId);
			//UE_LOG(LogTemp, Log, TEXT("Object is: %s"), *GetName());

			//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - Debug 2d"));

			FString nonceString = "10951350917635";
			FString encryption = "off";  // Allowing unencrypted on sandbox for now.  

			FString OutputString = "nonce=" + nonceString + "&encryption=" + encryption;

			//UE_LOG(LogTemp, Log, TEXT("ServerSessionHostAddress: %s"), *ServerSessionHostAddress);
			//UE_LOG(LogTemp, Log, TEXT("ServerSessionID: %s"), *ServerSessionID);

			if (ServerSessionHostAddress.Len() > 1) {
				OutputString = OutputString + "&session_host_address=" + ServerSessionHostAddress + "&session_id=" + ServerSessionID;
			}

			FString APIURI = "/api/v1/server/player/" + playerKeyId + "/activate";;

			bool requestSuccess = PerformHttpRequest(&UMyGameInstance::ActivateRequestComplete, APIURI, OutputString);

			return requestSuccess;
		}
		else {
			//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - update existing record"));
			PlayerRecord.ActivePlayers[ActivePlayerIndex].playerID = playerID;
			FString nonceString = "10951350917635";
			FString encryption = "off";  // Allowing unencrypted on sandbox for now.  

			FString OutputString = "nonce=" + nonceString + "&encryption=" + encryption;

			//UE_LOG(LogTemp, Log, TEXT("ServerSessionHostAddress: %s"), *ServerSessionHostAddress);
			//UE_LOG(LogTemp, Log, TEXT("ServerSessionID: %s"), *ServerSessionID);

			if (ServerSessionHostAddress.Len() > 1) {
				OutputString = OutputString + "&session_host_address=" + ServerSessionHostAddress + "&session_id=" + ServerSessionID;
			}

			FString APIURI = "/api/v1/server/player/" + playerKeyId + "/activate";;

			bool requestSuccess = PerformHttpRequest(&UMyGameInstance::ActivateRequestComplete, APIURI, OutputString);

			return requestSuccess;
		}
		return false;
	}
	return false;

}

void UMyGameInstance::ActivateRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());

		APlayerController* pc = NULL;
		int32 playerstateID;

		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization True"));
				bool PlayerAuthorized = JsonParsed->GetBoolField("player_authorized");
				if (PlayerAuthorized) {
					UE_LOG(LogTemp, Log, TEXT("Player Authorized"));

					int32 activeAuthorizedPlayers = 0;
					int32 activePlayerIndex;



					// TODO refactor this using our get_by_id function

					//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] ActivePlayers.Num() > 0"));
					for (int32 b = 0; b < PlayerRecord.ActivePlayers.Num(); b++)
					{
						//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] playerKeyId: %s"), *PlayerRecord.ActivePlayers[b].playerKeyId);
						if (PlayerRecord.ActivePlayers[b].playerKeyId == JsonParsed->GetStringField("player_userid")) {
							//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - FOUND MATCHING playerKeyId"));
							activePlayerIndex = b;
							PlayerRecord.ActivePlayers[b].authorized = true;
							PlayerRecord.ActivePlayers[b].playerTitle = JsonParsed->GetStringField("player_name");
							PlayerRecord.ActivePlayers[b].playerKeyId = JsonParsed->GetStringField("user_key_id");
							PlayerRecord.ActivePlayers[b].currencyCurrent = JsonParsed->GetIntegerField("player_currency");
							PlayerRecord.ActivePlayers[b].Rank = JsonParsed->GetIntegerField("player_rank");
							PlayerRecord.ActivePlayers[b].gamePlayerKeyId = JsonParsed->GetStringField("game_player_key_id");

							// We need to go through the playercontrollerIterator to hunt for the playerKeyId
							//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - looking for a playerKeyId match"));
							for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
							{
								pc = Iterator->Get();

								APlayerState* thisPlayerState = pc->PlayerState;
								AMyPlayerState* playerS = Cast<AMyPlayerState>(thisPlayerState);

								if (playerS->playerKeyId == JsonParsed->GetStringField("user_key_id"))
								{
									//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - Found a playerState with matching playerKeyID"));
									//UE_LOG(LogTemp, Log, TEXT("playerS->PlayerId: %d"), thisPlayerState->PlayerId);
									PlayerRecord.ActivePlayers[b].playerID = thisPlayerState->PlayerId;
									playerS->Currency = JsonParsed->GetIntegerField("player_currency");
								}
							}

							// Since we have a match, we also want to get all of the game player data associated with this player.



							GetGamePlayer(JsonParsed->GetStringField("game_player_key_id"), true);

						}
						if (PlayerRecord.ActivePlayers[b].authorized) {
							activeAuthorizedPlayers++;
						}

					}

					// ALso set this player state playerName

					for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
					{
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - Looking for player to set name"));
						pc = Iterator->Get();

						/*
						AMyPlayerController* thisPlayerController = Cast<AMyPlayerController>(pc);

						if (thisPlayerController) {
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - Cast Controller success"));

						if (matchStarted) {
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - Match in progress - setting spectator"));
						thisPlayerController->PlayerState->bIsSpectator = true;
						thisPlayerController->ChangeState(NAME_Spectating);
						thisPlayerController->ClientGotoState(NAME_Spectating);
						}


						playerstateID = thisPlayerController->PlayerState->PlayerId;
						if (PlayerRecord[activePlayerIndex].playerID == playerstateID)
						{
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - playerID match - setting name"));
						thisPlayerController->PlayerState->SetPlayerName(JsonParsed->GetStringField("player_name"));
						}
						*/


					}

					/*
					if (activeAuthorizedPlayers >= MinimumPlayersNeededToStart)
					{
					matchStarted = true;
					// travel to the third person map
					FString UrlString = TEXT("/Game/ThirdPersonCPP/Maps/ThirdPersonExampleMap?listen");
					GetWorld()->GetAuthGameMode()->bUseSeamlessTravel = true;
					GetWorld()->ServerTravel(UrlString);
					}
					*/



				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("Player NOT Authorized"));

					// First grab the active player data from our struct
					int32 activePlayerIndex = 0;
					bool playerKeyIdFound = false;
					FString jsonplayerKeyId = JsonParsed->GetStringField("player_userid");

					for (int32 b = 0; b < PlayerRecord.ActivePlayers.Num(); b++)
					{
						//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] playerKeyId: %s"), *PlayerRecord.ActivePlayers[b].playerKeyId);
						//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] jsonplayerKeyId: %s"), *jsonplayerKeyId);
						if (PlayerRecord.ActivePlayers[b].playerKeyId == jsonplayerKeyId) {
							//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - Found active player record"));
							playerKeyIdFound = true;
							activePlayerIndex = b;
						}
					}




					if (playerKeyIdFound)
					{
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - playerKeyId is found - moving to kick"));
						for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
						{
							UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - Looking for player to kick"));
							pc = Iterator->Get();

							playerstateID = pc->PlayerState->PlayerId;
							if (PlayerRecord.ActivePlayers[activePlayerIndex].playerID == playerstateID)
							{
								UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - playerID match - kicking back to connect"));
								//FString UrlString = TEXT("/Game/MyConnectLevel");
								//ETravelType seamlesstravel = TRAVEL_Absolute;
								//thisPlayerController->ClientTravel(UrlString, seamlesstravel);
								// trying a session kick instead.
								// Get the Online Subsystem to work with
								IOnlineSubsystem* const OnlineSub = IOnlineSubsystem::Get();
								const FString kickReason = TEXT("Not Authorized");
								const FText kickReasonText = FText::FromString(kickReason);
								//AMyGameSession::KickPlayer(pc, kickReasonText);
								this->GetGameSession()->KickPlayer(pc, kickReasonText);
								FString badUrl = "";
								pc->ClientTravel(badUrl, ETravelType::TRAVEL_Absolute);

							}

						}
					}
				}
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] Done!"));

}


void UMyGameInstance::ActivateMatchPlayerRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());

		APlayerController* pc = NULL;
		int32 playerstateID;

		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization True"));
				bool PlayerAuthorized = JsonParsed->GetBoolField("player_authorized");
				if (PlayerAuthorized) {
					UE_LOG(LogTemp, Log, TEXT("Player Authorized"));

					int32 activeJoinedPlayers = 0;
					int32 totalExpectedPlayers = 0;
					int32 activePlayerIndex;



					// TODO refactor this using our get_by_id function

					//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] ActivePlayers.Num() > 0"));
					for (int32 b = 0; b < MatchInfo.players.Num(); b++)
					{
						//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] playerKeyId: %s"), *PlayerRecord.ActivePlayers[b].playerKeyId);
						if (MatchInfo.players[b].userKeyId == JsonParsed->GetStringField("player_userid")) {
							//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - FOUND MATCHING playerKeyId"));
							activePlayerIndex = b;

							// set status flags
							MatchInfo.players[b].joined = true;
							MatchInfo.players[b].currentRoundAlive = true;

							for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
							{
								pc = Iterator->Get();

								// go through the player states to find this player.  
								// we need to set the playerId in here.

								APlayerState* thisPlayerState = pc->PlayerState;
								AMyPlayerState* playerS = Cast<AMyPlayerState>(thisPlayerState);

								if (playerS->playerKeyId == JsonParsed->GetStringField("user_key_id"))
								{
									UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateMatchPlayerRequestComplete] - Found a playerState with matching playerKeyID"));
									//UE_LOG(LogTemp, Log, TEXT("playerS->PlayerId: %d"), thisPlayerState->PlayerId);
									MatchInfo.players[b].playerID = thisPlayerState->PlayerId;
								}
							}

						}
						totalExpectedPlayers++;

						if (MatchInfo.players[b].joined) {
							activeJoinedPlayers++;
						}

					}

					// Also set up the team list struct.
					// This is mostly for convenience in displaying teams in-game.  The authority of data is the MatchInfo.

					// Put replicated data in game state
					AGameState* gameState = Cast<AGameState>(GetWorld()->GetGameState());
					AMyGameState* uetopiaGameState = Cast<AMyGameState>(gameState);

					// b is the team index
					for (int32 b = 0; b < uetopiaGameState->TeamList.teams.Num(); b++)
					{
						// i is the player index for the team
						for (int32 i = 0; i < uetopiaGameState->TeamList.teams[b].players.Num(); i++)
						{
							//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] playerKeyId: %s"), *PlayerRecord.ActivePlayers[b].playerKeyId);
							if (uetopiaGameState->TeamList.teams[b].players[i].userKeyId == JsonParsed->GetStringField("player_userid")) {
								UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - FOUND MATCHING playerKeyId in Game State.TeamList"));
								//activePlayerIndex = b;

								// set status flags
								uetopiaGameState->TeamList.teams[b].players[i].joined = true;

							}
						}

					}


					// ALso set this player state playerName

					for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
					{
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateMatchPlayerRequestComplete] - Looking for player to set name"));
						pc = Iterator->Get();

						/*
						AMyPlayerController* thisPlayerController = Cast<AMyPlayerController>(pc);

						if (thisPlayerController) {
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - Cast Controller success"));

						if (matchStarted) {
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - Match in progress - setting spectator"));
						thisPlayerController->PlayerState->bIsSpectator = true;
						thisPlayerController->ChangeState(NAME_Spectating);
						thisPlayerController->ClientGotoState(NAME_Spectating);
						}


						playerstateID = thisPlayerController->PlayerState->PlayerId;
						if (PlayerRecord[activePlayerIndex].playerID == playerstateID)
						{
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - playerID match - setting name"));
						thisPlayerController->PlayerState->SetPlayerName(JsonParsed->GetStringField("player_name"));
						}
						*/


					}


					if (activeJoinedPlayers >= totalExpectedPlayers)
					{
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateMatchPlayerRequestComplete] - activeJoinedPlayers >= totalExpectedPlayers - starting timer"));
						//matchStarted = true;

						// do it after a timer
						GetWorld()->GetTimerManager().SetTimer(AttemptStartMatchTimerHandle, this, &UMyGameInstance::AttemptStartMatch, 10.0f, false);
						// travel to the third person map
						//FString UrlString = TEXT("/Game/ThirdPersonCPP/Maps/ThirdPersonExampleMap?listen");
						//GetWorld()->GetAuthGameMode()->bUseSeamlessTravel = true;
						//GetWorld()->ServerTravel(UrlString);
					}




				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("Player NOT Authorized"));

					// First grab the active player data from our struct
					int32 activePlayerIndex = 0;
					bool playerKeyIdFound = false;
					FString jsonplayerKeyId = JsonParsed->GetStringField("player_userid");

					for (int32 b = 0; b < PlayerRecord.ActivePlayers.Num(); b++)
					{
						//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] playerKeyId: %s"), *PlayerRecord.ActivePlayers[b].playerKeyId);
						//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] jsonplayerKeyId: %s"), *jsonplayerKeyId);
						if (PlayerRecord.ActivePlayers[b].playerKeyId == jsonplayerKeyId) {
							//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - Found active player record"));
							playerKeyIdFound = true;
							activePlayerIndex = b;
						}
					}




					if (playerKeyIdFound)
					{
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateMatchPlayerRequestComplete] - playerKeyId is found - moving to kick"));
						for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
						{
							UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateMatchPlayerRequestComplete] - Looking for player to kick"));
							pc = Iterator->Get();

							playerstateID = pc->PlayerState->PlayerId;
							if (PlayerRecord.ActivePlayers[activePlayerIndex].playerID == playerstateID)
							{
								UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateMatchPlayerRequestComplete] - playerID match - kicking back to connect"));
								//FString UrlString = TEXT("/Game/MyConnectLevel");
								//ETravelType seamlesstravel = TRAVEL_Absolute;
								//thisPlayerController->ClientTravel(UrlString, seamlesstravel);
								// trying a session kick instead.
								// Get the Online Subsystem to work with
								IOnlineSubsystem* const OnlineSub = IOnlineSubsystem::Get();
								const FString kickReason = TEXT("Not Authorized");
								const FText kickReasonText = FText::FromString(kickReason);
								//AMyGameSession::KickPlayer(pc, kickReasonText);
								this->GetGameSession()->KickPlayer(pc, kickReasonText);
								FString badUrl = TEXT("/Game/EntryLevel");
								pc->ClientTravel(badUrl, ETravelType::TRAVEL_Absolute);

							}

						}
					}
				}
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateMatchPlayerRequestComplete] Done!"));

}

bool UMyGameInstance::GetGamePlayer(FString playerKeyId, bool bAttemptLock)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] GetGamePlayer"));

	FString nonceString = "10951350917635";
	FString encryption = "off";  // Allowing unencrypted on sandbox for now.  
	FString OutputString = "nonce=" + nonceString + "&encryption=" + encryption;

	if (bAttemptLock) {
		OutputString = OutputString + "&lock=True";
	}

	FString APIURI = "/api/v1/game/player/" + playerKeyId;

	bool requestSuccess = PerformHttpRequest(&UMyGameInstance::GetGamePlayerRequestComplete, APIURI, OutputString);

	return requestSuccess;
	return true;
}

void UMyGameInstance::GetGamePlayerRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());
		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			//UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				//UE_LOG(LogTemp, Log, TEXT("Authorization True"));

				// TODO - this can error out, and crash our server.
				// If the game player was not found, there is no userId to send back.
				// Check it first
				APlayerController* pc = NULL;
				FString playerKeyId = JsonParsed->GetStringField("userKeyId");

				FMyActivePlayer* activePlayer = getPlayerByPlayerKey(JsonParsed->GetStringField("userKeyId"));
				for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
				{
					//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetGamePlayerRequestComplete] - Looking for player Controller"));
					pc = Iterator->Get();
					APlayerState* thisPlayerState = pc->PlayerState;
					AMyPlayerState* playerS = Cast<AMyPlayerState>(thisPlayerState);

					FString playerstateplayerKeyId = playerS->playerKeyId;
					//UE_LOG(LogTemp, Log, TEXT("playerstateplayerKeyId: %s"), *playerstateplayerKeyId);
					FString playerArrayplayerKeyId = activePlayer->playerKeyId;
					//UE_LOG(LogTemp, Log, TEXT("playerArrayplayerKeyId: %s"), *playerArrayplayerKeyId);

					if (playerstateplayerKeyId == playerArrayplayerKeyId)
					{
						//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetGamePlayerRequestComplete] - playerKeyId match - Setting Game player state"));
						double scoreTemp;
						FString titleTemp = "None";
						JsonParsed->TryGetNumberField("score", scoreTemp);
						playerS->Score = scoreTemp;
						JsonParsed->TryGetNumberField("coordLocationX", playerS->savedCoordLocationX);
						JsonParsed->TryGetNumberField("coordLocationY", playerS->savedCoordLocationY);
						JsonParsed->TryGetNumberField("coordLocationZ", playerS->savedCoordLocationZ);
						JsonParsed->TryGetStringField("zoneName", playerS->savedZoneName);
						JsonParsed->TryGetStringField("zoneKey", playerS->savedZoneKey);
						JsonParsed->TryGetStringField("inventory", playerS->savedInventory);
						JsonParsed->TryGetStringField("equipment", playerS->savedEquipment);
						JsonParsed->TryGetStringField("abilities", playerS->savedAbilities);
						JsonParsed->TryGetStringField("interface", playerS->savedInterface);
						JsonParsed->TryGetStringField("userTitle", titleTemp);
						playerS->playerTitle = titleTemp;
						playerS->serverTitle = ServerTitle;
						//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetGamePlayerRequestComplete] playerS->playerTitle: %s"), *playerS->playerTitle);
						//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetGamePlayerRequestComplete] playerS->ServerTitle: %s"), *playerS->serverTitle);

						// TODO move player to the indicated position.

						// Deal with our inventory item Cube Count.
						// First try to parse the inventory as json.
						// If it fails or does not make sense, set it to our starting Cube Count value.
						FString InventoryJsonRaw = playerS->savedInventory;
						TSharedPtr<FJsonObject> InventoryJsonParsed;
						TSharedRef<TJsonReader<TCHAR>> InventoryJsonReader = TJsonReaderFactory<TCHAR>::Create(InventoryJsonRaw);
						if (FJsonSerializer::Deserialize(InventoryJsonReader, InventoryJsonParsed))
						{
							UE_LOG(LogTemp, Log, TEXT("InventoryJsonParsed"));
							UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetGamePlayerRequestComplete] InventoryJsonRaw: %s"), *InventoryJsonRaw);
							InventoryJsonParsed->TryGetNumberField("Cubes", playerS->InventoryCubes);
						}
						else {
							UE_LOG(LogTemp, Log, TEXT("InventoryJson Parse FAIL"));
							// setting default cubes
							playerS->InventoryCubes = 5;
						}

					}
				}
			}
		}
	}
}



bool UMyGameInstance::SaveGamePlayer(FString playerKeyId, bool bAttemptUnLock)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] SaveGamePlayer"));

	// get our player state so we can access all of the saved data

	APlayerController* pc = NULL;

	FMyActivePlayer* activePlayer = getPlayerByPlayerKey(playerKeyId);

	FString InventoryOutputString;

	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetGamePlayerRequestComplete] - Looking for player Controller"));
		pc = Iterator->Get();
		APlayerState* thisPlayerState = pc->PlayerState;
		AMyPlayerState* playerS = Cast<AMyPlayerState>(thisPlayerState);

		FString playerstateplayerKeyId = playerS->playerKeyId;
		//UE_LOG(LogTemp, Log, TEXT("playerstateplayerKeyId: %s"), *playerstateplayerKeyId);
		//UE_LOG(LogTemp, Log, TEXT("playerKeyId: %s"), *playerKeyId);

		if (playerstateplayerKeyId == playerKeyId) {
			//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [GetGamePlayerRequestComplete] - Found match"));
			TSharedPtr<FJsonObject> InventoryJsonObject = MakeShareable(new FJsonObject);
			InventoryJsonObject->SetNumberField("Cubes", playerS->InventoryCubes);

			TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&InventoryOutputString);
			FJsonSerializer::Serialize(InventoryJsonObject.ToSharedRef(), Writer);
			//UE_LOG(LogTemp, Log, TEXT("[UMyGameInstance] InventoryOutputString: %s"), *InventoryOutputString);
		}
	}

	FString nonceString = "10951350917635";
	FString encryption = "off";  // Allowing unencrypted on sandbox for now.  
								 //FString OutputString = "nonce=" + nonceString + "&encryption=" + encryption;

								 //TSharedPtr<FJsonObject> PlayerJsonObj;
	TSharedPtr<FJsonObject> PlayerJsonObj = MakeShareable(new FJsonObject);
	PlayerJsonObj->SetStringField("nonce", "nonceString");
	PlayerJsonObj->SetStringField("encryption", encryption);
	PlayerJsonObj->SetStringField("inventory", InventoryOutputString);
	PlayerJsonObj->SetStringField("equipment", "hardcoded test");
	PlayerJsonObj->SetStringField("abilities", "hardcoded test");
	PlayerJsonObj->SetStringField("interface", "hardcoded test");
	PlayerJsonObj->SetNumberField("rank", 1600);
	PlayerJsonObj->SetNumberField("score", 1);
	PlayerJsonObj->SetNumberField("coordLocationX", 1);
	PlayerJsonObj->SetNumberField("coordLocationY", 1);
	PlayerJsonObj->SetNumberField("coordLocationZ", 1);
	PlayerJsonObj->SetStringField("zoneName", "hardcoded test");
	PlayerJsonObj->SetStringField("zoneKey", "hardcoded test");

	FString JsonOutputString;
	TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&JsonOutputString);
	FJsonSerializer::Serialize(PlayerJsonObj.ToSharedRef(), Writer);

	//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] SaveGamePlayer - setup output string"));

	//OutputString = OutputString + JsonOutputString;
	//UE_LOG(LogTemp, Log, TEXT("[UMyGameInstance] JsonOutputString: %s"), *JsonOutputString);
	if (bAttemptUnLock) {
		//OutputString = OutputString + "&unlock=True";
		PlayerJsonObj->SetBoolField("unlock", true);
	}

	FString APIURI = "/api/v1/game/player/" + activePlayer->gamePlayerKeyId + "/update";

	bool requestSuccess = PerformJsonHttpRequest(&UMyGameInstance::SaveGamePlayerRequestComplete, APIURI, JsonOutputString);


	return requestSuccess;
	//return true;
}

void UMyGameInstance::SaveGamePlayerRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());
		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			//UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization True"));
			}
		}
	}
}



bool UMyGameInstance::DeActivatePlayer(int32 playerID)
{
	if (IsRunningDedicatedServer()) {

		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] DeActivatePlayer"));
		//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] DEBUG TEST"));

		// check to see if this player is in the active list already
		bool playerIDFound = false;
		int32 ActivePlayerIndex = 0;
		//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] check to see if this player is in the active list already"));

		//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [AuthorizePlayer] ActivePlayers: %i"), ActivePlayers);

		FMyActivePlayer* CurrentActivePlayer = getPlayerByPlayerId(playerID);

		if (CurrentActivePlayer) {
			//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] DeAuthorizePlayer - existing playerID found"));

			FString GamePlayerKeyIdString = CurrentActivePlayer->gamePlayerKeyId;

			// update the TArray as authorized=false
			FMyActivePlayer leavingplayer;
			leavingplayer.playerID = playerID;
			leavingplayer.authorized = false;
			leavingplayer.playerKeyId = PlayerRecord.ActivePlayers[ActivePlayerIndex].playerKeyId;

			FString playerKeyId = PlayerRecord.ActivePlayers[ActivePlayerIndex].playerKeyId;

			// we don't want to totally overwrite it, just update it.
			//PlayerRecord.ActivePlayers[ActivePlayerIndex] = leavingplayer;
			PlayerRecord.ActivePlayers[ActivePlayerIndex].authorized = false;

			//UE_LOG(LogTemp, Log, TEXT("playerKeyId: %d"), *playerKeyId);
			//UE_LOG(LogTemp, Log, TEXT("Object is: %s"), *GetName());

			FString nonceString = "10951350917635";
			FString encryption = "off";  // Allowing unencrypted on sandbox for now.  


			FString OutputString;
			// TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<>::Create(&OutputString);
			// FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);

			// Build Params as text string
			OutputString = "nonce=" + nonceString + "&encryption=" + encryption;
			// urlencode the params

			FString APIURI = "/api/v1/server/player/" + playerKeyId + "/deactivate";;

			bool requestSuccess = PerformHttpRequest(&UMyGameInstance::DeActivateRequestComplete, APIURI, OutputString);

			SaveGamePlayer(CurrentActivePlayer->playerKeyId, true);

			//return requestSuccess;

			// TODO cleanup any TravelAuthorizedActors that this player owns.  

		}
		else {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] DeAuthorizePlayer - Not found - Ignoring"));
		}

		// Check to see if there are any more players, if not...  Save and prepare for server shutdown
		int32 authorizedPlayerCount = getActivePlayerCount();
		if (authorizedPlayerCount > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] DeAuthorizePlayer - There are still players authorized on this server."));
		}
		else {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] DeAuthorizePlayer - No Authorized players round - moving to save."));

			/*
			bool FileIOSuccess;
			bool allComponentsSaved;
			FString FileName = "serversavedata.dat";
			URamaSaveLibrary::RamaSave_SaveToFile(GetWorld(), FileName, FileIOSuccess, allComponentsSaved);
			if (FileIOSuccess) {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] File IO Success."));
			}
			else {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] File IO FAIL."));
			}
			*/



			//Return the server back to the lobbyLevel in case a user logs in before the server is decomissioned

			FString UrlString = TEXT("/Game/LobbyLevel?listen");
			//World'/Game/LobbyLevel.LobbyLevel'
			GetWorld()->GetAuthGameMode()->bUseSeamlessTravel = true;
			GetWorld()->ServerTravel(UrlString);

			// Reset our playstarted flag
			bRequestBeginPlayStarted = false;

			// Reset the ServerPortal Actor Array
			//ServerPortalActorReference.Empty();

		}
	}
	return true;

}

void UMyGameInstance::DeActivateRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());

		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			//  We don't care too much about the results from this call.  
			UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization True"));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization False"));
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [DeActivateRequestComplete] Done!"));
}


void UMyGameInstance::TransferPlayer(const FString& ServerKey, int32 playerID, bool checkOnly)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] TransferPlayer"));

	FString nonceString = "10951350917635";
	FString encryption = "off";  // Allowing unencrypted on sandbox for now.  
	FString checkOnlyStr = "";
	if (checkOnly) {
		checkOnlyStr = "true";
	}
	else {
		checkOnlyStr = "false";
	}
	FString OutputString = "nonce=" + nonceString + "&encryption=" + encryption + "&checkOnly=" + checkOnlyStr;

	FMyActivePlayer* playerRecord = getPlayerByPlayerId(playerID);
	if (playerRecord == nullptr)
	{
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] TransferPlayer - Player NOT found by Id"));
		return;
	}

	FString playerplayerKeyId = playerRecord->playerKeyId;

	FMyServerLink* serverRecord = getServerByKey(ServerKey);
	if (serverRecord->permissionCanUserTravel)
	{
		//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] TransferPlayer - Permission Granted"));
		FString APIURI = "/api/v1/server/" + ServerKey + "/player/" + playerplayerKeyId + "/transfer";
		bool requestSuccess = PerformHttpRequest(&UMyGameInstance::TransferPlayerRequestComplete, APIURI, OutputString);
		return;
	}
	else {
		//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] TransferPlayer - Permission NOT Granted"));
		return;
	}

}


void UMyGameInstance::TransferPlayerRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());
		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization True"));

				// Starting over with this

				// First check to see if this was a checkOnly request
				// checkOnly performs the tests, but does not initiate the transfer.  This allows us to populate the user's portal list:  ServerPortalKeyIdsAuthorized

				// If it was a full transfer request, we can do any cleanups of old stuff.
				// TODO - cleanups?

				// Print out our incoming information
				FString targetServerKeyId = JsonParsed->GetStringField("targetServerKeyId");
				//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [TransferPlayerRequestComplete] targetServerKeyId: %s"), *targetServerKeyId);
				FString userKeyId = JsonParsed->GetStringField("userKeyId");
				//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [TransferPlayerRequestComplete] userKeyId: %s"), *userKeyId);
				FString checkOnly = JsonParsed->GetStringField("checkOnly");
				//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [TransferPlayerRequestComplete] checkOnly: %s"), *checkOnly);
				bool allowedToTravel = JsonParsed->GetBoolField("success");
				if (allowedToTravel) {
					UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [TransferPlayerRequestComplete] allowedToTravel: TRUE"));
				}
				else {
					UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [TransferPlayerRequestComplete] allowedToTravel: FALSE"));
				}


				for (int i = 0; i < ServerLinks.links.Num(); i++)
				{
					UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [TransferPlayerRequestComplete] Found serverlink"));
				}

				if (checkOnly == "true") {
					// get the player
					FMyActivePlayer* ActivePlayer = getPlayerByPlayerKey(userKeyId);
					//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [TransferPlayerRequestComplete] ActivePlayer->UniqueId: %s"), *ActivePlayer->UniqueId->ToString());

					// we need our uetopiaplugcharacter to set the ServerPortalKeyIdsAuthorized
					// just put it in the controller? No it should go in the state
					// We need to go through the playercontrollerIterator to hunt for the playerKeyId
					//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [TransferPlayerRequestComplete] - looking for a playerKeyId match"));
					APlayerController* pc = NULL;
					for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
					{
						pc = Iterator->Get();

						APlayerState* thisPlayerState = pc->PlayerState;
						AMyPlayerState* playerS = Cast<AMyPlayerState>(thisPlayerState);

						if (playerS->playerKeyId == userKeyId)
						{
							//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [TransferPlayerRequestComplete] - Found a playerState with matching playerKeyID"));
							//UE_LOG(LogTemp, Log, TEXT("playerS->PlayerId: %d"), thisPlayerState->PlayerId);
							// add the serverkey to the TArray if authorized
							if (allowedToTravel) {
								playerS->ServerPortalKeyIdsAuthorized.Add(targetServerKeyId);

								// Spawn and setup the travelapproved actor visible only to the owner
								FMyServerLink thisServerLink = GetServerLinkByTargetServerKeyId(targetServerKeyId);

								// check in case the server link is not found or does not exist.
								if (thisServerLink.targetServerKeyId.IsEmpty()) {
									//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [TransferPlayerRequestComplete] - ServerLink was empty - aborting TravelApproval"));
									return;
								}

								/*
								UWorld* const World = GetWorld(); // get a reference to the world
								if (World) {
								//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [TransferPlayerRequestComplete] - Spawning a TravelApprovedActor"));
								FVector spawnlocation = FVector(thisServerLink.coordLocationX, thisServerLink.coordLocationY, thisServerLink.coordLocationZ);
								FTransform SpawnTransform = FTransform(spawnlocation);
								AMyTravelApprovedActor* const TravelApprovedActor = World->SpawnActor<AMyTravelApprovedActor>(AMyTravelApprovedActor::StaticClass(), SpawnTransform);
								TravelApprovedActor->setPlayerKeyId(userKeyId);
								TravelApprovedActor->SetOwner(pc);
								TravelApprovedActor->GetStaticMeshComponent()->SetOnlyOwnerSee(true);


								}
								*/


							}
						}
					}
				}
			}
		}
	}
}




bool UMyGameInstance::Purchase(FString playerKeyId, FString itemName, FString description, int32 amount)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] Purchase"));

	FString nonceString = "10951350917635";
	FString encryption = "off";  // Allowing unencrypted on sandbox for now.  

	TSharedPtr<FJsonObject> PlayerJsonObj = MakeShareable(new FJsonObject);
	PlayerJsonObj->SetStringField("nonce", "nonceString");
	PlayerJsonObj->SetStringField("encryption", encryption);
	PlayerJsonObj->SetStringField("itemName", itemName);
	PlayerJsonObj->SetStringField("description", description);
	PlayerJsonObj->SetNumberField("amount", amount);

	FString JsonOutputString;
	TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&JsonOutputString);
	FJsonSerializer::Serialize(PlayerJsonObj.ToSharedRef(), Writer);

	FString APIURI = "/api/v1/server/player/" + playerKeyId + "/purchase";

	bool requestSuccess = PerformJsonHttpRequest(&UMyGameInstance::PurchaseRequestComplete, APIURI, JsonOutputString);

	return requestSuccess;
}

void UMyGameInstance::PurchaseRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());
		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization True"));
				bool Success = JsonParsed->GetBoolField("success");
				if (Success) {
					UE_LOG(LogTemp, Log, TEXT("Success True"));
					FString userKeyId = JsonParsed->GetStringField("userKeyId");
					FString itemName = JsonParsed->GetStringField("itemName");
					int32 purchaseAmount = JsonParsed->GetNumberField("amount");

					FMyActivePlayer* playerRecord = getPlayerByPlayerKey(userKeyId);
					if (playerRecord == nullptr)
					{
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] TransferPlayer - Player NOT found by Key"));
						return;
					}

					APlayerController* pc = NULL;
					for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
					{
						pc = Iterator->Get();

						APlayerState* thisPlayerState = pc->PlayerState;
						AMyPlayerState* playerS = Cast<AMyPlayerState>(thisPlayerState);

						if (playerS->playerKeyId == userKeyId)
						{

							// update player inventory
							if (itemName == "Cube") {
								UE_LOG(LogTemp, Log, TEXT("Purchase was a cube"));
								playerS->InventoryCubes = playerS->InventoryCubes + 1;


							}
							// update player currency balance
							playerRecord->currencyCurrent = playerRecord->currencyCurrent - purchaseAmount;
							playerS->Currency = playerS->Currency - purchaseAmount;
						}
					}

				}


			}
		}
	}
}

int32 UMyGameInstance::getSpawnRewardValue() {
	return SpawnRewardValue;
}

bool UMyGameInstance::Reward(FString playerKeyId, FString itemName, FString description, int32 amount)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] Purchase"));

	FString nonceString = "10951350917635";
	FString encryption = "off";  // Allowing unencrypted on sandbox for now.  

	TSharedPtr<FJsonObject> PlayerJsonObj = MakeShareable(new FJsonObject);
	PlayerJsonObj->SetStringField("nonce", "nonceString");
	PlayerJsonObj->SetStringField("encryption", encryption);
	PlayerJsonObj->SetStringField("itemName", itemName);
	PlayerJsonObj->SetStringField("description", description);
	PlayerJsonObj->SetNumberField("amount", amount);

	FString JsonOutputString;
	TSharedRef< TJsonWriter<> > Writer = TJsonWriterFactory<>::Create(&JsonOutputString);
	FJsonSerializer::Serialize(PlayerJsonObj.ToSharedRef(), Writer);

	FString APIURI = "/api/v1/server/player/" + playerKeyId + "/reward";

	bool requestSuccess = PerformJsonHttpRequest(&UMyGameInstance::RewardRequestComplete, APIURI, JsonOutputString);

	return requestSuccess;
}

void UMyGameInstance::RewardRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());
		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization True"));
				bool Success = JsonParsed->GetBoolField("success");
				if (Success) {
					UE_LOG(LogTemp, Log, TEXT("Success True"));
					FString userKeyId = JsonParsed->GetStringField("userKeyId");
					FString itemName = JsonParsed->GetStringField("itemName");
					int32 purchaseAmount = JsonParsed->GetNumberField("amount");

					FMyActivePlayer* playerRecord = getPlayerByPlayerKey(userKeyId);
					if (playerRecord == nullptr)
					{
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] TransferPlayer - Player NOT found by Key"));
						return;
					}

					APlayerController* pc = NULL;
					for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
					{
						pc = Iterator->Get();

						APlayerState* thisPlayerState = pc->PlayerState;
						AMyPlayerState* playerS = Cast<AMyPlayerState>(thisPlayerState);

						if (playerS->playerKeyId == userKeyId)
						{
							UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] TransferPlayer - Player Key ID match"));
							playerRecord->currencyCurrent = playerRecord->currencyCurrent + purchaseAmount;
							playerS->Currency = playerS->Currency + purchaseAmount;
						}
					}
				}
			}
		}
	}
}


void UMyGameInstance::RequestBeginPlay()
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] RequestBeginPlay"));
	// travel to the third person map
	FString UrlString = TEXT("/Game/Maps/ThirdPersonExampleMap?listen");
	GetWorld()->GetAuthGameMode()->bUseSeamlessTravel = true;
	GetWorld()->ServerTravel(UrlString);
	bRequestBeginPlayStarted = true;


}

void UMyGameInstance::LoadServerStateFromFile()
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] LoadServerStateFromFile"));
	/*
	bool FileIOSuccess;
	//bool allComponentsLoaded;
	//bool destroyActorsBeforeLoad = false;
	//bool dontLoadPlayerPawns = false;
	TArray<FString> StreamingLevelsStates;
	FString FileName = "serversavedata.dat";


	URamaSaveLibrary::RamaSave_LoadStreamingStateFromFile(GetWorld(), FileIOSuccess, FileName, StreamingLevelsStates);
	//URamaSaveLibrary::RamaSave_LoadFromFile(GetWorld(), FileIOSuccess, allComponentsLoaded, FileName, destroyActorsBeforeLoad, dontLoadPlayerPawns);
	if (FileIOSuccess) {
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] RequestBeginPlay File IO Success."));
	}
	else {
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] RequestBeginPlay File IO FAIL."));
	}
	*/


}

bool UMyGameInstance::OutgoingChat(int32 playerID, FText message)
{

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] OutgoingChat"));
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] DEBUG TEST"));

	// check to see if this player is in the active list already
	bool playerIDFound = false;
	int32 ActivePlayerIndex = 0;
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] check to see if this player is in the active list already"));

	// Get our player record
	FMyActivePlayer* CurrentActivePlayer = getPlayerByPlayerId(playerID);

	if (CurrentActivePlayer == nullptr) {
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] OutgoingChat - existing playerID found"));

	FString playerKeyId = CurrentActivePlayer->playerKeyId;

	UE_LOG(LogTemp, Log, TEXT("playerKeyId: %s"), *playerKeyId);
	UE_LOG(LogTemp, Log, TEXT("Object is: %s"), *GetName());
	UE_LOG(LogTemp, Log, TEXT("message is: %s"), *message.ToString());

	FString nonceString = "10951350917635";
	FString encryption = "off";  // Allowing unencrypted on sandbox for now.  

	FString OutputString;

	// Build Params as text string
	OutputString = "nonce=" + nonceString + "&encryption=" + encryption + "&message=" + message.ToString();
	// urlencode the params

	FString APIURI = "/api/v1/player/" + playerKeyId + "/chat";

	bool requestSuccess = PerformHttpRequest(&UMyGameInstance::OutgoingChatComplete, APIURI, OutputString);

	return requestSuccess;

}

void UMyGameInstance::OutgoingChatComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());

		FString JsonRaw = *HttpResponse->GetContentAsString();
		//  We don't care too much about the results from this call.  
	}
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [OutgoingChatComplete] Done!"));
}

bool UMyGameInstance::SubmitMatchMakerResults()
{

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] SubmitMatchMakerResults"));

	MatchInfo.encryption = "off";
	FString json_string;
	FJsonObjectConverter::UStructToJsonObjectString(MatchInfo.StaticStruct(), &MatchInfo, json_string, 0, 0);
	UE_LOG(LogTemp, Log, TEXT("json_string: %s"), *json_string);

	FString APIURI = "/api/v1/matchmaker/results";

	bool requestSuccess = PerformJsonHttpRequest(&UMyGameInstance::SubmitMatchMakerResultsComplete, APIURI, json_string);

	return requestSuccess;

}


void UMyGameInstance::SubmitMatchMakerResultsComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());

		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			//  We don't care too much about the results from this call.  
			UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization True"));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization False"));
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [SubmitMatchResults] Done!"));
}


bool UMyGameInstance::SubmitMatchResults()
{

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] SubmitMatchResults"));
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] DEBUG TEST"));

	FString player_dict_list = "%5B";  //TODO build this string urlencoded from json properly
	bool FoundKills = false;
	// get the data ready

	for (int32 b = 0; b < PlayerRecord.ActivePlayers.Num(); b++)
	{
		//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [DeAuthorizePlayer] playerID: %s"), ActivePlayers[b].playerID);
		if (PlayerRecord.ActivePlayers[b].roundKills > 0) {
			FoundKills = true;
		}
		player_dict_list = player_dict_list + "%7B%22deaths%22%3A" + FString::FromInt(PlayerRecord.ActivePlayers[b].roundDeaths) + "%2C";  // deaths

																																		   /*
																																		   player_dict_list = player_dict_list + "%22killed%22%3A+%5B"; // killed -list, go through em.
																																		   for (int32 pkilledi = 0; pkilledi < PlayerRecord.ActivePlayers[b].killed.Num(); pkilledi++)
																																		   {
																																		   player_dict_list = player_dict_list + "%22" + PlayerRecord.ActivePlayers[b].killed[pkilledi] + "%22";
																																		   if (pkilledi < PlayerRecord.ActivePlayers[b].killed.Num() - 1) {
																																		   // If it's not the last one add a comma.  I know this is dumb and should just be json
																																		   player_dict_list = player_dict_list + "%2C+";
																																		   }
																																		   }
																																		   player_dict_list = player_dict_list + "%5D%2C";
																																		   */
		player_dict_list = player_dict_list + "%22playerKeyId%22%3A%22" + PlayerRecord.ActivePlayers[b].playerKeyId + "%22%2C";
		player_dict_list = player_dict_list + "%22kills%22%3A+" + FString::FromInt(PlayerRecord.ActivePlayers[b].roundKills) + "%2C";
		player_dict_list = player_dict_list + "%22experience%22%3A+" + FString::FromInt(PlayerRecord.ActivePlayers[b].roundKills) + "%2C";
		player_dict_list = player_dict_list + "%22weapon%22%3A%22Bomb%22";
		player_dict_list = player_dict_list + "%7D";
		if (b < PlayerRecord.ActivePlayers.Num() - 1) {
			// If it's not the last one add a comma.  I know this is dumb and should just be json
			player_dict_list = player_dict_list + "%2C+";
		}
	}
	player_dict_list = player_dict_list + "%5D";

	// DOing this in pure json for comparison
	FString json_string;

	FJsonObjectConverter::UStructToJsonObjectString(FMyActivePlayers::StaticStruct(), &PlayerRecord, json_string, 0, 0);

	UE_LOG(LogTemp, Log, TEXT("player_dict_list: %s"), *player_dict_list);
	UE_LOG(LogTemp, Log, TEXT("json_string: %s"), *json_string);

	if (FoundKills == true) {
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] SubmitMatchResults - found kills"));

		FHttpModule* Http = &FHttpModule::Get();
		if (!Http) { return false; }
		if (!Http->IsHttpEnabled()) { return false; }

		UE_LOG(LogTemp, Log, TEXT("Object is: %s"), *GetName());

		FString nonceString = "10951350917635";
		FString encryption = "off";  // Allowing unencrypted on sandbox for now.  


		FString OutputString;

		// Build Params as text string
		OutputString = "nonce=" + nonceString + "&encryption=" + encryption + "&map_title=Demo&player_dict_list=" + player_dict_list;

		FString APIURI = "/api/v1/match/results";;

		bool requestSuccess = PerformHttpRequest(&UMyGameInstance::SubmitMatchResultsComplete, APIURI, OutputString);

		return requestSuccess;

	}
	else {
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] SubmitMatchResults - No Kills - Ignoring"));
	}
	return true;

}

void UMyGameInstance::SubmitMatchResultsComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded)
{
	if (!HttpResponse.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("Test failed. NULL response"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Completed test [%s] Url=[%s] Response=[%d] [%s]"),
			*HttpRequest->GetVerb(),
			*HttpRequest->GetURL(),
			HttpResponse->GetResponseCode(),
			*HttpResponse->GetContentAsString());

		FString JsonRaw = *HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonParsed;
		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonRaw);
		if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
		{
			bool Authorization = JsonParsed->GetBoolField("authorization");
			//  We don't care too much about the results from this call.  
			UE_LOG(LogTemp, Log, TEXT("Authorization"));
			if (Authorization)
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization True"));
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("Authorization False"));
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [SubmitMatchResults] Done!"));
}


/**
* Delegate fired when a session create request has completed
*
* @param SessionName the name of the session this callback is for
* @param bWasSuccessful true if the async action completed without error, false if there was an error
*/
void UMyGameInstance::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	//UE_LOG(LogOnlineGame, Verbose, TEXT("OnCreateSessionComplete %s bSuccess: %d"), *SessionName.ToString(), bWasSuccessful);
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] UMyGameInstance::OnCreateSessionComplete"));
}

/** Initiates matchmaker */
bool UMyGameInstance::StartMatchmaking(ULocalPlayer* PlayerOwner, FString MatchType)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::StartMatchmaking"));
	bool bResult = false;

	/*
	// THIS stuff came from shooter game
	// It requires a function inside gameMode for the cast to succeed:  GetGameSessionClass
	*/
	check(PlayerOwner != nullptr);
	if (PlayerOwner)
	{
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE Player Owner found"));
		AMyGameSession* const GameSession = GetGameSession();
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE got game session"));
		if (GameSession)
		{
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE Game session found"));
			GameSession->OnFindSessionsComplete().RemoveAll(this);
			OnSearchSessionsCompleteDelegateHandle = GameSession->OnFindSessionsComplete().AddUObject(this, &UMyGameInstance::OnSearchSessionsComplete);

			//UE_LOG(LogTemp, Verbose, TEXT("FindSessions PlayerOwner->GetPreferredUniqueNetId(): %d"), PlayerOwner->GetPreferredUniqueNetId());

			GameSession->StartMatchmaking(PlayerOwner->GetPreferredUniqueNetId(), GameSessionName, MatchType);
			//GameSession->FindSessions(PlayerOwner->GetPreferredUniqueNetId(), GameSessionName, bFindLAN, true);

			bResult = true;
		}
	}
	return bResult;
}


/** Initiates matchmaker */
bool UMyGameInstance::CancelMatchmaking(ULocalPlayer* PlayerOwner)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::CancelMatchmaking"));
	bool bResult = false;

	/*
	// THIS stuff came from shooter game
	// It requires a function inside gameMode for the cast to succeed:  GetGameSessionClass
	*/
	check(PlayerOwner != nullptr);
	if (PlayerOwner)
	{
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE Player Owner found"));
		AMyGameSession* const GameSession = GetGameSession();
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE got game session"));
		if (GameSession)
		{
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE Game session found"));
			GameSession->OnFindSessionsComplete().RemoveAll(this);

			//UE_LOG(LogTemp, Verbose, TEXT("FindSessions PlayerOwner->GetPreferredUniqueNetId(): %d"), PlayerOwner->GetPreferredUniqueNetId());

			GameSession->CancelMatchmaking(PlayerOwner->GetPreferredUniqueNetId(), GameSessionName);
			//GameSession->FindSessions(PlayerOwner->GetPreferredUniqueNetId(), GameSessionName, bFindLAN, true);

			bResult = true;
		}
	}
	return bResult;
}

/** Initiates the session searching */
bool UMyGameInstance::FindSessions(ULocalPlayer* PlayerOwner, bool bFindLAN)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::FindSessions"));
	bool bResult = false;

	/*
	// THIS stuff came from shooter game
	// It requires a function inside gameMode for the cast to succeed:  GetGameSessionClass
	*/
	check(PlayerOwner != nullptr);
	if (PlayerOwner)
	{
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE Player Owner found"));
		AMyGameSession* const GameSession = GetGameSession();
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE got game session"));
		if (GameSession)
		{
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE Game session found"));
			GameSession->OnFindSessionsComplete().RemoveAll(this);
			OnSearchSessionsCompleteDelegateHandle = GameSession->OnFindSessionsComplete().AddUObject(this, &UMyGameInstance::OnSearchSessionsComplete);

			//UE_LOG(LogTemp, Verbose, TEXT("FindSessions PlayerOwner->GetPreferredUniqueNetId(): %d"), PlayerOwner->GetPreferredUniqueNetId());

			GameSession->FindSessions(PlayerOwner->GetPreferredUniqueNetId(), GameSessionName, bFindLAN, true);

			bResult = true;
		}
	}
	return bResult;
}

/** Initiates the session searching on a server*/
bool UMyGameInstance::FindSessions(bool bFindLAN)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::FindSessions"));
	bool bResult = false;

	/*
	// THIS stuff came from shooter game
	// It requires a function inside gameMode for the cast to succeed:  GetGameSessionClass
	*/

	//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE Player Owner found"));
	AMyGameSession* const GameSession = GetGameSession();
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE got game session"));
	if (GameSession)
	{
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE Game session found"));
		GameSession->OnFindSessionsComplete().RemoveAll(this);
		OnSearchSessionsCompleteDelegateHandle = GameSession->OnFindSessionsComplete().AddUObject(this, &UMyGameInstance::OnSearchSessionsComplete);

		//UE_LOG(LogTemp, Verbose, TEXT("FindSessions PlayerOwner->GetPreferredUniqueNetId(): %d"), PlayerOwner->GetPreferredUniqueNetId());

		GameSession->FindSessions(GameSessionName, bFindLAN, true);

		bResult = true;
	}

	return bResult;
}

/** Callback which is intended to be called upon finding sessions */
void UMyGameInstance::OnSearchSessionsComplete(bool bWasSuccessful)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::OnSearchSessionsComplete"));
	AMyGameSession* const Session = GetGameSession();
	if (Session)
	{
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::OnSearchSessionsComplete: Session found"));
		Session->OnFindSessionsComplete().Remove(OnSearchSessionsCompleteDelegateHandle);

		//Session->GetSearchResults();

		const TArray<FOnlineSessionSearchResult> & SearchResults = Session->GetSearchResults();

		for (int32 IdxResult = 0; IdxResult < SearchResults.Num(); ++IdxResult)
		{
			//TSharedPtr<FServerEntry> NewServerEntry = MakeShareable(new FServerEntry());

			const FOnlineSessionSearchResult& Result = SearchResults[IdxResult];

			// setup a ustruct for bp
			// add the results to the TArray 
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] UMyGameInstance::OnSearchSessionsComplete: Adding to MySessionSearchResults"));
			FMySessionSearchResult searchresult;
			searchresult.OwningUserName = Result.Session.OwningUserName;
			searchresult.SearchIdx = IdxResult;
			FName key = "serverTitle";
			FString serverTitle;
			bool settingsSuccess = Result.Session.SessionSettings.Get(key, serverTitle);
			searchresult.ServerTitle = serverTitle;

			key = "serverKey";
			FString serverKey;
			Result.Session.SessionSettings.Get(key, serverKey);

			searchresult.ServerKey = serverKey;

			MySessionSearchResults.Add(searchresult);
			if (MySessionSearchResults.Num() > 0)
			{
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] UMyGameInstance::OnSearchSessionsComplete: MySessionSearchResults.Num() > 0"));  // just checking
			}
		}
	}
}


void UMyGameInstance::BeginWelcomeScreenState()
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::BeginWelcomeScreenState"));
	//this must come before split screen player removal so that the OSS sets all players to not using online features.
	SetIsOnline(false);

	// Remove any possible splitscren players
	RemoveSplitScreenPlayers();

	//LoadFrontEndMap(WelcomeScreenMap);

	ULocalPlayer* const LocalPlayer = GetFirstGamePlayer();
	//LocalPlayer->SetCachedUniqueNetId(nullptr);
	//check(!WelcomeMenuUI.IsValid());
	//WelcomeMenuUI = MakeShareable(new FShooterWelcomeMenu);
	//WelcomeMenuUI->Construct(this);
	//WelcomeMenuUI->AddToGameViewport();



	// Disallow splitscreen (we will allow while in the playing state)
	GetGameViewportClient()->SetDisableSplitscreenOverride(true);
}

void UMyGameInstance::SetIsOnline(bool bInIsOnline)
{
	bIsOnline = bInIsOnline;
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();

	if (OnlineSub)
	{
		for (int32 i = 0; i < LocalPlayers.Num(); ++i)
		{
			ULocalPlayer* LocalPlayer = LocalPlayers[i];

			TSharedPtr<const FUniqueNetId> PlayerId = LocalPlayer->GetPreferredUniqueNetId();
			if (PlayerId.IsValid())
			{
				OnlineSub->SetUsingMultiplayerFeatures(*PlayerId, bIsOnline);
			}
		}
	}
}

void UMyGameInstance::RemoveSplitScreenPlayers()
{
	// if we had been split screen, toss the extra players now
	// remove every player, back to front, except the first one
	while (LocalPlayers.Num() > 1)
	{
		ULocalPlayer* const PlayerToRemove = LocalPlayers.Last();
		RemoveExistingLocalPlayer(PlayerToRemove);
	}
}

void UMyGameInstance::RemoveExistingLocalPlayer(ULocalPlayer* ExistingPlayer)
{
	check(ExistingPlayer);
	if (ExistingPlayer->PlayerController != NULL)
		//{
		//	// Kill the player
		//	AShooterCharacter* MyPawn = Cast<AShooterCharacter>(ExistingPlayer->PlayerController->GetPawn());
		//	if (MyPawn)
		//	{
		//		MyPawn->KilledBy(NULL);
		//	}
		//}

		// Remove local split-screen players from the list
		RemoveLocalPlayer(ExistingPlayer);
}



bool UMyGameInstance::JoinSession(ULocalPlayer* LocalPlayer, int32 SessionIndexInSearchResults)
{
	// needs to tear anything down based on current state?

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::JoinSession 1"));


	AMyGameSession* const GameSession = GetGameSession();
	if (GameSession)
	{
		AddNetworkFailureHandlers();

		OnJoinSessionCompleteDelegateHandle = GameSession->OnJoinSessionComplete().AddUObject(this, &UMyGameInstance::OnJoinSessionComplete); //.AddUObject(this, &UMyGameInstance::OnJoinSessionComplete);
		if (GameSession->JoinSession(LocalPlayer->GetPreferredUniqueNetId(), GameSessionName, SessionIndexInSearchResults))
		{
			// If any error occured in the above, pending state would be set
			//if ((PendingState == CurrentState) || (PendingState == MyGameInstanceState::None))
			//{
			// Go ahead and go into loading state now
			// If we fail, the delegate will handle showing the proper messaging and move to the correct state
			//ShowLoadingScreen();
			//	GotoState(MyGameInstanceState::Playing);
			//	return true;
			//}
		}
	}


	return false;
}

bool UMyGameInstance::JoinSession(ULocalPlayer* LocalPlayer, const FOnlineSessionSearchResult& SearchResult)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::JoinSession 2"));
	// needs to tear anything down based on current state?
	AMyGameSession* const GameSession = GetGameSession();
	if (GameSession)
	{
		AddNetworkFailureHandlers();

		OnJoinSessionCompleteDelegateHandle = GameSession->OnJoinSessionComplete().AddUObject(this, &UMyGameInstance::OnJoinSessionComplete);
		if (GameSession->JoinSession(LocalPlayer->GetPreferredUniqueNetId(), GameSessionName, SearchResult))
		{
			// If any error occured in the above, pending state would be set
			if ((PendingState == CurrentState) || (PendingState == MyGameInstanceState::None))
			{
				// Go ahead and go into loading state now
				// If we fail, the delegate will handle showing the proper messaging and move to the correct state
				//ShowLoadingScreen();
				GotoState(MyGameInstanceState::Playing);
				return true;
			}
		}
	}

	return false;
}


/**
* Delegate fired when the joining process for an online session has completed
*
* @param SessionName the name of the session this callback is for
* @param bWasSuccessful true if the async action completed without error, false if there was an error
*/
void UMyGameInstance::OnJoinSessionComplete(EOnJoinSessionCompleteResult::Type Result)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::OnJoinSessionComplete"));
	// unhook the delegate
	AMyGameSession* const GameSession = GetGameSession();
	if (GameSession)
	{
		GameSession->OnJoinSessionComplete().Remove(OnJoinSessionCompleteDelegateHandle);
	}
	FinishJoinSession(Result);

}

void UMyGameInstance::FinishJoinSession(EOnJoinSessionCompleteResult::Type Result)
{
	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		FText ReturnReason;
		switch (Result)
		{
		case EOnJoinSessionCompleteResult::SessionIsFull:
			ReturnReason = NSLOCTEXT("NetworkErrors", "JoinSessionFailed", "Game is full.");
			break;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			ReturnReason = NSLOCTEXT("NetworkErrors", "JoinSessionFailed", "Game no longer exists.");
			break;
		default:
			ReturnReason = NSLOCTEXT("NetworkErrors", "JoinSessionFailed", "Join failed.");
			break;
		}

		FText OKButton = NSLOCTEXT("DialogButtons", "OKAY", "OK");
		RemoveNetworkFailureHandlers();
		//ShowMessageThenGoMain(ReturnReason, OKButton, FText::GetEmpty());
		return;
	}

	InternalTravelToSession(GameSessionName);
}

void UMyGameInstance::InternalTravelToSession(const FName& SessionName)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::InternalTravelToSession"));
	APlayerController * const PlayerController = GetFirstLocalPlayerController();

	if (PlayerController == nullptr)
	{
		FText ReturnReason = NSLOCTEXT("NetworkErrors", "InvalidPlayerController", "Invalid Player Controller");
		FText OKButton = NSLOCTEXT("DialogButtons", "OKAY", "OK");
		RemoveNetworkFailureHandlers();
		//ShowMessageThenGoMain(ReturnReason, OKButton, FText::GetEmpty());
		return;
	}

	// travel to session
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();

	if (OnlineSub == nullptr)
	{
		FText ReturnReason = NSLOCTEXT("NetworkErrors", "OSSMissing", "OSS missing");
		FText OKButton = NSLOCTEXT("DialogButtons", "OKAY", "OK");
		RemoveNetworkFailureHandlers();
		//ShowMessageThenGoMain(ReturnReason, OKButton, FText::GetEmpty());
		return;
	}

	FString URL;
	IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();

	if (!Sessions.IsValid() || !Sessions->GetResolvedConnectString(SessionName, URL))
	{
		FText FailReason = NSLOCTEXT("NetworkErrors", "TravelSessionFailed", "Travel to Session failed.");
		FText OKButton = NSLOCTEXT("DialogButtons", "OKAY", "OK");
		//ShowMessageThenGoMain(FailReason, OKButton, FText::GetEmpty());
		UE_LOG(LogOnlineGame, Warning, TEXT("Failed to travel to session upon joining it"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::InternalTravelToSession Traveling..."));
	PlayerController->ClientTravel(URL, TRAVEL_Absolute);
}

bool UMyGameInstance::TravelToSession(int32 ControllerId, FName SessionName)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::TravelToSession"));
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		FString URL;
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid() && Sessions->GetResolvedConnectString(SessionName, URL))
		{
			APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), ControllerId);
			if (PC)
			{
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::TravelToSession Travelling..."));
				PC->ClientTravel(URL, TRAVEL_Absolute);
				return true;
			}
		}
		else
		{
			UE_LOG(LogOnlineGame, Warning, TEXT("Failed to join session %s"), *SessionName.ToString());
		}
	}
#if !UE_BUILD_SHIPPING
	else
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), ControllerId);
		if (PC)
		{
			FString LocalURL(TEXT("127.0.0.1"));
			PC->ClientTravel(LocalURL, TRAVEL_Absolute);
			return true;
		}
	}
#endif //!UE_BUILD_SHIPPING

	return false;
}

void UMyGameInstance::RemoveNetworkFailureHandlers()
{
	// Remove the local session/travel failure bindings if they exist
	if (GEngine->OnTravelFailure().IsBoundToObject(this) == true)
	{
		GEngine->OnTravelFailure().Remove(TravelLocalSessionFailureDelegateHandle);
	}
}

void UMyGameInstance::AddNetworkFailureHandlers()
{
	// Add network/travel error handlers (if they are not already there)
	if (GEngine->OnTravelFailure().IsBoundToObject(this) == false)
	{
		TravelLocalSessionFailureDelegateHandle = GEngine->OnTravelFailure().AddUObject(this, &UMyGameInstance::TravelLocalSessionFailure);
	}
}

void UMyGameInstance::TravelLocalSessionFailure(UWorld *World, ETravelFailure::Type FailureType, const FString& ReasonString)
{
	/*
	AShooterPlayerController_Menu* const FirstPC = Cast<AShooterPlayerController_Menu>(UGameplayStatics::GetPlayerController(GetWorld(), 0));
	if (FirstPC != nullptr)
	{
	FText ReturnReason = NSLOCTEXT("NetworkErrors", "JoinSessionFailed", "Join Session failed.");
	if (ReasonString.IsEmpty() == false)
	{
	ReturnReason = FText::Format(NSLOCTEXT("NetworkErrors", "JoinSessionFailedReasonFmt", "Join Session failed. {0}"), FText::FromString(ReasonString));
	}

	FText OKButton = NSLOCTEXT("DialogButtons", "OKAY", "OK");
	ShowMessageThenGoMain(ReturnReason, OKButton, FText::GetEmpty());
	}
	*/
}


FName UMyGameInstance::GetInitialState()
{
	// On PC, go directly to the main menu
	return MyGameInstanceState::MainMenu;
}

void UMyGameInstance::GotoInitialState()
{
	GotoState(GetInitialState());
}

void UMyGameInstance::GotoState(FName NewState)
{
	UE_LOG(LogOnline, Log, TEXT("GotoState: NewState: %s"), *NewState.ToString());

	PendingState = NewState;
}

void UMyGameInstance::HandleSessionFailure(const FUniqueNetId& NetId, ESessionFailure::Type FailureType)
{
	UE_LOG(LogOnlineGame, Warning, TEXT("UMyGameInstance::HandleSessionFailure: %u"), (uint32)FailureType);

}

void UMyGameInstance::OnEndSessionComplete(FName SessionName, bool bWasSuccessful)
{
	UE_LOG(LogOnline, Log, TEXT("UMyGameInstance::OnEndSessionComplete: Session=%s bWasSuccessful=%s"), *SessionName.ToString(), bWasSuccessful ? TEXT("true") : TEXT("false"));

	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid())
		{
			Sessions->ClearOnStartSessionCompleteDelegate_Handle(OnStartSessionCompleteDelegateHandle);
			Sessions->ClearOnEndSessionCompleteDelegate_Handle(OnEndSessionCompleteDelegateHandle);
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(OnDestroySessionCompleteDelegateHandle);
		}
	}

	// continue
	CleanupSessionOnReturnToMenu();
}

void UMyGameInstance::CleanupSessionOnReturnToMenu()
{
	bool bPendingOnlineOp = false;

	// end online game and then destroy it
	IOnlineSubsystem * OnlineSub = IOnlineSubsystem::Get();
	IOnlineSessionPtr Sessions = (OnlineSub != NULL) ? OnlineSub->GetSessionInterface() : NULL;

	if (Sessions.IsValid())
	{
		EOnlineSessionState::Type SessionState = Sessions->GetSessionState(GameSessionName);
		UE_LOG(LogOnline, Log, TEXT("Session %s is '%s'"), *GameSessionName.ToString(), EOnlineSessionState::ToString(SessionState));

		if (EOnlineSessionState::InProgress == SessionState)
		{
			UE_LOG(LogOnline, Log, TEXT("Ending session %s on return to main menu"), *GameSessionName.ToString());
			OnEndSessionCompleteDelegateHandle = Sessions->AddOnEndSessionCompleteDelegate_Handle(OnEndSessionCompleteDelegate);
			Sessions->EndSession(GameSessionName);
			bPendingOnlineOp = true;
		}
		else if (EOnlineSessionState::Ending == SessionState)
		{
			UE_LOG(LogOnline, Log, TEXT("Waiting for session %s to end on return to main menu"), *GameSessionName.ToString());
			OnEndSessionCompleteDelegateHandle = Sessions->AddOnEndSessionCompleteDelegate_Handle(OnEndSessionCompleteDelegate);
			bPendingOnlineOp = true;
		}
		else if (EOnlineSessionState::Ended == SessionState || EOnlineSessionState::Pending == SessionState)
		{
			UE_LOG(LogOnline, Log, TEXT("Destroying session %s on return to main menu"), *GameSessionName.ToString());
			OnDestroySessionCompleteDelegateHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(OnEndSessionCompleteDelegate);
			Sessions->DestroySession(GameSessionName);
			bPendingOnlineOp = true;
		}
		else if (EOnlineSessionState::Starting == SessionState)
		{
			UE_LOG(LogOnline, Log, TEXT("Waiting for session %s to start, and then we will end it to return to main menu"), *GameSessionName.ToString());
			OnStartSessionCompleteDelegateHandle = Sessions->AddOnStartSessionCompleteDelegate_Handle(OnEndSessionCompleteDelegate);
			bPendingOnlineOp = true;
		}
	}

	if (!bPendingOnlineOp)
	{
		//GEngine->HandleDisconnect( GetWorld(), GetWorld()->GetNetDriver() );
	}
}

bool UMyGameInstance::RegisterNewSession(FString IncServerSessionHostAddress, FString IncServerSessionID)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::RegisterNewSession"));

	ServerSessionHostAddress = IncServerSessionHostAddress;
	ServerSessionID = IncServerSessionID;

	if (UEtopiaMode == "competitive") {
		// We're matchmaker, so get match info
		GetMatchInfo();
	}
	else {
		// UEtopia dedicated server.
		GetServerInfo();
		GetServerLinks();
	}



	return true;
}

bool UMyGameInstance::IsLocalPlayerOnline(ULocalPlayer* LocalPlayer)
{
	if (LocalPlayer == NULL)
	{
		return false;
	}
	const auto OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		const auto IdentityInterface = OnlineSub->GetIdentityInterface();
		if (IdentityInterface.IsValid())
		{
			auto UniqueId = LocalPlayer->GetCachedUniqueNetId();
			if (UniqueId.IsValid())
			{
				const auto LoginStatus = IdentityInterface->GetLoginStatus(*UniqueId);
				if (LoginStatus == ELoginStatus::LoggedIn)
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool UMyGameInstance::ValidatePlayerForOnlinePlay(ULocalPlayer* LocalPlayer)
{
	// This is all UI stuff.
	// Removing it for now

	return true;
}

void UMyGameInstance::HandleUserLoginChanged(int32 GameUserIndex, ELoginStatus::Type PreviousLoginStatus, ELoginStatus::Type LoginStatus, const FUniqueNetId& UserId)
{
	/*
	const bool bDowngraded = (LoginStatus == ELoginStatus::NotLoggedIn && !GetIsOnline()) || (LoginStatus != ELoginStatus::LoggedIn && GetIsOnline());

	UE_LOG(LogOnline, Log, TEXT("HandleUserLoginChanged: bDownGraded: %i"), (int)bDowngraded);

	TSharedPtr<GenericApplication> GenericApplication = FSlateApplication::Get().GetPlatformApplication();
	bIsLicensed = GenericApplication->ApplicationLicenseValid();

	// Find the local player associated with this unique net id
	ULocalPlayer * LocalPlayer = FindLocalPlayerFromUniqueNetId(UserId);

	// If this user is signed out, but was previously signed in, punt to welcome (or remove splitscreen if that makes sense)
	if (LocalPlayer != NULL)
	{
	if (bDowngraded)
	{
	UE_LOG(LogOnline, Log, TEXT("HandleUserLoginChanged: Player logged out: %s"), *UserId.ToString());

	//LabelPlayerAsQuitter(LocalPlayer);

	// Check to see if this was the master, or if this was a split-screen player on the client
	if (LocalPlayer == GetFirstGamePlayer() || GetIsOnline())
	{
	HandleSignInChangeMessaging();
	}
	else
	{
	// Remove local split-screen players from the list
	RemoveExistingLocalPlayer(LocalPlayer);
	}
	}
	}
	*/
}

void UMyGameInstance::HandleUserLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::HandleUserLoginComplete"));
	if (bWasSuccessful == false)
	{
		return;
	}
	const auto OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::HandleUserLoginComplete OnlineSub"));
		const auto FriendsInterface = OnlineSub->GetFriendsInterface();
		if (FriendsInterface.IsValid())
		{
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::HandleUserLoginComplete FriendsInterface.IsValid()"));
			TArray< TSharedRef<FOnlineFriend> > FriendList;
			APlayerController* thisPlayer = GetFirstLocalPlayerController();
			AMyPlayerController* thisMyPlayer = Cast<AMyPlayerController>(thisPlayer);
			//FOnReadFriendsListComplete Delegate = FOnReadFriendsListComplete::CreateUObject(this, thisMyPlayer::OnReadFriendsComplete);
			FriendsInterface->ReadFriendsList(LocalUserNum, "default", thisMyPlayer->OnReadFriendsListCompleteDelegate);

			TSharedPtr <const FUniqueNetId> pid = OnlineSub->GetIdentityInterface()->GetUniquePlayerId(0);
			FriendsInterface->QueryRecentPlayers(*pid, "default");
				//GetFriendsList(LocalUserNum, "default", FriendList);
		}
	}
}

void UMyGameInstance::HandleSignInChangeMessaging()
{
	// Master user signed out, go to initial state (if we aren't there already)
	if (CurrentState != GetInitialState())
	{
		GotoInitialState();
	}
}

void UMyGameInstance::BeginLogin(FString InType, FString InId, FString InToken)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] GAME INSTANCE UMyGameInstance::BeginLogin"));

	const auto OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		const auto IdentityInterface = OnlineSub->GetIdentityInterface();
		if (IdentityInterface.IsValid())
		{
			FOnlineAccountCredentials* Credentials = new FOnlineAccountCredentials(InType, InId, InToken);
			IdentityInterface->Login(0, *Credentials);
		}
	}

}



FMyActivePlayer* UMyGameInstance::getPlayerByPlayerId(int32 playerID)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] getPlayerByPlayerID"));
	for (int32 b = 0; b < PlayerRecord.ActivePlayers.Num(); b++)
	{
		if (PlayerRecord.ActivePlayers[b].playerID) {
			UE_LOG(LogTemp, Log, TEXT("Comparing playerID: %d PlayerRecord.ActivePlayers[b].playerID:%d"),
				playerID,
				PlayerRecord.ActivePlayers[b].playerID);

			if (PlayerRecord.ActivePlayers[b].playerID == playerID) {
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] getPlayerByPlayerID - FOUND MATCHING playerID"));
				FMyActivePlayer* playerPointer = &PlayerRecord.ActivePlayers[b];
				return playerPointer;
			}
		}
		else {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] DeActivatePlayer - PlayerRecord.ActivePlayers[b].playerID DOES NOT EXIST"));
		}

	}
	return nullptr;
}



FMyActivePlayer* UMyGameInstance::getPlayerByPlayerKey(FString playerKeyId)
{
	// TODO rename this, should be user not player
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] getPlayerByPlayerKey"));
	for (int32 b = 0; b < PlayerRecord.ActivePlayers.Num(); b++)
	{
		if (PlayerRecord.ActivePlayers[b].playerKeyId == playerKeyId) {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] getPlayerByPlayerKey - FOUND MATCHING playerID"));
			FMyActivePlayer* playerPointer = &PlayerRecord.ActivePlayers[b];
			return playerPointer;
		}
	}
	return nullptr;
}

int32 UMyGameInstance::getActivePlayerCount()
{
	int32 count = 0;
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] getActivePlayerCount"));
	for (int32 b = 0; b < PlayerRecord.ActivePlayers.Num(); b++)
	{
		if (PlayerRecord.ActivePlayers[b].authorized) {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] getActivePlayerCount - FOUND authorized"));
			count++;
		}
	}
	return count;
}

FMyServerLink* UMyGameInstance::getServerByKey(FString serverKey)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] getServerByKey"));
	for (int32 b = 0; b < ServerLinks.links.Num(); b++)
	{
		if (ServerLinks.links[b].targetServerKeyId == serverKey) {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] getServerByKey - FOUND MATCHING server"));
			FMyServerLink* serverPointer = &ServerLinks.links[b];
			return serverPointer;
		}
	}
	return nullptr;
}


bool UMyGameInstance::RecordKill(int32 killerPlayerID, int32 victimPlayerID)
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] "));
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] killerPlayerID: %i"), killerPlayerID);
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] victimPlayerID: %i "), victimPlayerID);
	if (UEtopiaMode == "competitive") {
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - Mode set to competitive"));
		// get attacker activeplayer
		bool attackerPlayerIDFound = false;
		int32 killerPlayerIndex = 0;
		// sum all the round kills while we're looping
		int32 roundKillsTotal = 0;

		// TODO refactor this to use our get player function instead

		for (int32 b = 0; b < MatchInfo.players.Num(); b++)
		{
			//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [DeAuthorizePlayer] playerID: %s"), ActivePlayers[b].playerID);
			if (MatchInfo.players[b].playerID == killerPlayerID) {
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - FOUND MATCHING killer playerID"));
				attackerPlayerIDFound = true;
				killerPlayerIndex = b;
			}
			roundKillsTotal = roundKillsTotal + MatchInfo.players[b].roundKills;
		}
		// we need one more since this kill has not been added to the list yet
		roundKillsTotal = roundKillsTotal + 1;

		if (killerPlayerID == victimPlayerID) {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] suicide"));
		}
		else {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] Not a suicide"));

			// get victim 
			bool victimPlayerIDFound = false;
			int32 victimPlayerIndex = 0;

			for (int32 b = 0; b < MatchInfo.players.Num(); b++)
			{
				//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [DeAuthorizePlayer] playerID: %s"), ActivePlayers[b].playerID);
				if (MatchInfo.players[b].playerID == victimPlayerID) {
					UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - FOUND MATCHING victim playerID"));
					victimPlayerIDFound = true;
					victimPlayerIndex = b;
					// set currentRoundAlive
					MatchInfo.players[b].currentRoundAlive = false;
				}
			}

			// check to see if this victim is already in the kill list

			bool victimIDFoundInKillList = false;

			for (int32 b = 0; b < MatchInfo.players[killerPlayerIndex].killed.Num(); b++)
			{
				if (MatchInfo.players[killerPlayerIndex].killed[b] == MatchInfo.players[victimPlayerIndex].userKeyId)
				{
					UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Victim already in kill list"));
					victimIDFoundInKillList = true;
				}
			}

			if (victimIDFoundInKillList == false) {
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Adding victim to kill list"));
				MatchInfo.players[killerPlayerIndex].killed.Add(MatchInfo.players[victimPlayerIndex].userKeyId);
			}

			// Increase the killer's kill count
			MatchInfo.players[killerPlayerIndex].roundKills = MatchInfo.players[killerPlayerIndex].roundKills + 1;
			// And increase the victim's deaths
			MatchInfo.players[victimPlayerIndex].roundDeaths = MatchInfo.players[victimPlayerIndex].roundDeaths + 1;

			// Give the killer some xp and score
			MatchInfo.players[killerPlayerIndex].score = MatchInfo.players[killerPlayerIndex].score + 100;
			MatchInfo.players[killerPlayerIndex].experience = MatchInfo.players[killerPlayerIndex].experience + 10;

			//calculate new ranks;
			CalculateNewRank(killerPlayerIndex, victimPlayerIndex, true);

			//Calculate winning team


		}

		// Check to see if the round is over
		// reset the level if one team is all dead
		// If you have a game that has more than two teams you should rewrite this for your end round logic
		bool anyTeamDead = false;
		int32 stillAliveTeamId = 0;
		for (int32 b = 0; b <= teamCount; b++) // b is team index
		{
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [CalculateNewRank] Checking team %d"), b);
			bool thisTeamAlive = false;

			for (int32 ip = 0; ip < MatchInfo.players.Num(); ip++) // ip is player index
			{
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [CalculateNewRank] Checking player %d"), ip);
				if (MatchInfo.players[ip].teamId == b) {
					UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Player is a member of this team"));
					// current round alive boolean check
					if (MatchInfo.players[ip].currentRoundAlive) {
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Player is alive"));
						thisTeamAlive = true;
						stillAliveTeamId = b;
					}
					else {
						UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Player is dead"));
					}
				}
			}

			if (thisTeamAlive == false) {
				anyTeamDead = true;
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - A team is dead"));
			}


		}


		if (anyTeamDead) {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Detected dead team"));
			// flag and count the team win
			// this is only for two teams - if you have more, change this.
			for (int32 b = 0; b < MatchInfo.players.Num(); b++)
			{
				if (MatchInfo.players[b].teamId == stillAliveTeamId) {
					MatchInfo.players[b].score = MatchInfo.players[b].score + 200;
					MatchInfo.players[b].experience = MatchInfo.players[b].experience + 75;

				}
			}


		}

		//Check to see if the game is over
		if (roundKillsTotal >= MinimumKillsBeforeResultsSubmit) {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Ending the Game"));

			int32 highestRoundKills = 0;
			int32 highestScoreTeamId = 0;

			// determine the highest roundKills
			for (int32 b = 0; b < MatchInfo.players.Num(); b++)
			{
				if (MatchInfo.players[b].roundKills > highestRoundKills) {
					highestScoreTeamId = MatchInfo.players[b].teamId;
					highestRoundKills = MatchInfo.players[b].roundKills;

				}
			}
			//set winner for the winning team
			for (int32 b = 0; b < MatchInfo.players.Num(); b++)
			{
				if (MatchInfo.players[b].teamId == highestScoreTeamId) {
					MatchInfo.players[b].win = true;
					MatchInfo.players[b].score = MatchInfo.players[b].score + 400;
					MatchInfo.players[b].experience = MatchInfo.players[b].experience + 200;
				}
			}


			bool gamesubmitted = SubmitMatchMakerResults();

			//Deauthorize everyone

			for (int32 b = 0; b < MatchInfo.players.Num(); b++)
			{
				DeActivatePlayer(MatchInfo.players[b].playerID);
			}



			// We might need some kind of delay in here because we're going to wipe this data.  plz don't crash

			// Reset the active players
			// ActivePlayers.Empty();
			// Kick everyone back to login
			// travel all clinets back to login
			APlayerController* pc = NULL;
			FString UrlString = TEXT("/Game/LoginLevel");

			for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
			{
				pc = Iterator->Get();
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [ActivateRequestComplete] - kicking back to login"));
				pc->ClientTravel(UrlString, ETravelType::TRAVEL_Absolute);
			}


		}
		else {
			if (anyTeamDead) {
				// restart the round
				// reset alive status
				for (int32 b = 0; b < MatchInfo.players.Num(); b++)
				{
					MatchInfo.players[b].currentRoundAlive = true;
				}

				FString UrlString = TEXT("/Game/Maps/ThirdPersonExampleMap?listen");
				GetWorld()->GetAuthGameMode()->bUseSeamlessTravel = true;
				GetWorld()->ServerTravel(UrlString);
			}
		}


	}
	else {
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AuthorizePlayer - Mode set to continuous"));
		// get attacker activeplayer
		bool attackerPlayerIDFound = false;
		int32 killerPlayerIndex = 0;
		// sum all the round kills while we're looping
		int32 roundKillsTotal = 0;

		// TODO refactor this to use our get player function instead

		for (int32 b = 0; b < PlayerRecord.ActivePlayers.Num(); b++)
		{
			//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [DeAuthorizePlayer] playerID: %s"), ActivePlayers[b].playerID);
			if (PlayerRecord.ActivePlayers[b].playerID == killerPlayerID) {
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - FOUND MATCHING killer playerID"));
				attackerPlayerIDFound = true;
				killerPlayerIndex = b;
			}
			roundKillsTotal = roundKillsTotal + PlayerRecord.ActivePlayers[b].roundKills;
		}
		// we need one more since this kill has not been added to the list yet
		roundKillsTotal = roundKillsTotal + 1;

		if (killerPlayerID == victimPlayerID) {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] suicide"));
		}
		else {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] Not a suicide"));

			// get victim activeplayer
			bool victimPlayerIDFound = false;
			int32 victimPlayerIndex = 0;

			for (int32 b = 0; b < PlayerRecord.ActivePlayers.Num(); b++)
			{
				//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [DeAuthorizePlayer] playerID: %s"), ActivePlayers[b].playerID);
				if (PlayerRecord.ActivePlayers[b].playerID == victimPlayerID) {
					UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - FOUND MATCHING victim playerID"));
					victimPlayerIDFound = true;
					victimPlayerIndex = b;
				}
			}

			// check to see if this victim is already in the kill list
			/*
			bool victimIDFoundInKillList = false;

			for (int32 b = 0; b < PlayerRecord.ActivePlayers[killerPlayerIndex].killed.Num(); b++)
			{
			if (PlayerRecord.ActivePlayers[killerPlayerIndex].killed[b] == PlayerRecord.ActivePlayers[victimPlayerIndex].playerKey)
			{
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Victim already in kill list"));
			victimIDFoundInKillList = true;
			}
			}

			if (victimIDFoundInKillList == false) {
			UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Adding victim to kill list"));
			PlayerRecord.ActivePlayers[killerPlayerIndex].killed.Add(PlayerRecord.ActivePlayers[victimPlayerIndex].playerKey);
			}
			*/

			// Increase the killer's kill count
			PlayerRecord.ActivePlayers[killerPlayerIndex].roundKills = PlayerRecord.ActivePlayers[killerPlayerIndex].roundKills + 1;
			// Increase the killer's balance
			//PlayerRecord.ActivePlayers[killerPlayerIndex].currencyCurrent = PlayerRecord.ActivePlayers[killerPlayerIndex].currencyCurrent + killRewardBTC;
			// And increase the victim's deaths
			PlayerRecord.ActivePlayers[victimPlayerIndex].roundDeaths = PlayerRecord.ActivePlayers[victimPlayerIndex].roundDeaths + 1;
			// Decrease the victim's balance
			//PlayerRecord.ActivePlayers[victimPlayerIndex].currencyCurrent = PlayerRecord.ActivePlayers[victimPlayerIndex].currencyCurrent - incrementCurrency;

			// TODO kick the victim if it falls below the minimum?

			APlayerController* pc = NULL;
			// Send out a chat message to all players
			FText chatSender = NSLOCTEXT("UETOPIA", "chatSender", "SYSTEM");
			FText chatMessageText = NSLOCTEXT("UETOPIA", "chatMessageText", " killed ");  //TODO figure out how to set up this string correctly.

			for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
			{
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Sending text chat"));
				pc = Iterator->Get();
				APlayerState* thisPlayerState = pc->PlayerState;
				AMyPlayerState* thisMyPlayerState = Cast<AMyPlayerState>(thisPlayerState);
				thisMyPlayerState->ReceiveChatMessage(chatSender, chatMessageText);

				/*
				// old way
				TSubclassOf<APlayerController> thisPlayerController = pc;
				AMyPlayerController* thisPlayerController = Cast<AMyPlayerController>(pc);
				if (thisPlayerController) {
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Cast Controller success"));

				APlayerState* thisPlayerState = thisPlayerController->PlayerState;
				AMyPlayerState* thisMyPlayerState = Cast<AMyPlayerState>(thisPlayerState);
				if (thisMyPlayerState) {
				UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Cast State success"));
				thisMyPlayerState->ReceiveChatMessage(chatSender, chatMessageText);
				}
				}
				*/
			}


		}

		// TODO deal with this
		/*
		//Check to see if the game is over
		if (roundKillsTotal >= MinimumKillsBeforeResultsSubmit) {
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Ending the Game"));
		bool gamesubmitted = SubmitMatchResults();

		//Deauthorize everyone
		for (int32 b = 0; b < ActivePlayers.Num(); b++)
		{
		DeAuthorizePlayer(ActivePlayers[b].playerID);
		}

		// We might need some kind of delay in here because we're going to wipe this data.  plz don't crash

		// Reset the active players
		ActivePlayers.Empty();
		// Kick everyone back to login
		FString UrlString = TEXT("/Game/MyLoginLevel?listen");
		GetWorld()->GetAuthGameMode()->bUseSeamlessTravel = true;
		GetWorld()->ServerTravel(UrlString);

		}
		else {
		// Check to see if the round is over
		if (roundKillsTotal >= MinimumPlayerDeathsBeforeRoundReset) {
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [RecordKill] - Ending the Round"));
		// travel to the third person map
		FString UrlString = TEXT("/Game/ThirdPersonCPP/Maps/ThirdPersonExampleMap?listen");
		GetWorld()->GetAuthGameMode()->bUseSeamlessTravel = true;
		GetWorld()->ServerTravel(UrlString);
		}
		}
		*/



	}


	return true;
}


FMyServerLink UMyGameInstance::GetServerLinkByTargetServerKeyId(FString incomingTargetServerKeyId) {
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] GetServerLinkByTargetServerKeyId."));
	for (int32 b = 0; b < ServerLinks.links.Num(); b++)
	{
		if (ServerLinks.links[b].targetServerKeyId == incomingTargetServerKeyId) {
			return ServerLinks.links[b];
		}
	}
	// just make an empty one
	FMyServerLink emptylink;
	return emptylink;
}

void UMyGameInstance::AttemptSpawnReward()
{
	if (IsRunningDedicatedServer())
	{
		//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AttemptSpawnReward - Dedicated server found."));
		if (bSpawnRewardsEnabled) {
			//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AttemptSpawnReward - Spawn rewards enabled."));

			if (bRequestBeginPlayStarted) {
				//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AttemptSpawnReward - bRequestBeginPlayStarted."));
				if (SpawnRewardServerMinimumBalanceRequired < serverCurrency) {
					//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AttemptSpawnReward - Server has enough currency."));
					float randomchance = FMath::RandRange(0.0f, 1.0f);
					if (randomchance < SpawnRewardChance) {
						//UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AttemptSpawnReward - Random success."));
						UWorld* const World = GetWorld(); // get a reference to the world
						if (World) {
							// if world exists, spawn the actor
							float randomXlocation = FMath::RandRange(SpawnRewardLocationXMin, SpawnRewardLocationXMax);
							float randomYlocation = FMath::RandRange(SpawnRewardLocationYMin, SpawnRewardLocationYMax);
							float randomZlocation = FMath::RandRange(SpawnRewardLocationZMin, SpawnRewardLocationZMax);

							FVector spawnlocation = FVector(randomXlocation, randomYlocation, randomZlocation);
							FTransform SpawnTransform = FTransform(spawnlocation);
							//AMyRewardItemActor* const RewardActor = World->SpawnActor<AMyRewardItemActor>(AMyRewardItemActor::StaticClass(), SpawnTransform);

							// reduce the server's balance by the reward amount
							serverCurrency = serverCurrency - SpawnRewardValue;

						}

					}
				}
			}
		}
	}
}

void UMyGameInstance::AttemptStartMatch()
{
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AttemptStartMatch."));
	if (IsRunningDedicatedServer())
	{
		UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] AttemptStartMatch - Dedicated server found."));
		// travel to the third person map
		FString UrlString = TEXT("/Game/Maps/ThirdPersonExampleMap?listen");
		GetWorld()->GetAuthGameMode()->bUseSeamlessTravel = true;
		GetWorld()->ServerTravel(UrlString);
	}
}

void UMyGameInstance::CalculateNewRank(int32 WinnerPlayerIndex, int32 LoserPlayerIndex, bool penalizeLoser) {
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [CalculateNewRank] "));

	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [CalculateNewRank] WinnerRank %d"), MatchInfo.players[WinnerPlayerIndex].rank);
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [CalculateNewRank] LoserRank %d"), MatchInfo.players[LoserPlayerIndex].rank);
	// redoing this according to:
	// https://metinmediamath.wordpress.com/2013/11/27/how-to-calculate-the-elo-rating-including-example/
	float winnerTransformedRankExp = MatchInfo.players[WinnerPlayerIndex].rank / 400.0f;
	float winnerTransformedRank = pow(10.0f, winnerTransformedRankExp);
	float loserTransformedRankExp = MatchInfo.players[LoserPlayerIndex].rank / 400.0f;
	float loserTransformedRank = pow(10.0f, loserTransformedRankExp);

	float winnerExpected = winnerTransformedRank / (winnerTransformedRank + loserTransformedRank);
	float loserExpected = loserTransformedRank / (winnerTransformedRank + loserTransformedRank);

	float k = 32.0f;

	int32 newWinnerRank = round(MatchInfo.players[WinnerPlayerIndex].rank + k * (1.0f - winnerExpected));
	int32 newLoserRank = round(MatchInfo.players[LoserPlayerIndex].rank + k * (0.0f - loserExpected));



	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [CalculateNewRank] newWinnerRank %d"), newWinnerRank);
	UE_LOG(LogTemp, Log, TEXT("[UETOPIA] [UMyGameInstance] [CalculateNewRank] NewLoserRank %d"), newLoserRank);

	MatchInfo.players[WinnerPlayerIndex].rank = newWinnerRank;
	MatchInfo.players[LoserPlayerIndex].rank = newLoserRank;
}