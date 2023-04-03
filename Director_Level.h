// Copyright Jordan Cain. All Rights Reserved.

#pragma once

#include "Engine.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"
#include "GlobalLog.h"
#include "Purpose/Context/ContextData.h"
#include "Purpose/PurposeEvaluationThread.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "DataMapInterface.h"
#include "AISpawn.h"
#include "Engine/AssetManager.h"
#include "Director_Level.generated.h"

UCLASS(NotPlaceable)
/// <summary>
///The Level Director is responsible for high level actor management
///They will establish managers
///They will control the event system
/// </summary>
class ADirector_Level : public AActor, public IDataMapInterface, public IPurposeManagementInterface
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ADirector_Level();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;


private:

	UPROPERTY(Replicated)
	/// This data is not currently and may never be utilized
	TArray<FDataMapEntry> data;

	/// Force implementers to provide an editable datamap
	/// The only way to the change the DataMap is through the Server RPCs
	TArray<FDataMapEntry>& DataMapInternal() final { return data; }

	UPROPERTY(Replicated)
	/// Level Directors are responsible for providing managers with Event direction from within their level
	TArray<TObjectPtr<class AManager>> managers;

	UPROPERTY(VisibleAnywhere)
	/// Each client level director requires a reference to the client's Manager
	/// This allows each client to maintain a manager as well as allows the Server to tell clients to run logic on their own manager
	TArray<TObjectPtr<class AManager_Player>> playerManagers;

#pragma region Event System
public:

	/// Used in order to reference up the management chain to the owner of Events and Backgrounds threads
	TScriptInterface<IPurposeManagementInterface> GetHeadOfPurposeManagment() final { return this; }

	/// @return TScriptInterface<IPurposeManagementInterface>: Returns the immediate purpose manager above caller
	TScriptInterface<IPurposeManagementInterface> GetPurposeSuperior() final { return this; }

	TArray<FPurposeEvaluationThread*> GetBackgroundPurposeThreads() final;

	TArray<TScriptInterface<IDataMapInterface>> GetCandidatesForSubPurposeSelection(const int PurposeLayerForUniqueSubjects) final;

	/// @param PurposeLayerForUniqueSubjects: Represents the purpose layer for which the PurposeOwner is meant to create new FUniqueSubjectMaps
	/// @param parentContext:
	/// @param candidate: This is the primary subject that will be combined with other subjects as needed for purpose selection
	/// @return TArray<FSubjectMap>: Each entry is a combination of the candidate and whatever other subjects required for the subpurpose indicated by addressOfSubPurpose
	virtual TArray<FSubjectMap> GetUniqueSubjectsRequiredForSubPurposeSelection(const int PurposeLayerForUniqueSubjects, const FContextData& parentContext, TScriptInterface<IDataMapInterface> candidate, FPurposeAddress addressOfSubPurpose) override;

	bool ProvidePurposeToOwner(const FContextData& purposeToStore) final;

	/// Events must be stored globally for the duration of a game so that they may have a consistent PurposeAddress
	TArray<FPurpose> GetEventAssets() final;

	/// As FPurpose can not hold an variable or TArray<> of itself, we're forced to workaround simply accessing subpurposes
	TArray<FPurpose> GetSubPurposesFor(FPurposeAddress address) final;

	/// When a purpose is put up for selection, but it appears to be a duplicate of a current purpose, we want to let the purpose owner handle the reoccurrence
	void PurposeReOccurrence(const FPurposeAddress addressOfPurpose, const int64 uniqueIDofActivePurpose) final;

	/// @param uniqueIdentifierOfContextTree: This ID unique to a series of context datas starting with Event allows separation of same purposes for different contexts
	/// @param fullAddress: Tying the address to the unique ID is how we can search stored contexts for the relevant context we seek
	/// @param layerToRetrieveFor: We may not necessarily wish to find the end address of the fullAddress, so we can indicate a layer to seek out
	/// @return FContextData&: Need to check for validity as the context data may not have been found and an empty struct returned
	FContextData& GetStoredPurpose(const int64 uniqueIdentifierOfContextTree, const FPurposeAddress& fullAddress, const int layerToRetrieveFor) final;

	/// @param parentAddress: An address which is either that of a purpose containing behaviors so that it may reference the parent, or the parent address itself
	/// @param TArray<TObjectPtr<UGA_Behavior>>: All the behaviors contained by the parent indicated
	TArray<TObjectPtr<class UBehavior_AI>> GetBehaviorsFromParent(const FPurposeAddress& parentAddress) final;

	/// @param parentAddress: An address of the behavior containing purpose
	/// @param TObjectPtr<UGA_Behavior>: The behavior contained by the address provided
	TObjectPtr<class UBehavior_AI> GetBehaviorAtAddress(const FPurposeAddress& inAddress) final;

	///@return bool: True when the Target+Action are the same
	bool DoesPurposeAlreadyExist(const FContextData& primary, const FSubjectMap& secondarySubjects, const TArray<FDataMapEntry>& secondaryContext, const FPurposeAddress optionalAddress = FPurposeAddress()) final;

	void SubPurposeCompleted(const int64& uniqueContextID, const FPurposeAddress& addressOfPurpose);

	void AllSubPurposesComplete(const int64& uniqueContextID, const FPurposeAddress& addressOfPurpose);


	/// As purposes of FContextData are stored as FPurpose, we have no reference to sub purposes or what they actually are
	///@return TArray<FTaskLayer>: So we trace the address from Event to the Objective and return it's sub tasks
	TArray<FTaskLayer> GetTasksOfObjective(const FPurposeAddress& address);

	/// As purposes of FContextData are stored as FPurpose, we have no reference to sub purposes or what they actually are
	///@return TArray<FTaskLayer>: So we trace the address from Event to the Objective and return it's sub tasks
	bool GetGoalLayer(const FPurposeAddress& address, FGoalLayer& outGoal);
	
	/// As purposes of FContextData are stored as FPurpose, we have no reference to sub purposes or what they actually are
	///@return TArray<FTaskLayer>: So we trace the address from Event to the Objective and return it's sub tasks
	bool GetEventLayer(const FPurposeAddress& address, FEventLayer& outEvent);

	void EventAssetsLoaded();

	void GoalComplete(const int64& uniqueContextID, const FPurposeAddress& addressOfGoal);

	/// Director will seek out any activities within a level on BeginPlay
	/// These activities will be stored alongside other Events and also stored as an active Event
	/// They are essentially the first Occurrences
	void SeekActivitiesInLevel();



protected:

	/// Stored as a copy since the background threads are who create the context data
	/// So long as we have purpose address and unique context ids, it's not a big deal. Each context data is pretty data lightweight
	TArray<FContextData> eventsActive;

	/// Stored as a copy because FEventLayer can come from any source potentially, from UDataAsset to AActor
	/// They aren't important, only the chain of purpose and their Conditions are
	TArray<FEventLayer> eventCacheForPurposeSystem;

private:

	//Thread Safety Tips:
	/*
		"Are you just reading the members? If so, that should be fine. If you are changing them/writing to them, then you should be blocking those with FCriticalSection objects to make sure two threads don’t try and change the same object at the same time."
		https://forums.unrealengine.com/t/uobjects-thread-safety/33513
	*/

	bool StopThreads() { return stopThreads; }

	///Container for the background purpose selection thread object
	FEventThread* eventThread = nullptr;
	///Container for the background running eventThread
	FRunnableThread* currentEventThread = nullptr;

	///Container for the background task selection thread object
	FActorThread* actorThread = nullptr;
	///Container for the background running actorThread
	FRunnableThread* currentActorThread = nullptr;

	FEventThread* GetEventThread()
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

	FActorThread* GetActorThread()
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

	///Initialize background threads for Purpose Evaluation
	void Init()
	{
		////Global::Log(Debug, Purpose, *this, "Init", TEXT("Creating Event thread."));
		eventThread = new FEventThread(ObjectiveQueue, GoalQueue, OccurrenceQueue);
		eventThread->stopThread = false;
		currentEventThread = FRunnableThread::Create(eventThread, TEXT("Event Thread"));

		////Global::Log(Debug, Purpose, *this, "Init", TEXT("Creating Actor thread."));
		actorThread = new FActorThread(ReactionQueue, TasksQueue);
		actorThread->stopThread = false;
		currentActorThread = FRunnableThread::Create(actorThread, TEXT("Actor Thread"));
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

};
