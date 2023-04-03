// Copyright Jordan Cain. All Rights Reserved.


#include "Purpose/Manager.h"
#include "Purpose/DataChunks/TrackedPurposes.h"
#include "Purpose/Context/OccurrenceContext.h"
#include "Purpose/Context/GoalContext.h"
#include "Purpose/Context/ObjectiveContext.h"
#include "Purpose/Context/TaskContext.h"
#include "Purpose/Context/ReactionContext.h"
#include "Purpose/Assets/ReactionAsset.h"
#include "Purpose/Director_Level.h"
#include "Purpose/PurposeAbilityComponent.h"
#include "NavigationSystem.h"
#include "Purpose/DataChunks/ActorRole.h"
#include "Purpose/DataChunks/ActorLocation.h"
#include "Purpose/DataChunks/Mass.h"
#include "Purpose/DataChunks/Morale.h"
#include "PurposeAssetManager.h"
#include "Purpose/PurposeAsset.h"
#include "Purpose/PurposeAbilityComponent.h"
#include "Purpose/Abilities/GA_Action.h"
#include "GameInstance_Base.h"
#include "Purpose/DataChunks/EventRelevantToAction.h"
#include "Gameframework/Character.h"

#include "GameModes/LyraGameMode.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "GameModes/LyraExperienceManagerComponent.h"
#include "Development/LyraDeveloperSettings.h"
#include "GameFramework/PlayerController.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "AIController.h"
#include "AbilitySystem/Attributes/LyraHealthSet.h"
#include "AbilitySystem/Attributes/LyraCombatSet.h"
#include "Player/LyraPlayerBotController.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "Player/LyraPlayerController.h"

// Sets default values
AManager::AManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
	
	bReplicates = true;

}

// Called when the game starts or when spawned
void AManager::BeginPlay()
{
	Super::BeginPlay();
	/*//Global::Log(FULLTRACE, ManagementLog, *this, "BeginPlay(AManager)", TEXT("NetMode: %s. NetOwner: %s. Owner: %s. World->FirstPlayerController: %s."), 
		*Global::NetModeAsString(GetNetMode()), 
		IsValid(GetNetOwner()) ? *GetNetOwner()->GetName() : TEXT("Invalid"), 
		IsValid(GetOwner()) ? *GetOwner()->GetName() : TEXT("Invalid"), 
		IsValid(GetWorld()->GetFirstPlayerController()) ? *GetWorld()->GetFirstPlayerController()->GetName() : TEXT("Invalid"));*/

	if (const UGlobalManagementSettings* ManagementSettings = GetDefault<UGlobalManagementSettings>())
	{
		playerSightEQSCache = ManagementSettings->PlayerSightQuery.LoadSynchronous();
	}

	timeSinceLastEQS = GetWorld()->GetTime();
}

void AManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	/// Here we list the variables we want to replicate + a condition if wanted
	DOREPLIFETIME(AManager, data);
	DOREPLIFETIME(AManager, actors);
}

void AManager::BeginDestroy()
{
	Super::BeginDestroy();
	
}

// Called every frame
void AManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	PerformVisualEQS();
}

#pragma region Purpose

TScriptInterface<IPurposeManagementInterface> AManager::GetHeadOfPurposeManagment()
{
	return director->GetHeadOfPurposeManagment();
}

TScriptInterface<IPurposeManagementInterface> AManager::GetPurposeSuperior()
{
	return director;
}

TArray<FPurposeEvaluationThread*> AManager::GetBackgroundPurposeThreads()
{
	return GetHeadOfPurposeManagment()->GetBackgroundPurposeThreads();
}

TArray<TScriptInterface<IDataMapInterface>> AManager::GetCandidatesForSubPurposeSelection(const int PurposeLayerForUniqueSubjects)
{
	TArray<TScriptInterface<IDataMapInterface>> candidates;

	Global::Log(DATADEBUG, PURPOSE, *this, "GetCandidatesForSubPurposeSelection", TEXT("Seeking candidates for layer %s.")
		, *Global::EnumValueOnly<EPurposeLayer>(PurposeLayerForUniqueSubjects)
	);
	switch (PurposeLayerForUniqueSubjects)
	{
		case (int)EPurposeLayer::Objective:
			for (TObjectPtr<UPurposeAbilityComponent> candidate : ownedPurposeCandidates)
			{
				Global::Log(DATADEBUG, PURPOSE, *this, "GetCandidatesForSubPurposeSelection", TEXT("Providing %s as candidate to layer %s.")
					, *candidate->GetFullGroupName(false)
					, *Global::EnumValueOnly<EPurposeLayer>(EPurposeLayer::Objective)
				);
				candidates.Add(candidate);
			}
			break;
	}

	return candidates;
}

TArray<FSubjectMap> AManager::GetUniqueSubjectsRequiredForSubPurposeSelection(const int PurposeLayerForUniqueSubjects, const FContextData& parentContext, TScriptInterface<IDataMapInterface> candidate, FPurposeAddress addressOfSubPurpose)
{
	TArray<FSubjectMap> uniqueSubjects;

	if (!IsValid(candidate.GetObject()))
	{
		Global::LogError(PURPOSE, *this, "GetUniqueSubjectsRequiredForSubPurposeSelection", TEXT("Candidate for layer %s is invalid!.")
			, *Global::EnumValueOnly<EPurposeLayer>(PurposeLayerForUniqueSubjects)
		);
		return uniqueSubjects;
	}

	switch (PurposeLayerForUniqueSubjects)
	{
		case (int)EPurposeLayer::Objective:

			FGoalLayer goal;
			if (!director->GetGoalLayer(addressOfSubPurpose, goal))
			{
				Global::LogError(OBJECTIVE, *this, "GetUniqueSubjectsRequiredForSubPurposeSelection", TEXT("Could not get goal for address %s, layer %s")
					, *addressOfSubPurpose.GetAddressAsString()
					, *Global::EnumValueOnly<EPurposeLayer>(PurposeLayerForUniqueSubjects)
				);
				return uniqueSubjects;
			}

			if (!goal.objectives.IsValidIndex(addressOfSubPurpose.GetAddressOfThisPurpose()))
			{
				Global::LogError(OBJECTIVE, *this, "GetUniqueSubjectsRequiredForSubPurposeSelection", TEXT("Could not get objective of goal %s for address %s, layer %s")
					, *goal.descriptionOfPurpose
					, *addressOfSubPurpose.GetAddressAsString()
					, *Global::EnumValueOnly<EPurposeLayer>(PurposeLayerForUniqueSubjects)
				);
				return uniqueSubjects;
			}

			/// We are retrieving all potential subjects for a specific purpose
				/// In this case it's for an Objective
			const FObjectiveLayer& objective = goal.objectives[addressOfSubPurpose.GetAddressOfThisPurpose()];

			/// So we get every potential target for the objective
			TArray<TScriptInterface<IDataMapInterface>> targets = PotentialObjectiveTargets(Cast<AActor>(candidate.GetObject()), parentContext, objective.targetingParams);
			for (TScriptInterface<IDataMapInterface> target : targets)
			{
				/// And combine them with the candidate to form a UniqueSubject entry
				FSubjectMap subjectMap;
				subjectMap.subjects.Add(ESubject::Candidate, candidate);
				subjectMap.subjects.Add(ESubject::ObjectiveTarget, target);
				Global::LogError(PURPOSE, *this, "GetUniqueSubjectsRequiredForSubPurposeSelection", TEXT("Adding candidate %s with target %s for layer %s.")
					, *candidate.GetObject()->GetName()
					, *target.GetObject()->GetName()
					, *Global::EnumValueOnly<EPurposeLayer>(PurposeLayerForUniqueSubjects)
				);
				uniqueSubjects.Add(subjectMap);
			}
			/// At purpose evaluation, it will utilize each UniqueSubject entry established here to choose the best combination
			break;
	}

	return uniqueSubjects;
}

bool AManager::ProvidePurposeToOwner(const FContextData& purposeToStore)
{
	switch (purposeToStore.addressOfPurpose.GetAddressLayer())
	{
		case (int)EPurposeLayer::Goal:
			if (!DataChunk<UTrackedPurposes>()->Value().Contains(purposeToStore))/// Because there may be a callback to this method for loading Goals
			{
				DataChunk<UTrackedPurposes>()->AddToValue(purposeToStore);///Ensure that selected context is tracked
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
	return false;
}

TArray<FPurpose> AManager::GetEventAssets()
{
	return GetHeadOfPurposeManagment()->GetEventAssets();
}

TArray<FPurpose> AManager::GetSubPurposesFor(FPurposeAddress address)
{
	return GetHeadOfPurposeManagment()->GetSubPurposesFor(address);
}

void AManager::PurposeReOccurrence(const FPurposeAddress addressOfPurpose, const int64 uniqueIDofActivePurpose)
{
}

FContextData& AManager::GetStoredPurpose(const int64 uniqueIdentifierOfContextTree, const FPurposeAddress& fullAddress, const int layerToRetrieveFor)
{
	switch (layerToRetrieveFor)
	{
		case (int)EPurposeLayer::Goal:

			for (FContextData& context : DataChunk<UTrackedPurposes>()->ValueNonConst())
			{
				if (context.GetContextID() == uniqueIdentifierOfContextTree && context.addressOfPurpose.GetAddressForLayer(layerToRetrieveFor) == fullAddress.GetAddressForLayer(layerToRetrieveFor))
				{
					return context;
				}
			}
			break;
	}
	return GetPurposeSuperior()->GetStoredPurpose(uniqueIdentifierOfContextTree, fullAddress, layerToRetrieveFor);
}

void AManager::ReevaluateObjectivesForAllCandidates(const FPurposeAddress& addressOfPurpose, const int64& uniqueIDofActivePurpose)
{
	if (!HasData(UTrackedPurposes::StaticClass()))
	{
		Global::LogError(MANAGEMENT, *this, "ReevaluateObjectivesForAllCandidates", TEXT("Manager has no Tracked Purposes."));
		return;
	}
	for (const FContextData& goal : DataChunk<UTrackedPurposes>()->Value())
	{
		if (goal.GetContextID() != uniqueIDofActivePurpose)
		{
			continue;
		}

		Global::Log(DATAESSENTIAL, GOAL, *this, "ReevaluateObjectiveForAllCandidates", TEXT("Reevaluating Objectives of %s"), *goal.GetName());
		PurposeSystem::QueueNextPurposeLayer(goal);
	}
}

void AManager::EndGoalsOfEvent(const int64& uniqueContextID, const FPurposeAddress& eventAddress)
{
	if (HasData(UTrackedPurposes::StaticClass()))
	{
		int addressOfEvent = eventAddress.GetAddressForLayer((int)EPurposeLayer::Event);
		for (int i = DataChunk<UTrackedPurposes>()->Value().Num(); i > -1; --i )
		{
			const FContextData& goal = DataChunk<UTrackedPurposes>()->Value()[i];

			if (uniqueContextID == goal.GetContextID() && eventAddress == goal.addressOfPurpose.GetAddressForLayer((int)EPurposeLayer::Event))
			{
				goal.AdjustDataIfPossible(goal.purpose.DataAdjustments(), EPurposeSelectionEvent::OnFinished, GOAL, "EndTrackedGoals", this);
				//DataChunk<UTrackedPurposes>()->RemoveFromValue(goal);
				DataChunk<UTrackedPurposes>()->RemoveFromValue(i);
			}
		}

		/// Now check every candidate, and if they have an Objective that falls under a removed Goal, tell them to get new 
		for (TObjectPtr<UPurposeAbilityComponent> candidate : ownedPurposeCandidates)
		{
			if (!candidate->HasCurrentObjective())
			{
				Global::LogError(OBJECTIVE, *this, "EndTrackedGoals", TEXT("Current Objective of %s is invalid. Should we end Abilities?"), *candidate->GetOwner()->GetName());
				continue;
			}
			
			if (candidate->CurrentObjective().addressOfPurpose.GetAddressForLayer((int)EPurposeLayer::Event) == addressOfEvent)/// Check if the actors objective belongs to a Goal being Removed
			{
				Global::Log(DATADEBUG, OBJECTIVE, *this, "EndTrackedGoals", TEXT("Ending %s for %s"), *candidate->CurrentObjective().GetName(), *candidate->GetOwner()->GetName());
				///Actor needs to drop current Objective + Abilities without reporting and get a new Objective
				candidate->EndCurrentObjective();
				candidate->SelectNewObjectiveFromExistingGoals();/// Retrieve a new Objective from existing Goals
			}
		}
	}
	else
	{
		//Global::Log(Debug, ManagementLog, *this, "ProvideActorWithNewObjective", TEXT("Manager has no Tracked Purposes."));
	}
}

TArray<TScriptInterface<IDataMapInterface>> AManager::PotentialObjectiveTargets(TObjectPtr<AActor> source, const FContextData& inGoal, FTargetingParameters targetingParams)
{
	if (IsValid(source.Get()) && IsValid(source->GetWorld()))
	{
		TArray<TScriptInterface<IDataMapInterface>> outDataMaps;/// Selected targets
	
		if (inGoal.ContextIsValid() && inGoal.HasSubject(ESubject::EventTarget))
		{
			outDataMaps.Add(inGoal.DataMapInterfaceForSubject(ESubject::EventTarget));
		}

		if (!targetingParams.targetingQuery)
		{
			Global::Log(DATADEBUG, OBJECTIVE, *this, "PotentialObjectiveTargets", TEXT("Targeting query invalid under goal %s!"), *inGoal.GetPurposeChainName());
			return outDataMaps;
		}

		UEnvQueryManager* EnvQueryManager = UEnvQueryManager::GetCurrent(GetWorld());
		if (EnvQueryManager == NULL)
		{
			UE_LOG(LogEQS, Warning, TEXT("Missing EQS manager!"));
			return outDataMaps;
		}
		FEnvQueryRequest QueryRequest(targetingParams.targetingQuery, this);

		TSharedPtr<FEnvQueryResult> result = EnvQueryManager->RunInstantQuery(QueryRequest, EEnvQueryRunMode::AllMatching);

		if (!result.IsValid() && result->Items.Num() <= 0) { return outDataMaps; }

		for (int i = 0; i < result->Items.Num(); ++i)
		{
			AActor* actor = result->GetItemAsActor(i);
			if (!IsValid(actor))
			{
				continue;
			}

			Global::Log(DATATRIVIAL, OBJECTIVE, *this, "PotentialObjectiveTargets", TEXT("Hit Result: %s."), *actor->GetName());

			UPurposeAbilityComponent* purposeComp = actor->FindComponentByClass<UPurposeAbilityComponent>();

			if (!purposeComp) { purposeComp = actor->GetOwner()->FindComponentByClass<UPurposeAbilityComponent>(); }
			else if (!purposeComp) { purposeComp = actor->IsA<APawn>() ? Cast<APawn>(actor)->GetController()->FindComponentByClass<UPurposeAbilityComponent>() : nullptr; }
			else if (!purposeComp) { purposeComp = actor->GetOwner()->IsA<ALyraPlayerController>() ? Cast<ALyraPlayerController>(actor->GetOwner())->PlayerState->FindComponentByClass<UPurposeAbilityComponent>() : nullptr; }

			if (!purposeComp->IsValidLowLevel())
			{
				Global::Log(DATADEBUG, OBJECTIVE, *this, "PotentialObjectiveTargets", TEXT("Could not find purpose component of target: %s.")
					, *actor->GetName()
				);
				continue;
			}

			outDataMaps.Add(purposeComp);
			Global::Log(DATADEBUG, OBJECTIVE, *this, "PotentialObjectiveTargets", TEXT("Target: %s.")
				, *purposeComp->GetName()
			);
		}

		return outDataMaps;
	}
	else
	{
		Global::LogError(OBJECTIVE, *this, "PotentialObjectiveTargets", TEXT("Source or World are nullptr!"));
	}

	return TArray< TScriptInterface<IDataMapInterface> >();
}

bool AManager::TargetHasGroupRelationship(TObjectPtr<UPurposeAbilityComponent> target, const FContextData& inGoal, EGroupRelationship groupRelationship)
{
	bool result = false;

	if (groupRelationship != EGroupRelationship::None && IsValid(target))
	{
		FContextData& eventContext = GetStoredPurpose(inGoal.GetContextID(), inGoal.addressOfPurpose, (int)EPurposeLayer::Event);
		FEventLayer eventLayer;
		
		if (!IsValid(director))
		{
			return false;
		}

		if (!director->GetEventLayer(inGoal.addressOfPurpose, eventLayer))
		{
			return false;
		}

		EEventGroup sourceGroup = eventLayer.GroupingForGoal(inGoal.addressOfPurpose);/// By establishing the source group of the Event
		
		////Global::Log(Debug, PurposeLog, *this, "TargetHasGroupRelationship", TEXT("Event: %s. Number of targetManager->Goals: %d"), *Event->GetName(), target->Manager()->DataChunk<UTrackedPurposes>()->Value().Num());
		for (auto goal : target->Manager()->DataChunk<UTrackedPurposes>()->Value())
		{
			////Global::Log(Debug, PurposeLog, *this, "TargetHasGroupRelationship", TEXT("Goal: %s."), *goal->GetName());

			EEventGroup targetGroup = eventLayer.GroupingForGoal(goal.addressOfPurpose);/// Then finding which group the target belongs to, if any
			if (targetGroup != EEventGroup::None)
			{
				if (targetGroup == sourceGroup && groupRelationship == EGroupRelationship::Allies) { return true; }/// If the actors belong to the same Goal they are allies

				EGroupRelationship relation = eventLayer.RelationshipBetweenGroups(sourceGroup, targetGroup);/// We can determine the relationship between the groups
				result = relation == groupRelationship;/// And whether it matches the input relationship
			}

			if (result) { return true; }
		}
	}

	return false;
}


#pragma endregion


