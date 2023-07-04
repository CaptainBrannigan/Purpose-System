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
#include "Purpose/Director_Level.h"

UPurposeAbilityComponent::UPurposeAbilityComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Global::Log(CALLTRACEESSENTIAL, PURPOSE, *this, "UPurposeAbilityComponent", TEXT("Owner: %s"), GetOwner() ?  *GetOwner()->GetName() : TEXT("Invalid"));
}

void UPurposeAbilityComponent::InitializePurposeSystem(TObjectPtr<class ADirector_Level> inDirector, TArray<FPurposeEvaluationThread*> backgroundThreads)
{
	cacheOfBackgroundThreads = backgroundThreads;
	director = inDirector;


	AbilityActivatedCallbacks.AddUObject(this, &UPurposeAbilityComponent::ActionPerformed);/// Primarily set up since player input goes straight to the ability system
	/// But now all behavior occurrences are routed through BehaviorPerformed
}

void UPurposeAbilityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	/// Here we list the variables we want to replicate + a condition if wanted
	DOREPLIFETIME(UPurposeAbilityComponent, data);
}

#pragma region Datamap Interface

void UPurposeAbilityComponent::AddData_Implementation(UDataChunk* inData, bool overwriteValue)
{
	AddDataLocal(*inData, overwriteValue);
}

void UPurposeAbilityComponent::AppendData_Implementation(const TArray<FDataMapEntry>& inDataMap, bool overwriteValue)
{
	AppendDataLocal(inDataMap, overwriteValue);
}

void UPurposeAbilityComponent::RemoveData_Implementation(TSubclassOf<UDataChunk> inClass)
{
	RemoveDataLocal(inClass);
}

#pragma endregion

/// BugWatch: AI Purpose PerformAbility; If an AI reaches a point where their Activity purpose ends, and they have no other Activity behaviors to perform
	/// They may just stand there helplessly
	/// Or they may bounce back and forth between unintended Behaviors
		/// If that happens too quickly, exiting the game may force an !IsRooted assertion from PurposeThread as too many ASyncAbilities are going in and out to clean up on shutdown


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

#pragma region Purpose

bool UPurposeAbilityComponent::ProvidePurposeToOwner(const FContextData& purposeToStore)
{
	Global::Log(DATAESSENTIAL, PURPOSE, *this, "ProvidePurposeToOwner", TEXT("Providing %s with Objective: %s.")
		, *GetFullGroupName(false)
		, *purposeToStore.purpose.descriptionOfPurpose
	);

	/// Since we already establish an existing behavior purpose score as the high score for purpose selection, do we need to check for similarity?
		/// If anything we should only check is the behavior is the same
	/*if (ObjectivesAreSimilar(purposeToStore, CurrentBehavior()))
	{
		Global::Log(DATADEBUG, PURPOSE, *this, "ProvidePurposeToOwner", TEXT("Incoming purpose %s already being performed by %s.")
			, *purposeToStore.GetName()
			, *GetFullGroupName(false)
		);
		return false;
	}*/
	
	/*if 
	(
		purposeToStore.HasSubject(ESubject::EventTarget) && CurrentBehavior().HasSubject(ESubject::EventTarget)
		&& purposeToStore.Subject(ESubject::EventTarget) == CurrentBehavior().Subject(ESubject::EventTarget) /// If the target
		&& purposeToStore.purpose.behaviorAbility == CurrentBehavior().purpose.behaviorAbility)/// And the ability are both the same
		///Then we don't want to restart behavior
	{
		Global::Log(DATADEBUG, PURPOSE, *this, "ProvidePurposeToOwner", TEXT("Incoming purpose %s already being performed by %s.")
			, *purposeToStore.GetName()
			, *GetFullGroupName(false)
		);
		return false;
	}*/

	EndCurrentBehavior();/// Explicitly pass the previous Objective, to ensure avoiding any chance of ending new Objective

	SetCurrentBehavior(purposeToStore);///Set the current objective of the actor

	PerformAbility(purposeToStore, purposeToStore.purpose.behaviorAbility, 1);

	return true;
}

void UPurposeAbilityComponent::EndCurrentBehavior()
{
	if (!CurrentBehavior().ContextIsValid())
	{
		Global::Log(DATAESSENTIAL, BEHAVIOR, *this, "{", TEXT("currentBehavior for %s invalid!")
			, *GetFullGroupName(false)
		);
		return;/// If the Objective is invalid, it likely means it comes from a nullptr of FActorData::currentBehavior
		/// Which is nullified when currentBehavior begins clean up
		/// Because AbilityFinished will be called for every Ability in the Objective, which will tell the Objective to clean up
	}

	Global::Log(DATAESSENTIAL, BEHAVIOR, *this, "EndObjective", TEXT("Ending Abilities of currentBehavior %s for %s.")
		, *CurrentBehavior().GetName()
		, *GetFullGroupName(false)
	);

	/// Crucial that on finished data adjustment is made if needed
	CurrentBehavior().AdjustDataIfPossible(CurrentBehavior().purpose.DataAdjustments(), EPurposeSelectionEvent::OnFinished, BEHAVIOR, "EndObjective", this);

	if (CurrentBehavior().purpose.behaviorAbility)
	{
		Global::Log(DATADEBUG, PURPOSE, *this, "EndObjective", TEXT("Ending %s for %s.")
			, *CurrentBehavior().purpose.behaviorAbility->GetName()
			, *GetName()
		);
		EndAbilitiesOf(CurrentBehavior().purpose.behaviorAbility->GetClass(), EAbilityPurposeFeedback::InterruptedForNewObjective);/// This could lead to a recursed call to AbilityFinished() without Ability()->FeedbackState()
		/// Bugwatch: Abilities Purpose Feedback; If abilityFeedbackState is not set in AbilityFinished() this could lead to an infinite loop of AbilityFinished() -> EndObjective() -> AbilityFinished()
	}
	else
	{

		Global::LogError(PURPOSE, * this, "EndObjective", TEXT("Behavior from %s for %s is invalid!")
			, *CurrentBehavior().GetName()
			, *GetName()
		);
	}

	SetCurrentBehavior(FContextData());/// Ensure the previous objective is no longer referenced
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

		/// So we need to solve issue with ActionPerformed somehow causing an empty purpose queued to the background thread
			/// I bet the struct is being GC'd
			/// Also need to solve why ability tasks are not being ended, has something to do with SequentialActions abilities. Tasks probably aren't being provided the ability correctly so they don't add to the activetasks
		if (Ability->IsA<UGA_PurposeBase>())
		{
			subjectMap = Cast<UGA_PurposeBase>(Ability)->context.subjectMap;
			contextData = Cast<UGA_PurposeBase>(Ability)->context.contextData;
		}

		subjectMap.subjects.Add(ESubject::Instigator, this);
		contextData.Add(FDataMapEntry(&NewObject<UActorAction>(this)->Initialize(Ability->GetClass())));

		PurposeSystem::Occurrence(subjectMap, contextData, cacheOfBackgroundThreads);
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
	bool bShouldNotSeekNewPurpose =
		reasonAbilityEnded == EAbilityPurposeFeedback::InterruptedForNewObjective
		|| reasonAbilityEnded == EAbilityPurposeFeedback::InterruptedByOverlappingResources
		|| reasonAbilityEnded == EAbilityPurposeFeedback::InterruptedByDeath /// In this case we don't have a new purpose already selected, but character is entering death state
		;

	if (bShouldNotSeekNewPurpose)
	{
		return;
	}

	EndCurrentBehavior();///Ensure previous Objective is ending

	/// Refactor: Purpose Behavior Finished; When ability finished, what if there isn't an occurrence which gives this AI another purpose?
		/// Or how do we specifically seek activities?
		/// We could create an Activity Occurrence?
			/// Well maybe not an occurrence, but rather find most suitable activity for AI
			/// If we did an occurrence, every AI would be polled again, which is unnecessary

	SeekNewBehavior();
}

void UPurposeAbilityComponent::SeekNewBehavior()
{
	if (!director.IsValid())
	{
		Global::LogError(MANAGEMENT, GetFullGroupName(false), "SeekNewBehavior", TEXT("Director is invalid!"));
		return;
	}

	FSubjectMap subjectsForNewBehavior;

	subjectsForNewBehavior.subjects.Add( ESubject::Instigator, TScriptInterface<IDataMapInterface>(director.Get()) );
	PurposeSystem::Occurrence(TArray<TObjectPtr<UPurposeAbilityComponent>>({ this }), FSubjectMap(), TArray<FDataMapEntry>(), cacheOfBackgroundThreads);
}

#pragma endregion

