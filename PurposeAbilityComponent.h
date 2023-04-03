// Copyright Jordan Cain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataMapInterface.h"
#include "AbilitySystem/LyraAbilitySystemComponent.h"
#include "Purpose/Abilities/GA_PurposeBase.h"
#include "Purpose/PurposeEvaluationThread.h"
#include "PurposeAbilityComponent.generated.h"

UCLASS()
/// <summary>
/// The purpose ability component is a marriage of the UGameplayAbilitySystem with the Purpose System
/// As a component it can be added to any actor and the management can keep track of that actor (for purpose system) via this component
/// </summary>
class UPurposeAbilityComponent : public ULyraAbilitySystemComponent, public IDataMapInterface, public IPurposeManagementInterface
{
	GENERATED_BODY()
public:

	UPurposeAbilityComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/// Enforces necessity for providing reqs to PurposeSystem
	void InitializePurposeSystem(TObjectPtr<class AManager> inManager);

	TObjectPtr<class AManager> Manager() { return manager; }
	
	bool HasCurrentObjective() { return currentObjectiveForOwner.ContextIsValid(); }

	FContextData& CurrentObjective() { return currentObjectiveForOwner; }
	void SetCurrentObjective(FContextData inContext) { currentObjectiveForOwner = inContext; }

	//TObjectPtr<ACharacter> GetOwnerCharacter() { return Cast<ACharacter>(GetOwner()); }
	TObjectPtr<ACharacter> GetOwnerCharacter() { return Cast<ACharacter>(Cast<AController>(GetOwner())->GetPawn()); }

	///@return Name of the object.
	FORCEINLINE FString GetName() const
	{
		return UObject::GetName() + "::" + (GetOwner() ? GetOwner()->GetName() : "Unknown Owner");
	}

protected:

	TObjectPtr<class AManager> manager = nullptr;

	FContextData currentObjectiveForOwner;

#pragma region Datamap Interface
public:


private:

	UPROPERTY(Replicated)
	///This data is representative of the subject "ESubject::Candidate"
	TArray<FDataMapEntry> data;

	/// Force implementers to provide an editable datamap
	/// The only way to the change the DataMap is through the Server RPCs or server side calls
	TArray<FDataMapEntry>& DataMapInternal() final { return data; }

#pragma endregion

#pragma region Gameplay Abilities
public:
	/// @param abilityClass: UPurposeAbilityComponent will create an Initialize the abilityClass and immediately attempt to run
	void PerformAbility(const FContextData& inContext, TSubclassOf<UGA_PurposeBase> abilityClass);

	/// @param ability: Existing Ability will be duplicated and performed, bypassing the need for custom initializations and allowing ability specific properties editable in editor
	void PerformAbility(const FContextData& inContext, const TObjectPtr<UGA_PurposeBase> ability, uint8 priority);

	/// @param ability: Ability as a non-null reference, as these abilitys have already been initialized prior to being sent to abilitysmanager
	void PerformAbility(UGA_PurposeBase& ability);

	///Refactor: Abilities Abilities Purpose; Consider whether we should be able to execute Abilities directly, or if only Abilities can perform AbilityAbilities
	void EndAbilitiesOf(TSubclassOf<UGA_PurposeBase> inClass, const EAbilityPurposeFeedback reasonAbilityEnded, TObjectPtr<UGA_PurposeBase> abilityToExclude = nullptr);
	void EndAbilitiesOf(TSubclassOf<UGA_PurposeBase> inClass, const EAbilityPurposeFeedback reasonAbilityEnded, TArray<TObjectPtr<UGA_PurposeBase>> abilitiesToExclude);
	void EndAbilitiesOf(TSubclassOf<UGA_PurposeBase> inClass, const EAbilityPurposeFeedback reasonAbilityEnded, TSubclassOf<UGA_PurposeBase> abilitiesToExclude);

	template<class T>
	T* GetAbility()
	{
		//for (TObjectPtr<UGA_PurposeBase> ability : KnownAbilities)
		//{
		//	/*	//Global::Log(FULLTRACE, AbilityLog, "UPurposeAbilityComponent", "GetAbility<T>", TEXT("Known Ability: %s. Sought Ability: %s.")
		//			, *ability->GetName()
		//			, *T::StaticClass()->GetName()
		//		);*/
		//	if (ability->IsA<T>())
		//	{
		//		return Cast<T>(ability);
		//	}
		//}

		return nullptr;
	}

protected:

	/// @Param Spec: Duplicates the ability belonging to Spec, then sets the Spec.Ability to the duplicated Ability
	/// @return FGameplayAbilitySpec: The ability spec added to ActivatableAbilites.Items, containing duplicated ability from Spec and new FGameplaySpecHandle
	FGameplayAbilitySpec GiveAbilityDuplicate(FGameplayAbilitySpec Spec);

#pragma endregion

#pragma region AbilitySystemComponent Overrides

	/// Overrides
	/*
	 * Grants an Ability.
	 * This will be ignored if the actor is not authoritative.
	 * Returns handle that can be used in TryActivateAbility, etc.
	 *
	 * @param AbilitySpec FGameplayAbilitySpec containing information about the ability class, level and input ID to bind it to.
	 */
	///FGameplayAbilitySpecHandle GiveAbility(const FGameplayAbilitySpec& AbilitySpec);

	/// Useful methods
		/**
	 * Clears all abilities bound to a given Input ID
	 * This will be ignored if the actor is not authoritative
	 *
	 * @param InputID The numeric Input ID of the abilities to remove
	 */
	///UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Gameplay Abilities")
	///	void ClearAllAbilitiesWithInputID(int32 InputID = 0);

	/**
	 * Removes the specified ability.
	 * This will be ignored if the actor is not authoritative.
	 *
	 * @param Handle Ability Spec Handle of the ability we want to remove
	 */
	///UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Gameplay Abilities")
		///void ClearAbility(const FGameplayAbilitySpecHandle& Handle);

	/// Debug info

	///static void OnShowDebugInfo(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos);
	///virtual void DisplayDebug(class UCanvas* Canvas, const class FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos);
	///virtual void PrintDebug();
	///void AccumulateScreenPos(FAbilitySystemComponentDebugInfo& Info);
	///virtual void Debug_Internal(struct FAbilitySystemComponentDebugInfo& Info);
	///void DebugLine(struct FAbilitySystemComponentDebugInfo& Info, FString Str, float XOffset, float YOffset, int32 MinTextRowsToAdvance = 0);

#pragma endregion

#pragma region Purpose Owner Interface
public:

	/// Used in order to reference up the management chain to the owner of Events and Backgrounds threads
	TScriptInterface<IPurposeManagementInterface> GetHeadOfPurposeManagment() final;

	/// @return TScriptInterface<IPurposeManagementInterface>: Returns the immediate purpose manager above caller
	TScriptInterface<IPurposeManagementInterface> GetPurposeSuperior() final;

	TArray<FPurposeEvaluationThread*> GetBackgroundPurposeThreads() final;

	TArray<TScriptInterface<IDataMapInterface>> GetCandidatesForSubPurposeSelection(const int PurposeLayerForUniqueSubjects) final;

	/// @param PurposeLayerForUniqueSubjects: Represents the purpose layer for which the PurposeOwner is meant to create new FUniqueSubjectMaps
	/// @param parentContext:
	/// @param candidate: This is the primary subject that will be combined with other subjects as needed for purpose selection
	/// @return TArray<FSubjectMap>: Each entry is a combination of the candidate and whatever other subjects required for the subpurpose indicated by addressOfSubPurpose
	TArray<FSubjectMap> GetUniqueSubjectsRequiredForSubPurposeSelection(const int PurposeLayerForUniqueSubjects, const FContextData& parentContext, TScriptInterface<IDataMapInterface> candidate, FPurposeAddress addressOfSubPurpose) final;

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
	FContextData& GetStoredPurpose(const int64 uniqueIdentifierOfContextTree, const FPurposeAddress& fullAddress, const int layerToRetrieveFor) override;

	/// @param parentAddress: An address which is either that of a purpose containing behaviors so that it may reference the parent, or the parent address itself
	/// @param TArray<TObjectPtr<UGA_Behavior>>: All the behaviors contained by the parent indicated
	TArray<TObjectPtr<class UBehavior_AI>> GetBehaviorsFromParent(const FPurposeAddress& parentAddress) override { return GetHeadOfPurposeManagment()->GetBehaviorsFromParent(parentAddress); }

	/// @param parentAddress: An address of the behavior containing purpose
	/// @param TObjectPtr<UGA_Behavior>: The behavior contained by the address provided
	TObjectPtr<class UBehavior_AI> GetBehaviorAtAddress(const FPurposeAddress& inAddress) final { return GetHeadOfPurposeManagment()->GetBehaviorAtAddress(inAddress); }

	///@return bool: True when the target and candidate are the same
	bool DoesPurposeAlreadyExist(const FContextData& primary, const FSubjectMap& secondarySubjects, const TArray<FDataMapEntry>& secondaryContext, const FPurposeAddress optionalAddress = FPurposeAddress()) final;

#pragma endregion

#pragma region Purpose

	bool ObjectivesAreSimilar(const FContextData& primary, const FContextData& secondary);

	void EndCurrentObjective();

	/// For instances when GA activation can not be routed through the manager, this method is bound to the inActor's AbilityActivationCallbacks
	/// Shouldn't need to unbind, as the existence of inActor (which owns the delegate this method is bound to) is dependent on this manager, who will outlive the component
	/// @param Ability: The GA that has been activated, for access to its context data
	void ActionPerformed(UGameplayAbility* Ability);

	/// @param inActor: Upon completing a ability, provide this with a new ability
	/// Manager will begin evaluation of state of Purpose Chain
	void AbilityHasFinished(const FContextData& inContext, const EAbilityPurposeFeedback reasonAbilityEnded);

	/// @return EPurposeState::Complete if parent ObjectiveContext->CompletionCriteria indicate completion
	EPurposeState EvaluateObjectiveStatus(const FContextData& contextOfObjective);

	/// @return EPurposeState::Complete if all required ObjectiveContext->CompletionCriteria belonging to Goal indicate completion
	EPurposeState EvaluateGoalStatus(const FContextData& inContext);

	/// Compile all objectives from existing goals of Manager for selection
	void SelectNewObjectiveFromExistingGoals();

	/// Select a ability from inActor's current objective
	void NewAbilityFromCurrentObjective();


#pragma endregion
};
