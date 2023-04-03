// Copyright Jordan Cain. All Rights Reserved.


#include "Purpose/Director_Level.h"
#include "Purpose/Manager.h"
#include "Manager_Player.h"
#include "Purpose/Context/OccurrenceContext.h"
#include "Purpose/Context/EventContext.h"
#include "Purpose/Context/GoalContext.h"
#include "PurposeAssetManager.h"
#include "Purpose/Assets/EventAsset.h"
#include "Purpose/DataChunks/TrackedPurposes.h"
#include "EngineUtils.h"
#include "AIActivity.h"
#include "Purpose/DataChunks/ActorRole.h"
#include "Purpose/DataChunks/ActorLocation.h"
#include "Purpose/DataChunks/ActorAction.h"

// Sets default values
ADirector_Level::ADirector_Level()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
}

// Called when the game starts or when spawned
void ADirector_Level::BeginPlay()
{
	Super::BeginPlay();
	Global::Log(CALLTRACEESSENTIAL, MANAGEMENT, *this, "BeginPlay", TEXT(""));
	AddData(NewObject<UTrackedPurposes>(this));

	Init();

	UAssetManager* assetManager = GEngine->AssetManager;
	if (!assetManager)
	{
		Global::LogError(MANAGEMENT, * this, "BeginPlay", TEXT("Asset manager invalid!"));
		return;
	}

	TSharedPtr<FStreamableHandle> callback = assetManager->LoadPrimaryAssetsWithType(UEventAsset::PrimaryAssetType(), TArray<FName>(), FStreamableDelegate::CreateUObject(this, &ADirector_Level::EventAssetsLoaded));

	if (!callback)
	{
		Global::LogError(PURPOSE, *this, "BeginPlay", TEXT("Callback to load EventAssets invalid!."));
		return;
	}
}


#pragma region Event System

///Debug: Lyra Occurrence Purpose; Why are occurrences being constantly called? Is it because of Lyra GA's?
	/// I believe it is due to Gameplay ability for auto reload
	/// Can also be due to a Behavior Tree having Wait task


TArray<FPurposeEvaluationThread*> ADirector_Level::GetBackgroundPurposeThreads()
{
	TArray<FPurposeEvaluationThread*> threads;

	threads.Add(eventThread);
	threads.Add(actorThread);

	return threads;
}

TArray<TScriptInterface<IDataMapInterface>> ADirector_Level::GetCandidatesForSubPurposeSelection(const int PurposeLayerForUniqueSubjects)
{
	TArray<TScriptInterface<IDataMapInterface>> candidates;

	Global::Log(DATADEBUG, EVENT, *this, "GetCandidatesForSubPurposeSelection", TEXT("Seeking candidates for layer %s.")
		, *Global::EnumValueOnly<EPurposeLayer>(PurposeLayerForUniqueSubjects)
	);
	switch (PurposeLayerForUniqueSubjects)
	{
		case (int)EPurposeLayer::Event:
			candidates.Add(this);

			Global::Log(DATADEBUG, EVENT, *this, "GetCandidatesForSubPurposeSelection", TEXT("Providing %s as candidate to layer %s.")
				, *GetName()
				, *Global::EnumValueOnly<EPurposeLayer>(EPurposeLayer::Event)
			);
			break;
		case (int)EPurposeLayer::Goal:
			for (TObjectPtr<AManager> manager : managers)
			{
				candidates.Add(manager);
				Global::Log(DATADEBUG, EVENT, *this, "GetCandidatesForSubPurposeSelection", TEXT("Providing %s as candidate to layer %s.")
					, *manager->GetName()
					, *Global::EnumValueOnly<EPurposeLayer>(EPurposeLayer::Goal)
				);
			}
			break;
	}

	return candidates;
}

TArray<FSubjectMap> ADirector_Level::GetUniqueSubjectsRequiredForSubPurposeSelection(const int PurposeLayerForUniqueSubjects, const FContextData& parentContext, TScriptInterface<IDataMapInterface> candidate, FPurposeAddress addressOfSubPurpose)
{
	TArray<FSubjectMap> uniqueSubjects;
	FSubjectMap subjectMap;

	if (!IsValid(candidate.GetObject()))
	{
		Global::LogError( PURPOSE, * this, "GetUniqueSubjectsRequiredForSubPurposeSelection", TEXT("Candidate for layer %s is invalid!.")
			, *Global::EnumValueOnly<EPurposeLayer>(PurposeLayerForUniqueSubjects)
		);
		return uniqueSubjects;
	}

	switch (PurposeLayerForUniqueSubjects)
	{
		case (int)EPurposeLayer::Event:
		case (int)EPurposeLayer::Goal:
			subjectMap.subjects.Add(ESubject::Candidate, candidate);

			Global::Log(DATADEBUG, PURPOSE, *this, "GetUniqueSubjectsRequiredForSubPurposeSelection", TEXT("Providing %s as candidate to layer %s.")
				, *candidate.GetObject()->GetName()
				, *Global::EnumValueOnly<EPurposeLayer>(PurposeLayerForUniqueSubjects)
			);
			uniqueSubjects.Add(subjectMap);
			break;
	}

	return uniqueSubjects;
}

bool ADirector_Level::ProvidePurposeToOwner(const FContextData& purposeToStore)
{
	switch (purposeToStore.addressOfPurpose.GetAddressLayer())
	{
		case (int)EPurposeLayer::Event:
			if (!HasData(UTrackedPurposes::StaticClass()))
			{
				Global::LogError(EVENT, *this, "ProvidePurposeToOwner", TEXT("Director does not have tracked purposes!"));
				return false;
			}

			if (!DataChunk<UTrackedPurposes>()->Value().Contains(purposeToStore))
			{
				DataChunk<UTrackedPurposes>()->AddToValue(purposeToStore);///Ensure that selected context is stored until it ends
				Global::Log(DATADEBUG, EVENT, *this, "ProvidePurposeToOwner", TEXT("Adding Purpose: %s; Description: %s")
					, *purposeToStore.GetName()
					, *purposeToStore.Description()
				);
				return true;
			}
			else
			{
				Global::Log(DATADEBUG, EVENT, *this, "ProvidePurposeToOwner", TEXT("Purpose: %s is already contained!")
					, *purposeToStore.GetName()
				);

				
			}
		break;
	}

	Global::Log(DATADEBUG, EVENT, *this, "ProvidePurposeToOwner", TEXT("Purpose: %s was not stored! Address: %s.")
		, *purposeToStore.GetName()
		, *purposeToStore.addressOfPurpose.GetAddressAsString()
	);

	return false;
}

TArray<FPurpose> ADirector_Level::GetEventAssets()
{
	TArray<FPurpose> purposes;
	for (FEventLayer& storedEvent : eventCacheForPurposeSystem)
	{
		purposes.Add(storedEvent);
	}

	return purposes;
}

TArray<FPurpose> ADirector_Level::GetSubPurposesFor(FPurposeAddress address)
{
	TArray<FPurpose> subPurposes;

	int purposeLayer = address.GetAddressLayer();

	int eventAddress = address.GetAddressForLayer((int)EPurposeLayer::Event);

	if (!eventCacheForPurposeSystem.IsValidIndex(eventAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *address.GetAddressAsString());
		return TArray<FPurpose>();
	}
	
	TArray<FGoalLayer>& goals = eventCacheForPurposeSystem[eventAddress].goals;

	if (purposeLayer == (int)EPurposeLayer::Event)
	{
		for (FPurpose& goal : goals)
		{
			subPurposes.Add(goal);
		}
		return subPurposes;
	}

	int goalAddress = address.GetAddressForLayer((int)EPurposeLayer::Goal);

	if (!goals.IsValidIndex(goalAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *address.GetAddressAsString());
		return TArray<FPurpose>();
	}

	TArray<FObjectiveLayer>& objectives = goals[goalAddress].objectives;

	if (purposeLayer == (int)EPurposeLayer::Goal)
	{
		for (FPurpose& objective : objectives)
		{
			subPurposes.Add(objective);
		}
		return subPurposes;
	}

	int objectiveAddress = address.GetAddressForLayer((int)EPurposeLayer::Objective);

	if (!objectives.IsValidIndex(objectiveAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *address.GetAddressAsString());
		return TArray<FPurpose>();
	}

	TArray<FTaskLayer>& tasks = objectives[goalAddress].tasks;

	if (purposeLayer == (int)EPurposeLayer::Objective)
	{
		for (FPurpose& task : tasks)
		{
			subPurposes.Add(task);
		}
		return subPurposes;
	}

	return TArray<FPurpose>();
}

void ADirector_Level::PurposeReOccurrence(const FPurposeAddress addressOfPurpose, const int64 uniqueIDofActivePurpose)
{
	switch (addressOfPurpose.GetAddressLayer())
	{
		case (int)EPurposeLayer::Event: /// If an Event reoccurs, we want to notify the manager that the goals may be reevaluated as desired

			for (TObjectPtr<AManager> manager : managers)
			{
				if (!IsValid(manager))
				{
					Global::LogError(MANAGEMENT, *this, "ReOccurrenceOfEventObjectives", TEXT("A manager is invalid!"));
					continue;
				}

				manager->ReevaluateObjectivesForAllCandidates(addressOfPurpose, uniqueIDofActivePurpose);
			}
		break;
	}
}

FContextData& ADirector_Level::GetStoredPurpose(const int64 uniqueIdentifierOfContextTree, const FPurposeAddress& fullAddress, const int layerToRetrieveFor)
{
	switch (layerToRetrieveFor)
	{
		case (int)EPurposeLayer::Event:

			for(FContextData& context : DataChunk<UTrackedPurposes>()->ValueNonConst())
			{
				if (context.GetContextID() == uniqueIdentifierOfContextTree && context.addressOfPurpose.GetAddressForLayer(layerToRetrieveFor) == fullAddress.GetAddressForLayer(layerToRetrieveFor))
				{
					return context;
				}
			}
			break;
	}
	return FContextData();
}

TArray<TObjectPtr<UBehavior_AI>> ADirector_Level::GetBehaviorsFromParent(const FPurposeAddress& parentAddress)
{
	TArray<TObjectPtr<UBehavior_AI>> behaviors;
	TArray<FTaskLayer> tasks = GetTasksOfObjective(parentAddress);

	for (FTaskLayer& task : tasks)
	{
		behaviors.Add(task.behaviorAbility);
	}

	return behaviors;
}

TObjectPtr<UBehavior_AI> ADirector_Level::GetBehaviorAtAddress(const FPurposeAddress& inAddress)
{
	int eventAddress = inAddress.GetAddressForLayer((int)EPurposeLayer::Event);

	if (!eventCacheForPurposeSystem.IsValidIndex(eventAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *inAddress.GetAddressAsString());
		return nullptr;
	}

	TArray<FGoalLayer>& goals = eventCacheForPurposeSystem[eventAddress].goals;

	int goalAddress = inAddress.GetAddressForLayer((int)EPurposeLayer::Goal);

	if (!goals.IsValidIndex(goalAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *inAddress.GetAddressAsString());
		return nullptr;
	}

	TArray<FObjectiveLayer>& objectives = goals[goalAddress].objectives;

	int objectiveAddress = inAddress.GetAddressForLayer((int)EPurposeLayer::Objective);

	if (!objectives.IsValidIndex(objectiveAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *inAddress.GetAddressAsString());
		return nullptr;
	}

	TArray<FTaskLayer>& tasks = objectives[goalAddress].tasks;

	int taskAddress = inAddress.GetAddressForLayer((int)EPurposeLayer::Behavior);

	if (!tasks.IsValidIndex(taskAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *inAddress.GetAddressAsString());
		return nullptr;
	}

	return tasks[taskAddress].behaviorAbility;
}

bool ADirector_Level::DoesPurposeAlreadyExist(const FContextData& primary, const FSubjectMap& secondarySubjects, const TArray<FDataMapEntry>& secondaryContext, const FPurposeAddress optionalAddress)
{
	//If the Instigator + action + target is already contained, ignore objective
	if///If the action + target is already contained, ignore purpose
		/// Design: AI Purpose Occurrence; New similarity comparison ignores instigator to avoid duplicate occurrences causing AI to swap objectives unnecsessarily
			///As an exmaple, if two AI both spot the same player, then the second occurrence will be ignored 
		(
			/*primary.Subject(ESubject::Instigator) == secondary.Subject(ESubject::Instigator)
			&&*/ primary.Subject(ESubject::EventTarget) == (secondarySubjects.subjects.Contains(ESubject::EventTarget) ? secondarySubjects.subjects[ESubject::EventTarget].GetObject() : nullptr)
			&& DataMapGlobals::HasData(primary.contextData, UActorAction::StaticClass()) && DataMapGlobals::HasData(secondaryContext, UActorAction::StaticClass())
			&& DataMapGlobals::DataChunk<UActorAction>(primary.contextData)->Value() == DataMapGlobals::DataChunk<UActorAction>(secondaryContext)->Value()
			)
	{
		return true;
	}

	if///Same as above, except if the Instigator role is switched, essentially meaning the previous target is returning the same action
		(
			primary.Subject(ESubject::Instigator) == (secondarySubjects.subjects.Contains(ESubject::EventTarget) ? secondarySubjects.subjects[ESubject::EventTarget].GetObject() : nullptr)
			&& primary.Subject(ESubject::EventTarget) == (secondarySubjects.subjects.Contains(ESubject::Instigator) ? secondarySubjects.subjects[ESubject::Instigator].GetObject() : nullptr)
			&& DataMapGlobals::HasData(primary.contextData, UActorAction::StaticClass()) && DataMapGlobals::HasData(secondaryContext, UActorAction::StaticClass())
			&& DataMapGlobals::DataChunk<UActorAction>(primary.contextData)->Value() == DataMapGlobals::DataChunk<UActorAction>(secondaryContext)->Value()
			)
	{
		return true;
	}

	return false;
}

void ADirector_Level::SubPurposeCompleted(const int64& uniqueContextID, const FPurposeAddress& addressOfPurpose)
{
	switch (addressOfPurpose.GetAddressLayer())
	{
		case (int)EPurposeLayer::Goal:
			GoalComplete(uniqueContextID, addressOfPurpose);
			break;
	}
}

void ADirector_Level::AllSubPurposesComplete(const int64& uniqueContextID, const FPurposeAddress& addressOfPurpose)
{
	switch (addressOfPurpose.GetAddressLayer())
	{
		case (int)EPurposeLayer::Event:

			int indexOfStoredEvent = eventsActive.IsValidIndex(addressOfPurpose.GetAddressForLayer((int)EPurposeLayer::Event)) ? addressOfPurpose.GetAddressForLayer((int)EPurposeLayer::Event) : INDEX_NONE;
			//int indexOfStoredEvent = eventsActive.IndexOfByKey(addressOfGoal);

			if (indexOfStoredEvent == INDEX_NONE)
			{
				Global::LogError(GOAL, GetName(), "GoalComplete", TEXT("Address %s not found in eventsActive!"), *addressOfPurpose.GetAddressAsString());
				return;
			}

			if (HasData(UTrackedPurposes::StaticClass()))
			{
				const FContextData& activeEvent = eventsActive[indexOfStoredEvent];
				activeEvent.AdjustDataIfPossible(activeEvent.purpose.DataAdjustments(), EPurposeSelectionEvent::OnFinished, EVENT, "GoalComplete", this);
				//Global::Log(Informative, PurposeLog, *this, "GoalComplete", TEXT("Ending %s"), *Event->GetName());
				DataChunk<UTrackedPurposes>()->RemoveFromValue(indexOfStoredEvent);/// Then remove Event from Tracked Purposes
			}
			break;
		case (int)EPurposeLayer::Goal:
			break;
	}
}

TArray<FTaskLayer> ADirector_Level::GetTasksOfObjective(const FPurposeAddress& address)
{
	int eventAddress = address.GetAddressForLayer((int)EPurposeLayer::Event);

	if (!eventCacheForPurposeSystem.IsValidIndex(eventAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *address.GetAddressAsString());
		return TArray<FTaskLayer>();
	}
	
	TArray<FGoalLayer>& goals = eventCacheForPurposeSystem[eventAddress].goals;

	int goalAddress = address.GetAddressForLayer((int)EPurposeLayer::Goal);

	if (!goals.IsValidIndex(goalAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *address.GetAddressAsString());
		return TArray<FTaskLayer>();
	}

	TArray<FObjectiveLayer>& objectives = goals[goalAddress].objectives;

	int objectiveAddress = address.GetAddressForLayer((int)EPurposeLayer::Objective);

	if (!objectives.IsValidIndex(objectiveAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *address.GetAddressAsString());
		return TArray<FTaskLayer>();
	}

	TArray<FTaskLayer>& tasks = objectives[goalAddress].tasks;

	return tasks;
}

bool ADirector_Level::GetGoalLayer(const FPurposeAddress& address, FGoalLayer& outGoal)
{
	int eventAddress = address.GetAddressForLayer((int)EPurposeLayer::Event);

	if (!eventCacheForPurposeSystem.IsValidIndex(eventAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *address.GetAddressAsString());
		return false;
	}

	TArray<FGoalLayer>& goals = eventCacheForPurposeSystem[eventAddress].goals;

	int goalAddress = address.GetAddressForLayer((int)EPurposeLayer::Goal);

	if (!goals.IsValidIndex(goalAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *address.GetAddressAsString());
		return false;
	}

	outGoal = goals[goalAddress];
	return true;
}

bool ADirector_Level::GetEventLayer(const FPurposeAddress& address, FEventLayer& outEvent)
{
	int eventAddress = address.GetAddressForLayer((int)EPurposeLayer::Event);

	if (!eventCacheForPurposeSystem.IsValidIndex(eventAddress))
	{
		Global::LogError(EVENT, GetName(), "GetSubPurposesFor", TEXT("Address %s not found for Event"), *address.GetAddressAsString());
		return false;
	}

	outEvent = eventCacheForPurposeSystem[eventAddress];
	return true;
}

void ADirector_Level::EventAssetsLoaded()
{
	UAssetManager* assetManager = GEngine->AssetManager;
	if (!assetManager)
	{
		Global::LogError(MANAGEMENT, *this, "BeginPlay", TEXT("Asset manager invalid!"));
		return;
	}

	TSharedPtr<FStreamableHandle> callback = assetManager->LoadPrimaryAssetsWithType(UEventAsset::PrimaryAssetType());
	//TSharedPtr<FStreamableHandle> callback = assetManager->GetPrimaryAssetDataList(UEventAsset::PrimaryAssetType());

	if (!callback)
	{
		Global::LogError(PURPOSE, GetName(), "EventAssetsLoaded", TEXT("Callback to load EventAssets invalid!"));
		return;
	}
	TArray<UObject*> assets;
	callback->GetLoadedAssets(assets);

	for (UObject* asset : assets)
	{
		if (IsValid(asset) && asset->IsA<UEventAsset>())
		{
			eventCacheForPurposeSystem.Add(Cast<UEventAsset>(asset)->eventLayer);
		}
	}
}

void ADirector_Level::GoalComplete(const int64& uniqueContextID, const FPurposeAddress& addressOfGoal)
{
	for (TObjectPtr<AManager> manager : managers)
	{
		manager->EndGoalsOfEvent(uniqueContextID, addressOfGoal);/// Tell every Manager to Remove their Goal that belongs to inContext->Parent() Event
	}
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
			/// Refactor: Purpose Events Activity AI; Instead of seeking purpose assets through AssetManager, Activities should have purpose layers established on them with EditInlineNew
				/// This way we aren't creating Events and Goals that are irrelevant anywhere except that activity
				/// But there should be an option for utilizing a global purpose 
				/// Instead of putting the Event Asset throught purpose selection, simply add it to selected eventCacheForPurposeSystem
				/// Currently, because we are loading in purpose assets, we simply are telling the activity to load the assets
				/// Once loaded, the activity passes the Event back to the ADirector_Level for queuing in PurposeEvaluationThread
			FContextData activityData = ActorItr->Activity();
			activityData.AddSubject(ESubject::Instigator, this);

			activityData.addressOfPurpose = eventCacheForPurposeSystem.Add(ActorItr->eventForActivity);/// We both need to store the activity for future potential occurrences
			/// And we need to ensure the address of the activity is updated to match its index in the eventCacheForPurposeSystem cache
			
			ProvidePurposeToOwner(activityData);

			PurposeSystem::QueueNextPurposeLayer(activityData);
		}
	}
}

#pragma endregion

