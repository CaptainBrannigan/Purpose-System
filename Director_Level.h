// Copyright Jordan Cain. All Rights Reserved.

#pragma once

#include "Engine.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"
#include "GlobalLog.h"
#include "Purpose/PurposeEvaluationThread.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "AISpawn.h"
#include "Engine/AssetManager.h"
#include "Director_Level.generated.h"


USTRUCT(BlueprintType)
//
struct FAISpawnEntry
{
	GENERATED_BODY()

	FAISpawnEntry()
	{
	}
	
	FAISpawnEntry(TWeakObjectPtr<AAISpawner> inSpawner)
		: spawner(inSpawner)
	{
	}

	UPROPERTY()
	TWeakObjectPtr<AAISpawner> spawner = nullptr;

	UPROPERTY()
	///Time in seconds from beginning of level when AI were spawned
	int spawnTime = 0;
};

UCLASS()
/// <summary>
///The Level Director is responsible for high level actor management
///They will establish managers
///They will control the event system
/// </summary>
class LYRAGAME_API ADirector_Level : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ADirector_Level();

	/// Used in place of EndPlay so that we can ensure the PurposeThreads and any Async tasks that hold Rooted ContextDatas are correctly disposed of
	virtual void SafelyShutdownGame(TObjectPtr<APlayerController> inPlayer);
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	virtual void BeginDestroy() override;

	/// To be called by a timer delegate
	/// Can't pass UKismetLibrary::QuitGame to a FTimerDelegate::BindStatic() because it requires a const pointer, which Iguess doesn't work with delegates
	void DirectorLatentSafeShutdown(TObjectPtr<APlayerController> inPlayer);
public:	

	///AISpawners placed in level will seek the level director, and provide "spawnData"
	///AISpawners will be responsible for spawning both managers, and providing those managers with spawnData for AI
	//void Spawn(const TArray<FAISpawnParameters>& spawnData) final;

#pragma region Event System
public:

	void EventAssetsLoaded();

	/// Director will seek out any activities within a level on BeginPlay
	/// These activities will be stored alongside other Events and also stored as an active Event
	/// They are essentially the first Occurrences
	void SeekActivitiesInLevel();

protected:

	UPROPERTY(VisibleAnywhere)
	TArray<FPurpose> eventCacheForPurposeSystem;

private:

	//Thread Safety Tips:
	/*
		"Are you just reading the members? If so, that should be fine. If you are changing them/writing to them, then you should be blocking those with FCriticalSection objects to make sure two threads don’t try and change the same object at the same time."
		https://forums.unrealengine.com/t/uobjects-thread-safety/33513
	*/

	bool StopThreads() { return stopThreads; }

	///Container for the background purpose selection thread object
	FPurposeEvaluationThread* eventThread = nullptr;
	///Container for the background running eventThread
	FRunnableThread* currentEventThread = nullptr;

	///Container for the background task selection thread object
	FPurposeEvaluationThread* actorThread = nullptr;
	///Container for the background running actorThread
	FRunnableThread* currentActorThread = nullptr;

	FPurposeEvaluationThread* GetEventThread()
	{
		if (stopThreads)
		{
			return nullptr;
		}
		if (eventThread && currentEventThread)
		{
			////Global::Log(EHierarchicalCalltraceVerbosity::DEBUG, *this, "Occurrence", TEXT("Adding occurrence to queue."));
			return eventThread;
		}

		return nullptr;
	}

	FPurposeEvaluationThread* GetActorThread()
	{
		if (stopThreads)
		{
			return nullptr;
		}
		if (actorThread && currentActorThread)
		{
			////Global::Log(EHierarchicalCalltraceVerbosity::DEBUG, *this, "Occurrence", TEXT("Adding occurrence to queue."));
			return actorThread;
		}

		return nullptr;
	}

	TArray<FPurposeEvaluationThread*> GetPurposeEvaluationThreads()
	{
		return TArray<FPurposeEvaluationThread*>({ eventThread, actorThread });
	}

	///Initialize background threads for Purpose Evaluation
	void Init()
	{
		////Global::Log(Debug, Purpose, *this, "Init", TEXT("Creating Event thread."));
		eventThread = new FPurposeEvaluationThread(eventCacheForPurposeSystem, candidateCacheForPurposeThread);
		eventThread->stopThread = false;
		currentEventThread = FRunnableThread::Create(eventThread, TEXT("PrimaryPurposeThread"));

		////Global::Log(Debug, Purpose, *this, "Init", TEXT("Creating Actor thread."));
		actorThread = new FPurposeEvaluationThread(eventCacheForPurposeSystem, candidateCacheForPurposeThread);
		actorThread->stopThread = false;
		currentActorThread = FRunnableThread::Create(actorThread, TEXT("SecondaryPurposeThread"));
	}

	///Shutdown background threads for Purpose Evaluation
	void Shutdown()
	{
		stopThreads = true;

		if (actorThread && eventThread)
		{
			eventThread->Stop();/// Will be called automatically from Kill(), but no reason not to call it here first as it will stop Run() early
			actorThread->Stop();/// Will be called automatically from Kill(), but no reason not to call it here first as it will stop Run() early
		}

		ClearQueues();/// After stopping the threads, ensure all queues are cleared before shutdown

		if (eventThread && currentEventThread)
		{
			currentEventThread->Kill(true);
			currentEventThread->WaitForCompletion();///Allow the current calculation to complete before we delete the thread

			eventThread->Exit();
			////Global::Log(Debug, Purpose, *this, "Shutdown", TEXT("Deleting eventThread"));
			delete eventThread; ///ensure the non-UObject memory is deleted
			eventThread = nullptr;
		}
		else if (eventThread)
		{
			eventThread->Exit();
			////Global::Log(Debug, Purpose, *this, "Shutdown", TEXT("Deleting eventThread"));
			delete eventThread;
			eventThread = nullptr;
		}

		if (actorThread && currentActorThread)
		{
			currentActorThread->Kill(true);
			currentActorThread->WaitForCompletion();///Allow the current calculation to complete before we delete the thread

			actorThread->Exit();
			////Global::Log(Debug, Purpose, *this, "Shutdown", TEXT("Deleting eventThread"));
			delete actorThread; ///ensure the non-UObject memory is deleted
			actorThread = nullptr;
		}
		else if (actorThread)
		{
			actorThread->Exit();
			////Global::Log(Debug, Purpose, *this, "Shutdown", TEXT("Deleting actorThread"));
			delete actorThread;
			actorThread = nullptr;
		}
	}

	/// Because every context data being sent to the background thread must be added to the root set manually
	/// We have to cover all cases where it will have to be removed manually, such as shutdown
	void ClearQueues();

private:

	///Used to prevent ASyncTasks from attempting to queue purpose after thread completion
	bool stopThreads = false;

#pragma endregion

#pragma region Management
public:

	/// Level Director will seek out spawners
	/// When found, will create Managers
	/// Managers will then spawn in relevant AI
	void InitiateManagement();

	void AddNewPlayer(TObjectPtr<class APlayerController> newPlayer) { players.Add(newPlayer); }

protected:

	UPROPERTY()
	TArray<FAISpawnEntry> SpawnCache;

	UPROPERTY()
	TArray<TObjectPtr<class UPurposeAbilityComponent>> candidateCacheForPurposeThread;

	UPROPERTY()
	TArray<TObjectPtr<class APlayerController>> players;

	/// When a Director is created for a level, they must seek out AI Spawners
	/// Then they can create Managers to populate the level with AI actors
	/// These Spawners+Managers are cached
	/// Caching will allow Director to monitor AI population, spawn timing, etc.
	void LocateSpawnsInLevel();

	/// Level Director will iterate over known spawners, checking whether their AI spawn conditions have been met
	/// @return bool: True if the spawn conditions of the spawnEntry are met
	bool CheckAISpawnConditions(const FAISpawnEntry& spawnEntry);

	/// Managers periodically will poll for EQS sight perceptions
	/// Super:: implementation determines if the EQS can be performed this tick
	virtual bool PerformVisualEQS();

	void QueryFinished(TSharedPtr<struct FEnvQueryResult> Result, class APlayerController* inPlayer);

	TObjectPtr<class UPurposeAbilityComponent> GetPurposeComponentFromPlayerController(class APlayerController* inPlayer);

	FGameTime timeSinceLastEQS;

	UPROPERTY(EditAnywhere, Category="Management");
	float timeBetweenEQSQueries = 0.25f;

	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	/// This EQS query is the method by which we establish player sight occurrences
	TObjectPtr<class UEnvQuery> PlayerSightQuery = nullptr;

#pragma endregion

};

