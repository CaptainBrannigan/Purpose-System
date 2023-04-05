// Copyright Jordan Cain. All Rights Reserved.



#include "Purpose/PurposeAbilityComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"

#include "AbilitySystemLog.h"
#include "AttributeSet.h"
#include "GameplayPrediction.h"
#include "GameplayEffectTypes.h"
#include "GameplayAbilitySpec.h"
#include "UObject/UObjectHash.h"
#include "GameFramework/PlayerController.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystemStats.h"
#include "AbilitySystemGlobals.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTargetActor.h"
#include "TickableAttributeSetInterface.h"
#include "Purpose/Abilities/GA_PurposeBase.h"
#include "Purpose/Assets/EventAsset.h"
#include "Purpose/DataChunks/ActorAction.h"

UPurposeAbilityComponent::UPurposeAbilityComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Global::Log(CALLTRACEESSENTIAL, PURPOSE, *this, "UPurposeAbilityComponent", TEXT("Owner: %s"), GetOwner() ?  *GetOwner()->GetName() : TEXT("Invalid"));
}

void UPurposeAbilityComponent::InitializePurposeSystem(TObjectPtr<AManager> inManager)
{
	manager = inManager;

	AbilityActivatedCallbacks.AddUObject(this, &UPurposeAbilityComponent::ActionPerformed);/// Primarily set up since player input goes straight to the ability system
	/// But now all behavior occurrences are routed through BehaviorPerformed
}

void UPurposeAbilityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	/// Here we list the variables we want to replicate + a condition if wanted
	DOREPLIFETIME(UPurposeAbilityComponent, data);
}

void UPurposeAbilityComponent::PerformAbility(const FContextData& inContext, TSubclassOf<UGA_PurposeBase> abilityClass)
{
	////Global::Log(Debug, AbilityLog, "UPurposeAbilityComponent", "PerformAbility", TEXT("New ability of %s"), *inContext->GetName());
	if (abilityClass)
	{
		FGameplayAbilitySpecHandle handle = GiveAbility(FGameplayAbilitySpec(abilityClass, 1, INDEX_NONE, this));

		FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(handle);
		if(Spec)
		{
			Cast<UGA_PurposeBase>(Spec->Ability)->Initialize(inContext);/// Abilities need to have their context attached for the Purpose System

			SetRemoveAbilityOnEnd(Spec->Handle);/// Our abilities are set as a one time use, as they are selected via purpose system individually and contextually, not like in a set state machine or behavior tree

			TryActivateAbility(handle);
		}
		else
		{
			ABILITY_LOG(Warning, TEXT("PerformAbility(TSubclassOf<>) found invalid Handle"));
			Global::LogError(TASK, "UPurposeAbilityComponent", "PerformAbility", TEXT("Owner: , Class: %s, Context: %s."),
				//IsValid(GetOwner()) ? *GetOwner()->GetName() : TEXT("Invalid"),
				abilityClass.Get() ? *abilityClass->GetName() : TEXT("Invalid")
				, *inContext.GetName()
			);
		}
	}
	else
	{
		Global::LogError(TASK, "UPurposeAbilityComponent", "PerformAbility", TEXT("Owner: , Class: %s, Context: %s."),
			//IsValid(GetOwner()) ? *GetOwner()->GetName() : TEXT("Invalid"),
			abilityClass.Get() ? *abilityClass->GetName() : TEXT("Invalid")
			, *inContext.GetName()
		);
	}
}

void UPurposeAbilityComponent::PerformAbility(const FContextData& inContext, const TObjectPtr<UGA_PurposeBase> ability, uint8 priority)
{
	if (!IsValid(ability))
	{
		Global::LogError(TASK, "UPurposeAbilityComponent", "PerformAbility", TEXT("Owner: %s, Ability: %s, Context: %s."),
			IsValid(GetOwner()) ? *GetOwner()->GetName() : TEXT("Invalid"),
			IsValid(ability) ? *ability->GetName() : TEXT("Invalid")
			, *inContext.GetName()
		);
		return;
	}
	FGameplayAbilitySpec Spec = GiveAbilityDuplicate(FGameplayAbilitySpec(ability, 1, INDEX_NONE, this));

	FGameplayAbilitySpecHandle handle = Spec.Handle;
	if (Spec.Ability)
	{
		Cast<UGA_PurposeBase>(Spec.Ability)->Initialize(inContext);/// Abilities need to have their context attached for the Purpose System

		//SetRemoveAbilityOnEnd(Spec->Handle);/// Our abilities are set as a one time use, as they are selected via purpose system individually and contextually, not like in a set state machine or behavior tree
		/// I believe the above version was clearing the ability prematurely, otherwise it does this anyways
		///Spec->RemoveAfterActivation = true;/// Our abilities are set as a one time use, as they are selected via purpose system individually and contextually, not like in a set state machine or behavior tree

		TryActivateAbility(handle);
	}
	else
	{
		ABILITY_LOG(Warning, TEXT("PerformAbility(TSubclassOf<>) found invalid Handle"));

		Global::LogError(TASK, "UPurposeAbilityComponent", "PerformAbility", TEXT("Owner: %s, Ability: %s, Context: %s."),
			IsValid(GetOwner()) ? *GetOwner()->GetName() : TEXT("Invalid"),
			IsValid(ability) ? *ability->GetName() : TEXT("Invalid")
			, *inContext.GetName()
		);
	}
}

void UPurposeAbilityComponent::PerformAbility(UGA_PurposeBase& ability)
{
	FGameplayAbilitySpec Spec = GiveAbilityDuplicate(FGameplayAbilitySpec(&ability, 1, INDEX_NONE, this));

	FGameplayAbilitySpecHandle handle = Spec.Handle;

	bool activated =TryActivateAbility(handle);
	
	if(!activated)
	{
		ABILITY_LOG(Warning, TEXT("PerformAbility(TSubclassOf<>) found invalid Handle"));

		Global::LogError(TASK, "UPurposeAbilityComponent", "PerformAbility", TEXT("Owner: %s, Ability: %s. Failed to activate reference to ability!")
			, IsValid(GetOwner()) ? *GetOwner()->GetName() : TEXT("Invalid")
			, *ability.GetName()
		);
	}
}

void UPurposeAbilityComponent::EndAbilitiesOf(TSubclassOf<UGA_PurposeBase> inClass, const EAbilityPurposeFeedback reasonAbilityEnded, TObjectPtr<UGA_PurposeBase> abilityToExclude)
{
	/** Cancels the specified ability CDO. */
	///void CancelAbility(UGameplayAbility * Ability);

	/** Cancels the ability indicated by passed in spec handle. If handle is not found among reactivated abilities nothing happens. */
	///void CancelAbilityHandle(const FGameplayAbilitySpecHandle & AbilityHandle);

	/** Cancel all abilities with the specified tags. Will not cancel the Ignore instance */
	///void CancelAbilities(const FGameplayTagContainer * WithTags = nullptr, const FGameplayTagContainer * WithoutTags = nullptr, UGameplayAbility * Ignore = nullptr);

	/** Cancels all abilities regardless of tags. Will not cancel the ignore instance */
	///void CancelAllAbilities(UGameplayAbility * Ignore = nullptr);
	TArray<FGameplayAbilitySpec> abilitySpecs = ActivatableAbilities.Items;
	for (FGameplayAbilitySpec& Spec : abilitySpecs)
	{
		///if (ability->IsA(inClass)) In this case we may actually want to be specific, and not affect derived classes
		/// Design: Ability Purpose Feedback; In the future, we may consider allowing this to use IsA, and affect derived ability classes
		if (Spec.Ability->GetClass()->IsChildOf(inClass) && Spec.Ability != abilityToExclude)
		{
			Global::Log(DATADEBUG, TASK, *this, "EndAbilitiesOf", TEXT("Ending ability: %s for %s"), *Spec.Ability->GetName(), *Global::EnumValueOnly<EAbilityPurposeFeedback>(reasonAbilityEnded));
			Cast<UGA_PurposeBase>(Spec.Ability)->AbilityFinished(reasonAbilityEnded);

			/// Crucial that ClearAbility is called!
			/// Gas works by keeping abilities for recall, whereas my system was built off GameplayTasks, which are destroyed an unavailable on finish
			ClearAbility(Spec.Handle);
		}
	}
}

void UPurposeAbilityComponent::EndAbilitiesOf(TSubclassOf<UGA_PurposeBase> inClass, const EAbilityPurposeFeedback reasonAbilityEnded, TArray<TObjectPtr<UGA_PurposeBase>> abilitiesToExclude)
{
	TArray<FGameplayAbilitySpec> abilitySpecs = ActivatableAbilities.Items;
	for (FGameplayAbilitySpec& Spec : abilitySpecs)
	{
		///if (ability->IsA(inClass)) In this case we may actually want to be specific, and not affect derived classes
		/// Design: Ability Purpose Feedback; In the future, we may consider allowing this to use IsA, and affect derived ability classes
		if (Spec.IsActive() && Spec.Ability->GetClass()->IsChildOf(inClass) && !abilitiesToExclude.Contains(Spec.Ability))
		{
			Global::Log(DATADEBUG, TASK, *this, "EndAbilitiesOf", TEXT("Ending ability: %s for %s"), *Spec.Ability->GetName(), *Global::EnumValueOnly<EAbilityPurposeFeedback>(reasonAbilityEnded));
			Cast<UGA_PurposeBase>(Spec.Ability)->AbilityFinished(reasonAbilityEnded);

			/// Crucial that ClearAbility is called!
			/// Gas works by keeping abilities for recall, whereas my system was built off GameplayTasks, which are destroyed an unavailable on finish
			ClearAbility(Spec.Handle);
		}
	}
}

void UPurposeAbilityComponent::EndAbilitiesOf(TSubclassOf<UGA_PurposeBase> inClass, const EAbilityPurposeFeedback reasonAbilityEnded, TSubclassOf<UGA_PurposeBase> abilitiesToExclude)
{
	TArray<FGameplayAbilitySpec> abilitySpecs = ActivatableAbilities.Items;
	for (FGameplayAbilitySpec& Spec : abilitySpecs)
	{
		///if (ability->IsA(inClass)) In this case we may actually want to be specific, and not affect derived classes
		/// Design: Ability Purpose Feedback; In the future, we may consider allowing this to use IsA, and affect derived ability classes
		if (Spec.IsActive() && Spec.Ability->GetClass()->IsChildOf(inClass) && Spec.Ability->GetClass() != abilitiesToExclude)
		{
			Global::Log(DATADEBUG, TASK, *this, "EndAbilitiesOf", TEXT("Ending ability: %s for %s"), *Spec.Ability->GetName(), *Global::EnumValueOnly<EAbilityPurposeFeedback>(reasonAbilityEnded));
			Cast<UGA_PurposeBase>(Spec.Ability)->AbilityFinished(reasonAbilityEnded);

			/// Crucial that ClearAbility is called!
			/// Gas works by keeping abilities for recall, whereas my system was built off GameplayTasks, which are destroyed an unavailable on finish
			ClearAbility(Spec.Handle);
		}
	}
}

FGameplayAbilitySpec UPurposeAbilityComponent::GiveAbilityDuplicate(FGameplayAbilitySpec Spec)
{
	if (!IsValid(Spec.Ability))
	{
		ABILITY_LOG(Error, TEXT("GiveAbility called with an invalid Ability Class."));

		return FGameplayAbilitySpec();
	}

	if (!IsOwnerActorAuthoritative())
	{
		ABILITY_LOG(Error, TEXT("GiveAbility called on ability %s on the client, not allowed!"), *Spec.Ability->GetName());

		return FGameplayAbilitySpec();
	}

	// If locked, add to pending list. The Spec.Handle is not regenerated when we receive, so returning this is ok.
	if (AbilityScopeLockCount > 0)
	{
		AbilityPendingAdds.Add(Spec);
		return Spec;
	}

	ABILITYLIST_SCOPE_LOCK();
	FGameplayAbilitySpec& OwnedSpec = ActivatableAbilities.Items[ActivatableAbilities.Items.Add(Spec)];

	if (OwnedSpec.Ability->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::InstancedPerActor)
	{
		// Create the instance at creation time
		///CreateNewInstanceOfAbility(OwnedSpec, Spec.Ability);
		///But instead of utilizing provided method, we want to instead duplicate the ability so that we can utilize any properties set in ability asset ability came from

		AActor* Owner = GetOwner();
		check(Owner);

		UGameplayAbility* AbilityInstance = DuplicateObject<UGameplayAbility>(OwnedSpec.Ability, Owner);
		check(AbilityInstance);

		OwnedSpec.Ability = AbilityInstance;/// We have set the specs ability to the duplicate
		/// Else it will attempt to modify the Task Asset ability!
		/// This is actually supposed to be the CDO of the ability
		/// In the future they may make it const, so we may need a better method of giving the ability from the purpose asset

		// Add it to one of our instance lists so that it doesn't GC.
		if (AbilityInstance->GetReplicationPolicy() != EGameplayAbilityReplicationPolicy::ReplicateNo)
		{
			OwnedSpec.ReplicatedInstances.Add(AbilityInstance);
			AddReplicatedInstancedAbility(AbilityInstance);
		}
		else
		{
			OwnedSpec.NonReplicatedInstances.Add(AbilityInstance);
		}
	}

	OnGiveAbility(OwnedSpec);
	MarkAbilitySpecDirty(OwnedSpec, true);

	return OwnedSpec;
}

#pragma region Purpose Management Interface

TScriptInterface<IPurposeManagementInterface> UPurposeAbilityComponent::GetHeadOfPurposeManagment()
{
	return Cast<IPurposeManagementInterface>(manager)->GetHeadOfPurposeManagment();
}

TScriptInterface<IPurposeManagementInterface> UPurposeAbilityComponent::GetPurposeSuperior()
{
	return TScriptInterface<IPurposeManagementInterface>(manager);
}

TArray<FPurposeEvaluationThread*> UPurposeAbilityComponent::GetBackgroundPurposeThreads()
{
	return GetHeadOfPurposeManagment()->GetBackgroundPurposeThreads();
}

TArray<TScriptInterface<IDataMapInterface>> UPurposeAbilityComponent::GetCandidatesForSubPurposeSelection(const int PurposeLayerForUniqueSubjects)
{
	TArray<TScriptInterface<IDataMapInterface>> candidates;

	Global::Log(DATADEBUG, PURPOSE, *this, "GetCandidatesForSubPurposeSelection", TEXT("Seeking candidates for layer %s.")
		, *Global::EnumValueOnly<EPurposeLayer>(PurposeLayerForUniqueSubjects)
	);
	switch (PurposeLayerForUniqueSubjects)
	{
		case (int)EPurposeLayer::Behavior:
			candidates.Add(this);
			break;
	}

	return candidates;
}

TArray<FSubjectMap> UPurposeAbilityComponent::GetUniqueSubjectsRequiredForSubPurposeSelection(const int PurposeLayerForUniqueSubjects, const FContextData& parentContext, TScriptInterface<IDataMapInterface> candidate, FPurposeAddress addressOfSubPurpose)
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
		case (int)EPurposeLayer::Behavior:
			FSubjectMap subjectMap;
			subjectMap.subjects.Add(ESubject::Candidate, candidate);
			Global::LogError(PURPOSE, *this, "GetUniqueSubjectsRequiredForSubPurposeSelection", TEXT("Adding candidate %s for layer %s.")
				, *candidate.GetObject()->GetName()
				, *Global::EnumValueOnly<EPurposeLayer>(PurposeLayerForUniqueSubjects)
			);
			uniqueSubjects.Add(subjectMap);
			/// At purpose evaluation, it will utilize each UniqueSubject entry established here to choose the best combination
			break;
	}

	return uniqueSubjects;
}

bool UPurposeAbilityComponent::ProvidePurposeToOwner(const FContextData& purposeToStore)
{
	switch (purposeToStore.addressOfPurpose.GetAddressLayer())
	{
		case (int)EPurposeLayer::Objective:
			if (CurrentObjective().ContextIsValid())
			{
				if (CurrentObjective().cachedScoreOfPurpose >= purposeToStore.cachedScoreOfPurpose)/// Workaround for required data causing score to be 0 for the candidate who is already performing an Objective
				{
					Global::Log(DATADEBUG, OBJECTIVE, *this, "ObjectiveFoundForActor", TEXT("Objective %s already active or score %f is lower than current score %f for %s.")
						, *purposeToStore.GetPurposeChainName()
						, purposeToStore.cachedScoreOfPurpose
						, CurrentObjective().cachedScoreOfPurpose
						, *GetFullGroupName(false)
					);
					return false;
				}
				else
				{
					Global::Log(DATATRIVIAL, OBJECTIVE, *this, "ObjectiveFoundForActor", TEXT("Current Objective %s with ScoreCache %f. Incoming score %f for %s.")
						, *purposeToStore.GetPurposeChainName()
						, purposeToStore.cachedScoreOfPurpose
						, CurrentObjective().cachedScoreOfPurpose
						, *GetFullGroupName(false)
					);
				}
			}

			Global::Log(DATAESSENTIAL, OBJECTIVE, *this, "ProvidePurposeToOwner", TEXT("Providing %s with Objective: %s.")
				, *GetFullGroupName(false)
				, *purposeToStore.GetPurposeChainName()
			);

			EndCurrentObjective();/// Explicitly pass the previous Objective, to ensure avoiding any chance of ending new Objective

			SetCurrentObjective(purposeToStore);///Set the current objective of the actor
			return true;
		break;
		case (int)EPurposeLayer::Behavior:

			TObjectPtr<UBehavior_AI> behavior = GetBehaviorAtAddress(purposeToStore.addressOfPurpose);

			PerformAbility(purposeToStore, behavior, 1);
			return true;
		break;
	}
}

TArray<FPurpose> UPurposeAbilityComponent::GetEventAssets()
{
	return GetHeadOfPurposeManagment()->GetEventAssets();
}

TArray<FPurpose> UPurposeAbilityComponent::GetSubPurposesFor(FPurposeAddress address)
{
	return GetHeadOfPurposeManagment()->GetSubPurposesFor(address);
}

void UPurposeAbilityComponent::PurposeReOccurrence(const FPurposeAddress addressOfPurpose, const int64 uniqueIDofActivePurpose)
{
}

FContextData& UPurposeAbilityComponent::GetStoredPurpose(const int64 uniqueIdentifierOfContextTree, const FPurposeAddress& fullAddress, const int layerToRetrieveFor)
{
	switch (layerToRetrieveFor)
	{
		case (int)EPurposeLayer::Objective:

			if (CurrentObjective().ContextIsValid() && CurrentObjective().GetContextID() == uniqueIdentifierOfContextTree && CurrentObjective().addressOfPurpose.GetAddressForLayer(layerToRetrieveFor) == fullAddress.GetAddressForLayer(layerToRetrieveFor))
			{
				return CurrentObjective();
			}
			break;
	}
	return GetPurposeSuperior()->GetStoredPurpose(uniqueIdentifierOfContextTree, fullAddress, layerToRetrieveFor);
}

bool UPurposeAbilityComponent::DoesPurposeAlreadyExist(const FContextData& primary, const FSubjectMap& secondarySubjects, const TArray<FDataMapEntry>& secondaryContext, const FPurposeAddress optionalAddress)
{
	return primary.Subject(ESubject::Candidate) == (secondarySubjects.subjects.Contains(ESubject::Candidate) ? secondarySubjects.subjects[ESubject::Candidate].GetObject() : nullptr)
		&& primary.Subject(ESubject::ObjectiveTarget) == (secondarySubjects.subjects.Contains(ESubject::ObjectiveTarget) ? secondarySubjects.subjects[ESubject::ObjectiveTarget].GetObject() : nullptr)
		&& primary.addressOfPurpose == optionalAddress;
}

#pragma endregion

#pragma region Purpose

void UPurposeAbilityComponent::EndCurrentObjective()
{
	if (!CurrentObjective().ContextIsValid())
	{
		Global::Log(DATAESSENTIAL, OBJECTIVE, *this, "{", TEXT("currentObjective for %s invalid!")
			, *GetFullGroupName(false)
		);
		return;/// If the Objective is invalid, it likely means it comes from a nullptr of FActorData::currentObjective
		/// Which is nullified when currentObjective begins clean up
		/// Because AbilityFinished will be called for every Ability in the Objective, which will tell the Objective to clean up
	}

	Global::Log(DATAESSENTIAL, OBJECTIVE, *this, "EndObjective", TEXT("Ending Abilities of currentObjective %s for %s.")
		, *CurrentObjective().GetName()
		, *GetFullGroupName(false)
	);

	/// Crucial that on finished data adjustment is made if needed
	CurrentObjective().AdjustDataIfPossible(CurrentObjective().purpose.DataAdjustments(), EPurposeSelectionEvent::OnFinished, OBJECTIVE, "EndObjective", this);

	/// Decrease the address layer of the CurrentObjective in order to retrieve the Goal layer
	FContextData& parentContext = CurrentObjective().purposeOwner->GetStoredPurpose(CurrentObjective().GetContextID(), CurrentObjective().addressOfPurpose, CurrentObjective().addressOfPurpose.GetAddressLayer() - 1);
	if (parentContext.ContextIsValid())
	{
		/// As the Objective is finished, ensure participation is updated
		if (!parentContext.DecreaseSubPurposeParticipants(CurrentObjective().addressOfPurpose))
		{
			Global::Log(DATADEBUG, PURPOSE, "PurposeSystem", "PurposeSelected", TEXT("Participation of %s not decreased!")
				, *CurrentObjective().GetPurposeChainName()
			);
		}
	}

	/// tthis could be solved by going through the management intterface
	TArray<TObjectPtr<UBehavior_AI>> behaviors = GetBehaviorsFromParent(CurrentObjective().addressOfPurpose);
	for (TObjectPtr<UBehavior_AI>& behavior : behaviors)
	{
		if (!behavior)
		{
			continue;
		}
		Global::Log(DATADEBUG, OBJECTIVE, *this, "EndObjective", TEXT("Ending %s for %s.")
			, *behavior->GetName()
			, *GetName()
		);

		EndAbilitiesOf(behavior->GetClass(), EAbilityPurposeFeedback::InterruptedForNewObjective);/// This could lead to a recursed call to AbilityFinished() without Ability()->FeedbackState()
		/// Bugwatch: Abilities Purpose Feedback; If abilityFeedbackState is not set in AbilityFinished() this could lead to an infinite loop of AbilityFinished() -> EndObjective() -> AbilityFinished()
	}

	SetCurrentObjective(FContextData());/// Ensure the previous objective is no longer referenced
}

void UPurposeAbilityComponent::ActionPerformed(UGameplayAbility* Ability)
{
	if (IsValid(Ability))
	{
		Global::Log(DATATRIVIAL, BEHAVIOR, *this, "ActionPerformed", TEXT("%s is performing %s.")
			, *Ability->GetName()
		);

		FSubjectMap subjectMap;
		TArray<FDataMapEntry> contextData;

		if (Ability->IsA<UGA_PurposeBase>())
		{
			subjectMap = Cast<UGA_PurposeBase>(Ability)->context.subjectMap;
			contextData = Cast<UGA_PurposeBase>(Ability)->context.contextData;
		}
		else
		{
			subjectMap.subjects.Add(ESubject::Instigator, this);
			contextData.Add(FDataMapEntry(&NewObject<UActorAction>(this)->Initialize(Ability->GetClass())));
		}

		PurposeSystem::Occurrence(subjectMap, contextData, this);
	}
	else
	{
		Global::Log(DATADEBUG, BEHAVIOR, *this, "ActionPerformed", TEXT("Ability invalid!"));
	}
}


void UPurposeAbilityComponent::AbilityHasFinished(const FContextData& inContext, const EAbilityPurposeFeedback reasonAbilityEnded)
{
	inContext.AdjustDataIfPossible(inContext.purpose.DataAdjustments(), EPurposeSelectionEvent::OnFinished, TASK, "ActorFinishedAbility", this);

	/// Either the Ability was ended because a new Objective was selected over previous, and previous Objective's Abilities are being ended
	/// Or we received a new Ability to perform which has OverlappingResources and is taking precedence over this Ability
	bool shouldNotSeekNewPurpose =
		reasonAbilityEnded == EAbilityPurposeFeedback::InterruptedForNewObjective
		|| reasonAbilityEnded == EAbilityPurposeFeedback::InterruptedByOverlappingResources
		|| reasonAbilityEnded == EAbilityPurposeFeedback::InterruptedByDeath /// In this case we don't have a new purpose already selected, but character is entering death state
		;

	EPurposeState objectiveState = EPurposeState::None;

	FContextData& objectiveContext = GetStoredPurpose(inContext.GetContextID(), inContext.addressOfPurpose, (int)EPurposeLayer::Objective);
	FContextData& goalContext = GetStoredPurpose(inContext.GetContextID(), inContext.addressOfPurpose, (int)EPurposeLayer::Goal);

	if (objectiveContext.ContextIsValid() && goalContext.ContextIsValid())
	{
		objectiveState = EvaluateObjectiveStatus(objectiveContext);/// Evaluate the status of the objective belonging to a chain of purpose
		goalContext.UpdateSubPurposeStatus(objectiveContext.addressOfPurpose, objectiveState);/// Ensure parent context has updated Objective status
	}
	else
	{
		objectiveState = EPurposeState::Ongoing;/// The incoming ability context was from a reaction, so just return to current Objective for actor
	}
	Global::Log(DATADEBUG, TASK, *this, "ActorFinishedAbility", TEXT("Objective Status: %s. Ability Feedback State: %s")
		, *Global::EnumValueOnly<EPurposeState>(objectiveState)
		, *Global::EnumValueOnly<EAbilityPurposeFeedback>(reasonAbilityEnded)
	);

	switch (objectiveState)
	{
		case EPurposeState::Ongoing:/// Retrieve a new ability from actor's existing Objective

			if (shouldNotSeekNewPurpose) { return; }/// We return because we don't wish to find a new Objective or new Ability, as one or the other was already selected for inActor and that's why this Ability ended

			if (CurrentObjective().ContextIsValid())
			{
				NewAbilityFromCurrentObjective();/// Valid current objective, select new ability from it
			}
			else
			{
				/// If the context has an Objective, then this actor was somehow involved and we want to ensure the data is adjusted to indicate that the Objective lost a participant
				if (objectiveContext.ContextIsValid()) { objectiveContext.AdjustDataIfPossible(objectiveContext.purpose.DataAdjustments(), EPurposeSelectionEvent::OnFinished, OBJECTIVE, "AbilityHasFinished", this); }
				SelectNewObjectiveFromExistingGoals();/// No valid current objective, get a new one
			}
			break;
		case EPurposeState::Complete:/// If the Objective is complete, evaluate status of Goal
			{
				EPurposeState goalState = EvaluateGoalStatus(goalContext);
				Global::Log(DATADEBUG, TASK, *this, "ActorFinishedAbility", TEXT("Goal Status: %s"), *Global::EnumValueOnly<EPurposeState>(goalState));

				/// We don't check shouldNotSeekNewPurpose here because we want to perform GoalComplete logic if necessary
				switch (goalState)
				{
					case EPurposeState::Complete:/// If the Goal was completed, notify Director, then compile objectives from remaining goals for actor to select

						FContextData& eventContext = GetStoredPurpose(inContext.GetContextID(), inContext.addressOfPurpose, (int)EPurposeLayer::Event);

						eventContext.UpdateSubPurposeStatus(goalContext.addressOfPurpose, goalState);/// Ensure parent context has updated Goal status

						bool bAllPurposeComplete = false;

						for (auto subStatus : eventContext.subPurposeStatus)
						{
							if (subStatus.Value != EPurposeState::Complete)
							{
								break;
							}

							bAllPurposeComplete = true;
							eventContext.purposeOwner->AllSubPurposesComplete(eventContext.GetContextID(), eventContext.addressOfPurpose);
						}

						if (!bAllPurposeComplete && goalState == EPurposeState::Complete)
						{
							eventContext.purposeOwner->SubPurposeCompleted(goalContext.GetContextID(), goalContext.addressOfPurpose);
						}

						if (shouldNotSeekNewPurpose) { return; }/// We return because we don't wish to find a new Objective or new Ability, as one or the other was already selected for inActor and that's why this Ability ended

						EndCurrentObjective();///Ensure previous Objective is ending

						SelectNewObjectiveFromExistingGoals();;
						break;
					case EPurposeState::Ongoing:/// Else just retrieve a new Objective from current Goals for actor

						if (shouldNotSeekNewPurpose) { return; }/// We return because we don't wish to find a new Objective or new Ability, as one or the other was already selected for inActor and that's why this Ability ended

						EndCurrentObjective();///Ensure previous Objective is ending

						SelectNewObjectiveFromExistingGoals();
						break;
				}
			}
			break;
	}
}

EPurposeState UPurposeAbilityComponent::EvaluateObjectiveStatus(const FContextData& contextOfObjective)
{
	EPurposeState objectiveStatus = EPurposeState::None;
	float score = 0.0f;
	float finalScore = 0.0f;

	/// Refactor: Purpose Completion ContextData; How can we determine if a purpose is done
		/// Previous method was a separate set of conditions
		/// What if we used the current conditions, and possibly the score cache?
		/// It has to be accessible from anywhere. Currently stored event assets are held on the level director

	if (contextOfObjective.purpose.completionCriteria.Num() <= 0)
	{
		objectiveStatus = EPurposeState::Ongoing;/// Without conditions we have no way of gauging the state of an Objective
	}
	else
	{
		for (const TObjectPtr<UCondition> condition : contextOfObjective.purpose.completionCriteria)///Score each condition and add to finalscore of purpose
		{
			if (!condition)
			{
				Global::LogError(OBJECTIVE, *this, "EvaluateObjectiveStatus", TEXT("Context->ParentPurpose->completionCriteria returned an invalid object."));
				continue;
			}
			TMap<ESubject, TArray<FDataMapEntry>> subjectsWithoutPointers;
			subjectsWithoutPointers.Add(ESubject::Context, contextOfObjective.contextData);
			subjectsWithoutPointers.Append(contextOfObjective.subjectMap.GetSubjectsAsDataMaps());
			score += condition->EvaluateCondition(subjectsWithoutPointers, contextOfObjective.purposeOwner, contextOfObjective.GetContextID(), contextOfObjective.addressOfPurpose);///Get a baseline 0-1 score for condition
			////Global::Log(Debug, PurposeLog, "FPurposeEvaluationThread", "EvaluatePurpose", TEXT("Score for Condition: %s = %f; Potential Score = %f."), *condition->description.ToString(), score, potentialScore);
		}

		/// Scoring for completion criteria is meant to be more yes/no than scoring for purpose selection conditions.
		finalScore = score / contextOfObjective.purpose.completionCriteria.Num();/// Get the average score

		if (finalScore == 1) { objectiveStatus = EPurposeState::Complete; }
		if (finalScore < 1) { objectiveStatus = EPurposeState::Ongoing; }
	}

	//Global::Log( Debug, PurposeLog, *this, "EvaluateObjectiveStatus", TEXT("Score: %f, Status: %s"), finalScore, *Global::EnumValueOnly<EPurposeState>(objectiveStatus));

	return objectiveStatus;
}

EPurposeState UPurposeAbilityComponent::EvaluateGoalStatus(const FContextData& contextOfGoal)
{
	EPurposeState state = EPurposeState::Complete;

	for (auto objective : contextOfGoal.subPurposeStatus)
	{
		if (objective.Value == EPurposeState::Ongoing)
		{
			state = EPurposeState::Ongoing;/// So long as a single objective is Ongoing, Goal is incomplete
		}
	}

	return state;
}

void UPurposeAbilityComponent::SelectNewObjectiveFromExistingGoals()
{
	const TArray<FContextData>& goals = GetPurposeSuperior()->GetActivePurposes();

	for (const FContextData& goal : goals)
	{
		PurposeSystem::QueueNextPurposeLayer(goal);
	}
}

void UPurposeAbilityComponent::NewAbilityFromCurrentObjective()
{
	PurposeSystem::QueueNextPurposeLayer(CurrentObjective());
}

#pragma endregion

