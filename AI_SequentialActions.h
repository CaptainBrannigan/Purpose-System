// Copyright Jordan Cain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Purpose/Abilities/Behavior_AI.h"
#include "DataChunks/PatrolPoints.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameplayTagContainer.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Abilities/SequenceTaskInterface.h"
#include "DataMapInterface.h"
#include "AI_SequentialActions.generated.h"

UCLASS(MinimalAPI)
/// This AI behavior 
class UAI_SequentialActions : public UBehavior_AI, public IDataMapInterface
{
	GENERATED_BODY()
public:

	/*UAI_EngageTarget(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		ResourceOverlapPolicy = EAbilityResourceOverlapPolicy::StartOnTop;
		AddRequiredResource(UAIResource_Movement::StaticClass());
		AddClaimedResource(UAIResource_Movement::StaticClass());
	}*/

	/** Called to trigger the actual ability once the delegates have been set up
	 *	Note that the default implementation does nothing and you don't have to call it */
	void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) final;

	void AttemptSequenceOfActions();

	void AttemptNextSequenceOfActions()
	{
		taskDuplicatesForCurrentAction.Empty();/// Ensure the previous task pointers are all removed
		++actionIndex;
		AttemptSequenceOfActions();
	}

	void PerformTask();

	UFUNCTION()/// Has to be UFUNCTION() otherwise FScriptDelegate cannot bind to it
		void TaskFinished();

	UPROPERTY(EditAnywhere, Category = "Behavior Data", DisplayName = "Sequential Actions", meta = (TitleProperty = "description"))
		TArray<FActionSequenceEntry> sequenceOfActions;

protected:

	int actionIndex = 0;
	int actionAttemptsOnFail = 0;
	int maxFailAttempts = 2;

	UPROPERTY()
		///TArray<FActionEntry> taskDuplicatesForCurrentAction;
		TArray<TObjectPtr<UGameplayTask>> taskDuplicatesForCurrentAction;

#pragma region DataMapInterface
	///Allows evaluations to retrieve chunks of data from any type of object implementing interface
	const TArray<FDataMapEntry>& DataMap() final { return dataMap; }
	TArray<FDataMapEntry> DataMapCopy() final { return dataMap; }

	void AddData(UDataChunk* inData, bool overwriteValue = true) final { AddDataLocal(*inData); }
	void AppendData(const TArray<FDataMapEntry>& inDataMap, bool overwriteValue = true) final { AppendDataLocal(inDataMap); }
	void RemoveData(TSubclassOf<UDataChunk> inClass) final { RemoveDataLocal(inClass); }

	UPROPERTY(EditAnywhere)
		TArray<FDataMapEntry> dataMap;

	/// Force implementers to provide an editable datamap
	/// The only way to the change the DataMap is through the Server RPCs
	TArray<FDataMapEntry>& DataMapInternal() final { return dataMap; }

#pragma endregion
};