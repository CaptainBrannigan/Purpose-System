// Copyright Jordan Cain. All Rights Reserved.


#include "Purpose/Director_Level.h"
#include "Purpose/Assets/EventAsset.h"
#include "EngineUtils.h"
#include "AIActivity.h"
#include "Purpose/DataChunks/ActorRole.h"
#include "Purpose/DataChunks/ActorLocation.h"
#include "Purpose/DataChunks/ActorAction.h"
#include "GameFramework/PlayerController.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "Perception/AISense_Sight.h"
#include "Player/LyraPlayerBotController.h"
#include "GameModes/LyraGameMode.h"
#include "NavigationSystem.h"
#include "Purpose/PurposeAbilityComponent.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/Attributes/LyraCombatSet.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "GameModes/LyraExperienceManagerComponent.h"
#include "Development/LyraDeveloperSettings.h"

// Sets default values
ADirector_Level::ADirector_Level()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	//if (const UGlobalManagementSettings* ManagementSettings = GetDefault<UGlobalManagementSettings>())
	//{
	//	playerSightEQSCache = ManagementSettings->PlayerSightQuery.LoadSynchronous();
	//}

	if (GetWorld())
	{
		timeSinceLastEQS = GetWorld()->GetTime();
	}
}

// Called when the game starts or when spawned
void ADirector_Level::BeginPlay()
{
	Super::BeginPlay();
	Global::Log(CALLTRACEESSENTIAL, MANAGEMENT, *this, "BeginPlay", TEXT(""));

	/// Refacotor: Purpose Director: Init, which creates the background threads, needs to be called after event assets have loaded and characters have all been spawned
	//Init();

	UAssetManager* assetManager = GEngine->AssetManager;
	if (!assetManager)
	{
		Global::LogError(MANAGEMENT, * this, "BeginPlay", TEXT("Asset manager invalid!"));
		return;
	}

	timeSinceLastEQS = GetWorld()->GetTime();/// Need to initialize the time
	PrimaryActorTick.TickInterval = timeBetweenEQSQueries;

	TSharedPtr<FStreamableHandle> callback = assetManager->LoadPrimaryAssetsWithType(UEventAsset::PrimaryAssetType(), TArray<FName>(), FStreamableDelegate::CreateUObject(this, &ADirector_Level::EventAssetsLoaded));

	if (!callback)
	{
		Global::LogError(PURPOSE, *this, "BeginPlay", TEXT("Callback to load EventAssets invalid!."));
		return;
	}

}

// Called every frame
void ADirector_Level::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	PerformVisualEQS();
}

void ADirector_Level::BeginDestroy()
{
	Shutdown();///Ensure that rooted context datas are cleaned up and background threads are shutdown
	Super::BeginDestroy();
}

void ADirector_Level::DirectorLatentSafeShutdown(TObjectPtr<APlayerController> inPlayer)
{
	UKismetSystemLibrary::QuitGame(GetWorld(), inPlayer, EQuitPreference::Background, false);
}

void ADirector_Level::SafelyShutdownGame(TObjectPtr<APlayerController> inPlayer)
{
	Shutdown();///Ensure that rooted context datas are cleaned up and background threads are shutdown
	/// We will use a timer to allow the Async Tasks to run through remove their ContextDatas from root
	FTimerDelegate TimerDel;
	FTimerHandle TimerHandle;
	TimerDel.BindUObject(this, &ADirector_Level::DirectorLatentSafeShutdown, inPlayer);
	GetWorldTimerManager().SetTimer(TimerHandle, TimerDel, 3.f, false);
}

#pragma region Event System

///Debug: Lyra Occurrence Purpose; Why are occurrences being constantly called? Is it because of Lyra GA's?
	/// I believe it is due to Gameplay ability for auto reload
	/// Can also be due to a Behavior Tree having Wait task and our binding to ActionPerformed 

void ADirector_Level::EventAssetsLoaded()
{
	UAssetManager* assetManager = GEngine->AssetManager;
	if (!assetManager)
	{
		Global::LogError(MANAGEMENT, *this, "BeginPlay", TEXT("Asset manager invalid!"));
		return;
	}

	//TSharedPtr<FStreamableHandle> callback = assetManager->LoadPrimaryAssetsWithType(UEventAsset::PrimaryAssetType());
	TArray<FAssetData> assets;
	if(!assetManager->GetPrimaryAssetDataList(UEventAsset::PrimaryAssetType(), assets))
	{
		Global::LogError(PURPOSE, GetName(), "EventAssetsLoaded", TEXT("Callback to load EventAssets invalid!"));
		return;
	}

	for (const FAssetData& asset : assets)
	{
		if (asset.IsValid() && asset.GetAsset()->IsA<UEventAsset>())
		{
			eventCacheForPurposeSystem.Add(Cast<UEventAsset>(asset.GetAsset())->eventPurpose);
		}
	}


	/// Firstly, let's go ahead and establish the purpose threads
	Init();

	/// If the threads are not valid and active, then we don't want to continue and game needs to restart
	if (!eventThread || !actorThread || !currentEventThread || !currentActorThread)
	{
		Global::LogError(PURPOSE, GetName(), "EventAssetsLoaded", TEXT("Purpose threads invalid!"));
		return;
	}

	/// With the background threads for the Purpose System running, we need AI and their AManager's
	FTimerDelegate latentInitiateManagement;
	FTimerHandle managementHandle;
	latentInitiateManagement.BindUObject(this, &ADirector_Level::InitiateManagement);
	GetWorldTimerManager().SetTimer(managementHandle, latentInitiateManagement, 1.f, false);

	/// With managers spawned in, we'll want to initiate the Level loop by seeking Activity Events and providing the AManager AI groups with Goals and Objectives to perform
	FTimerDelegate TimerDel;
	FTimerHandle TimerHandle;
	TimerDel.BindUObject(this, &ADirector_Level::SeekActivitiesInLevel);
	GetWorldTimerManager().SetTimer(TimerHandle, TimerDel, 3.f, false);
}

void ADirector_Level::SeekActivitiesInLevel()
{
	Global::Log(CALLTRACEESSENTIAL, PURPOSE, *this, "SeekActivitiesInLevel", TEXT(""));

	for (TActorIterator<AAIActivity> ActorItr(GetWorld(), AAIActivity::StaticClass()); ActorItr; ++ActorItr)/// Find all AI Activity objects in level
	{
		////Global::Log( FULLTRACE, ManagementLog, *this, "SeekActivitiesInLevel", TEXT("Activity found."));
		if (IsValid(*ActorItr))
		{
			Global::Log( DATADEBUG, EVENT, *this, "SeekActivitiesInLevel", TEXT("Activity: %s"), *ActorItr->GetName());

			eventCacheForPurposeSystem.Add(ActorItr->eventForActivity);/// We need to store the activity for future potential occurrences
		}

		FSubjectMap subjects;
		subjects.subjects.Add(ESubject::Instigator, *ActorItr);
		TArray<FDataMapEntry> contextData;
		contextData = ActorItr->data;

		/// For every Activity create an Occurrence with for AI to evaluate
		PurposeSystem::Occurrence(subjects, contextData, GetPurposeEvaluationThreads());
	}
}

void ADirector_Level::ClearQueues()
{
	
}

#pragma endregion

#pragma region Management

void ADirector_Level::InitiateManagement()
{
	LocateSpawnsInLevel();

	for (const FAISpawnEntry& spawnData : SpawnCache)
	{
		//SpawnManager(entry);

		/// Refactor: Purpose Candidates PurposeSystem: On begin play, purpose components all need to be initialized
		/// From here, we want to access all cached actors, players included, and initialize their purpose ability component
		/// As well as add relevant datachunks
		/// 
		//playerManager->AddData(NewObject<UActorRole>(playerManager));
		//playerManager->AddData(&NewObject<UActorLocation>(playerManager)->Initialize(playerManager));///
		//Global::Log( Debug, ManagementLog, *this, "Spawn", TEXT("Spawning for role: %s"), spawnData->HasData(UActorRole::StaticClass()) ? *spawnData->DataChunk<UActorRole>()->ValueAsString() : TEXT("UnknownRole"));
		FActorSpawnParameters aiParam;
		aiParam.Owner = this;

		int zones = spawnData.spawner->spawnZones.Num() - 1;///In order to get a random point in a random zone, we need the indices of spawn zones

		if (zones < 0) { return; }///Ensure we don't execute further logic if spawnZones are empty

		int chosenZone = FMath::RandRange(0, zones);
		TObjectPtr<ATriggerBase> spawnZone = spawnData.spawner->spawnZones[chosenZone];

		if (!spawnZone) { return; }///In case a spawn zone element is added to array but isn't set to an actual trigger actor

		FCollisionShape shape = spawnZone->GetCollisionComponent()->GetCollisionShape();
		FVector spawnLocation = spawnZone->GetActorLocation();

		for (int i = 0; i < spawnData.spawner->numAI; i++)
		{
			switch (shape.ShapeType)
			{
				case ECollisionShape::Box:
					{
						UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
						FNavLocation location;
						//int count = 0;
						//do
						//{
							//++count;
						FVector zoneLocation = spawnZone->GetActorLocation();///since shape has no reference to world location, we can use the zone actor
						///min + max represent a line diagonally from one corner of a box(the min) to the farthest corner of the box(the max)
						FVector min = FVector(zoneLocation.X - shape.Box.HalfExtentX, zoneLocation.Y - shape.Box.HalfExtentY, zoneLocation.Z - shape.Box.HalfExtentZ);///Subtract half the box extent to get the "minimum"
						FVector max = FVector(zoneLocation.X + shape.Box.HalfExtentX, zoneLocation.Y + shape.Box.HalfExtentY, zoneLocation.Z + shape.Box.HalfExtentZ);///Add half the box extent to get the "maximum"

						spawnLocation = FMath::RandPointInBox(FBox(min, max));

						if (NavSys)
						{
							NavSys->ProjectPointToNavigation(spawnLocation, location);
						}
						//}/// Ensure that the point found is on navmesh, and limit retries to 5
						//while (!UNavigationSystemV1::GetNavigationSystem(GetWorld())->ProjectPointToNavigation(spawnLocation, location) || count < 5);
					}
					break;
				case ECollisionShape::Sphere:
					{
						UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
						FNavLocation location;

						FVector zoneLocation = spawnZone->GetActorLocation();

						if (NavSys)
						{
							NavSys->GetRandomPointInNavigableRadius(zoneLocation, shape.GetSphereRadius(), location);
							spawnLocation = location.Location;///Is there a difference?
							////Global::Log(Debug, ManagementLog, *this, "Spawn", TEXT("Sphere; SpawnLocation: %s"),
							//	*spawnLocation.ToString()
							//);
						}
					}
					break;
				default:
					//Global::Log(Debug, ManagementLog, *this, "Spawn", TEXT("No Shape was found for %s"), spawnData->HasData(UActorRole::StaticClass()) ? *spawnData->DataChunk<UActorRole>()->ValueAsString() : TEXT("UnknownRole"));
					return;///if we can't find a shape then we can't get a spawn location, better not to spawn in that case
			}

			/// Taken from ULyraBotCreationComponent::SpawnOneBot()
			FActorSpawnParameters SpawnInfo;
			SpawnInfo.Instigator = GetInstigator();
			SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnInfo.OverrideLevel = GetWorld()->GetCurrentLevel();
			SpawnInfo.ObjectFlags |= RF_Transient; /// Controllers and pawns should never be saved, only the data supporting them
			ALyraPlayerBotController* NewController = nullptr;

			spawnData.spawner->controller ? /// If a subclass is specified, spawn in that type
				NewController = GetWorld()->SpawnActor<ALyraPlayerBotController>(spawnData.spawner->controller, FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo)
				: NewController = GetWorld()->SpawnActor<ALyraPlayerBotController>(ALyraPlayerBotController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);

			if (!IsValid(NewController))
			{
				Global::LogError(MANAGEMENT, *this, "Spawn", TEXT("New controller for AI invalid!"));
				continue;
			}

			//NewController->CheckDefaultInitialization();

			ALyraGameMode* GameMode = GetWorld()->GetAuthGameMode<ALyraGameMode>();
			check(GameMode);

			///Todo: Lyra Teams AI Player; DispatchPostLogin leads to logic for assigning teams
			//GameMode->DispatchPostLogin(NewController);
			//GameMode->RestartPlayer(NewController);
			/*
			void ULyraTeamCreationComponent::OnPostLogin(AGameModeBase* GameMode, AController* NewPlayer)
			{
				check(NewPlayer);
				check(NewPlayer->PlayerState);


				AActor* UTDM_PlayerSpawningManagmentComponent::OnChoosePlayerStart(AController* Player, TArray<ALyraPlayerStart*>& PlayerStarts)
				{
				/Script/Engine.GameModeBase.FindPlayerStart
				[0817.08][991]LogOutputDevice: Error: Ensure condition failed: PlayerTeamId != INDEX_NONE [File:C:\Liege-Root\LiegeWarzone\Plugins\GameFeatures\ShooterCore\Source\ShooterCoreRuntime\Private\TDM_PlayerSpawningManagmentComponent.cpp] [Line: 31]
					*/

			if (!IsValid(GameMode->GetDefaultPawnClassForController(NewController)))
			{
				Global::LogError(MANAGEMENT, *this, "Spawn", TEXT("Pawn class not found for %s!"), *NewController->GetName());
				NewController->Destroy();
				continue;
			}

			FTransform spawnTransform(spawnData.spawner->GetActorRotation(), spawnLocation, FVector(1));

			APawn* NewPawn = GameMode->SpawnDefaultPawnAtTransform(NewController, spawnTransform);

			if (!IsValid(NewPawn))
			{
				Global::LogError(MANAGEMENT, *this, "Spawn", TEXT("Pawn was not created for %s!"), *NewController->GetName());
				NewController->Destroy();
				continue;
			}

			NewController->Possess(NewPawn);

			TObjectPtr<UPurposeAbilityComponent> purposeComp = Cast<UPurposeAbilityComponent>(NewController->AddComponentByClass(UPurposeAbilityComponent::StaticClass(), false, FTransform(FQuat(0), FVector(0), FVector(1)), false));

			if (!purposeComp)
			{
				Global::LogError(MANAGEMENT, *this, "Spawn", TEXT("PurposeComp was not created for %s!"), *NewController->GetName());
				NewController->Destroy();
				NewPawn->Destroy();
				continue;
			}

			purposeComp->InitAbilityActorInfo(NewController, NewPawn);/// AbilitySystemComp::OnRegister creates a new FGameplayAbilityActorInfo, not sure where avatar actor is set but it's not correctly set
			/// So we call this, even though PawnExtComponent->InitializeAbilitySystem also calls this

			purposeComp->AddSet<ULyraHealthSet>();/// Needed for LyraHealthComponent
			purposeComp->AddSet<ULyraCombatSet>();/// Not sure if necessary, but likely is for ShooterCore framework

			if (ULyraPawnExtensionComponent* PawnExtComponent = NewController->GetPawn()->FindComponentByClass<ULyraPawnExtensionComponent>())
			{
				PawnExtComponent->CheckDefaultInitialization();
				PawnExtComponent->InitializeAbilitySystem(purposeComp, NewController);
			}
			else
			{
				Global::LogError(MANAGEMENT, *this, "Spawn", TEXT("PawnExtensionComponent not found for %s!"), *NewController->GetName());
				NewController->Destroy();
				NewPawn->Destroy();
				continue;
			}


			purposeComp->InitializePurposeSystem(this, GetPurposeEvaluationThreads());
			if (spawnData.spawner->HasData(UActorRole::StaticClass())) { purposeComp->AddData(spawnData.spawner->DataChunk<UActorRole>()); }/// Ensure the actor's role reflects that or their manager
			purposeComp->AddData(&NewObject<UActorLocation>(purposeComp)->Initialize(purposeComp->GetOwner()));///
			///ai->AddData(NewObject<UManagementChunk>(ai)->Initialize(this));
			///ai->AddData(*DataChunk<ActorRace>());//Ensure race is established so a mesh and animBP can be sough

			purposeComp->AppendData(spawnData.spawner->DataMap());

			if (IsValid(purposeComp->GetOwnerCharacter()))
			{
				if (!IsValid(purposeComp->GetOwnerCharacter()->GetMesh()->GetSkeletalMeshAsset()) && spawnData.spawner->PotentialMeshesForActor().Num() > 0)
				{
					purposeComp->GetOwnerCharacter()->GetMesh()->SetSkeletalMeshAsset(spawnData.spawner->PotentialMeshesForActor()[FMath::RandRange(0, spawnData.spawner->PotentialMeshesForActor().Num() - 1)]);
				}

				if (!purposeComp->GetOwnerCharacter()->GetMesh()->HasValidAnimationInstance() && IsValid(spawnData.spawner->AnimBPForActor()))
				{
					purposeComp->GetOwnerCharacter()->GetMesh()->SetAnimClass(spawnData.spawner->AnimBPForActor());
					purposeComp->GetOwnerCharacter()->GetMesh()->InitAnim(true);
				}
			}
			else
			{
				Global::LogError(MANAGEMENT, *this, "InitializeAI", TEXT("%s is not an ACharacter!"), *purposeComp->GetOwner()->GetName());
			}

			candidateCacheForPurposeThread.Add(purposeComp);

			//if (GetWorld()->GetGameInstance<UGameInstance_Base>())//Ensure AI is given their skeletal mesh prior to establishing character data, as the weapon chunk requires finding a socket on SKM
			//{
			//	ai->GetMesh()->SetSkeletalMesh(Retrieval::GetSkeletalMesh(*this, ai->DataChunk<UCharacterRaceChunk>()->Value()));
			//	ai->GetMesh()->SetAnimInstanceClass(Retrieval::GetAnimationBlueprint(*this, ai->DataChunk<UCharacterRaceChunk>()->Value()));
			//}
		}
	}

	/// Once all components are initialized, all Events are loaded, then we need to establish the purpose threads with candidates and potential events
	/// 
	/// Once all purpose threads are initialized, we'll want to Initialize the first Occurrence
}

void ADirector_Level::LocateSpawnsInLevel()
{
	TArray<FAISpawnParameters> spawnParams;
	///Design: To get spawners with level streaming:
	/*
	UWorld* world = GetWorld();
	auto StreamingLevels = world->GetStreamingLevels();
	for (UStreamingLevel* StreamingLevel : StreamingLevels ) {
		ULevel* level = StreamingLevel->GetLoadedLevel();
		if(!level)
			continue;
		for (AActor* Actor : level->Actors)
		{
			// Actor
		}
	*/

	/// To get spawners in loaded level through iteration:
	for (TActorIterator<AAISpawner> ActorItr(GetWorld(), AAISpawner::StaticClass()); ActorItr; ++ActorItr)
	{
		if (IsValid(*ActorItr))
		{
			//spawnParams.Append(ActorItr->spawnParams);
			SpawnCache.Add(FAISpawnEntry(*ActorItr));
		}
	}
	Global::Log(DATAESSENTIAL, MANAGEMENT, *this, "InitializeManagement", TEXT("Spawners found: %d."), SpawnCache.Num());
}

bool ADirector_Level::CheckAISpawnConditions(const FAISpawnEntry& spawnEntry)
{
	return true;/// Todo: Management AISpawner; establish evaluation of spawner conditions for spawning AI
	return false;
}

bool ADirector_Level::PerformVisualEQS()
{
	Global::Log(FULLTRACE, MANAGEMENT, GetName(), "PerformVisualEQS", TEXT(""));

	if (GetWorld()->TimeSince(timeSinceLastEQS.GetRealTimeSeconds()) > timeBetweenEQSQueries)
	{
		timeSinceLastEQS = GetWorld()->GetTime();
		return true;
	}

	Global::Log(FULLTRACE, MANAGEMENT, GetName(), "PerformVisualEQS", TEXT("Performing EQS"));

	/// Since player doesn't have an AI controller or blackboard, we need to run the query directly. 
		/// We'll have to solve how to correctly set query request params
		/// And what they even are
		/// For now, sight queries have to generate around the Context_Querier
	if (!PlayerSightQuery)
	{
		Global::LogError(MANAGEMENT, GetName(), "PerformVisualEQS", TEXT("Player sight EQS invalid!"));

		return false;
	}

	for (APlayerController* controller : players)
	{

		FEnvQueryRequest QueryRequest(playerSightEQSCache.Get(), controller);

		//if (QueryConfig.Num() > 0)
		//{
		//    // resolve 
		//    for (FAIDynamicParam& RuntimeParam : QueryConfig)
		//    {
		//        QueryRequest.SetDynamicParam(RuntimeParam, BlackboardComponent);
		//    }
		//}

		QueryRequest.Execute(EEnvQueryRunMode::AllMatching, FQueryFinishedSignature::CreateUObject(this, &ADirector_Level::QueryFinished, controller));
	}
	return true;
}

void ADirector_Level::QueryFinished(TSharedPtr<FEnvQueryResult> Result, APlayerController* inPlayer)
{
	if (IsValid(this)) /// In case a delegate attempts to callback and manager is being destroyed
	{
		return;
	}

	Global::Log(CALLTRACETRIVIAL, MANAGEMENT, GetName(), "QueryFinished", TEXT("Receiving Results."));

	if (!Result.IsValid())
	{
		Global::LogError(MANAGEMENT, GetName(), "QueryFinished", TEXT("Query chosenActor is invalid!"));
		return;
	}

	float highItemScore = 0.0f;
	TObjectPtr<AActor> chosenActor = nullptr;
	FVector chosenLocation;

	for (int i = 0; i < Result->Items.Num(); ++i)
	{
		if (Result->GetItemScore(i) < highItemScore)
		{
			continue;/// ignore the result
		}

		highItemScore = Result->GetItemScore(i);

		if (chosenActor = Result->GetItemAsActor(i))
		{
			Global::Log(DATATRIVIAL, MANAGEMENT, GetName(), "QueryFinished", TEXT("Actor: %s. Item Score: %f; High Item Score: %f."), *chosenActor.GetName(), Result->GetItemScore(i), highItemScore);
		}
	}

	if (!chosenActor)
	{
		Global::Log(DATADEBUG, MANAGEMENT, GetName(), "QueryFinished", TEXT("No actor with visuals on player!"));

	}
	Global::Log(DATATRIVIAL, MANAGEMENT, GetName(), "QueryFinished", TEXT("%s has visual on player!"), *chosenActor->GetName());

	/// The actor chosen as most relevant by the EQS query specified in the mangement settings now needs to react to sighting a player via an occurrence
	if (TObjectPtr<UPurposeAbilityComponent> purposeComp = chosenActor->GetInstigatorController()->FindComponentByClass<UPurposeAbilityComponent>())
	{
		/*TObjectPtr<UAction_Declaration> declaration = NewObject<UAction_Declaration>(chosenActor);
		declaration->InitializeAction(*purposeComp.Get(), Player());

		TObjectPtr<UOccurrenceContext> occurrence = UOccurrenceContext::CreateContext(purposeComp);

		occurrence->AddData(&NewObject<UActorAction>(occurrence)->Initialize(declaration->GetClass()));
		occurrence->AddSubject(ESubject::EventTarget, Player());

		/// Ensure the target location is stored in as a contextual POI so that we can reference as last known location if needed
		occurrence->AddData(&NewObject<UContextualPointOfInterest>(occurrence.Get())->Initialize(chosenActor->GetActorLocation()));

		/// We also want to store the sense type, as that will also help refine further behaviors
		occurrence->AddData(&NewObject<UContextualAISenseUtilized>(occurrence.Get())->Initialize(UAISense_Sight::StaticClass()));

		purposeComp->Manager()->PerformAction(purposeComp, *declaration, occurrence);*/

		FSubjectMap subjects;
		subjects.subjects.Add(ESubject::Instigator, chosenActor);
		subjects.subjects.Add(ESubject::EventTarget, GetPurposeComponentFromPlayerController(inPlayer));

		TArray<FDataMapEntry> contextualData;

		PurposeSystem::Occurrence(subjects, contextualData, GetPurposeEvaluationThreads());
	}
}

TObjectPtr<UPurposeAbilityComponent> ADirector_Level::GetPurposeComponentFromPlayerController(APlayerController* inPlayer)
{
	TObjectPtr<UPurposeAbilityComponent> purposeComp = nullptr;

	if (!IsValid(inPlayer))
	{
		return nullptr;
	}

	purposeComp = inPlayer->FindComponentByClass<UPurposeAbilityComponent>();

	if (!purposeComp)/// In the LyraGame sample project the ability component is placed on the player state rather than the controller
	{
		purposeComp = inPlayer->PlayerState->FindComponentByClass<UPurposeAbilityComponent>();
	}

	return purposeComp;
}

#pragma endregion

//UGlobalManagementSettings::UGlobalManagementSettings(const FObjectInitializer& ObjectInitializer)
//	: Super(ObjectInitializer)
//{
//}
