// Copyright Jordan Cain. All Rights Reserved.


#include "Abilities/AI/AI_SequentialActions.h"
#include "Abilities/Tasks/TaskResources.h"
#include "Purpose/Abilities/GA_Action.h"

void UAI_SequentialActions::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (sequenceOfActions.Num() < 1)
	{
		Global::Log(DATADEBUG, BEHAVIOR, *AbilityNameForLog(), "ActivateAbility", TEXT("No actions added to sequence!"));
		AbilityFinished(EAbilityPurposeFeedback::FinishedUninterrupted);
		return;
	}

	AttemptSequenceOfActions();
}

void UAI_SequentialActions::AttemptSequenceOfActions()
{
	Global::Log(DATADEBUG, BEHAVIOR, AbilityNameForLog(), "QueryFinished", TEXT("Action index to perform %d!"), actionIndex);
	if (!sequenceOfActions.IsValidIndex(actionIndex))
	{
		Global::LogError(BEHAVIOR, AbilityNameForLog(), "QueryFinished", TEXT("Action index %d is invalid!"), actionIndex);
		AbilityFinished(EAbilityPurposeFeedback::FinishedUninterrupted);
		return;
	}
	FActionSequenceEntry& currentAction = sequenceOfActions[actionIndex];

	if (actionAttemptsOnFail > maxFailAttempts)
	{
		Global::LogError(BEHAVIOR, AbilityNameForLog(), "AttemptSequenceOfActions", TEXT("Too many attempts for %s!"), *currentAction.description.ToString());
		AbilityFinished(EAbilityPurposeFeedback::FinishedUninterrupted);
		return;
	}

	/// Firstly, we need to duplicate all Tasks that are a part of this sequence
	/// This way we can modify the Tasks without affecting the static version in the purpose asset
	for (int i = 0; i < currentAction.tasksForAction.Num(); ++i)
	{
		FActionEntry& taskEntry = currentAction.tasksForAction[i];
		if (!taskEntry.task)
		{
			Global::LogError(BEHAVIOR, AbilityNameForLog(), "AttemptSequenceOfActions", TEXT("Task for entry %d invalid!"), i);
			continue;
		}

		UGameplayTask* taskDuplicate = NewAbilityTask(taskEntry.task);
		
		taskDuplicate->AddClaimedResource(taskEntry.requiredResource);
		taskDuplicatesForCurrentAction.Add(taskDuplicate);/// the task now has required resources
		/// The index of the cached duplicate and the original entry are identical, so when we go to perform data adjustments we just use the same index
	}

	PerformTask();
}

void UAI_SequentialActions::PerformTask()
{
	Global::Log(FULLTRACE, BEHAVIOR, AbilityNameForLog(), "PerformTask", TEXT(""));
	if (!IsActive() || bIsAbilityEnding)/// As we are utilizing gameplay tasks and delegates, we need to account for callbacks when ability is ending
	{
		return;
	}
	Global::Log(CALLTRACETRIVIAL, BEHAVIOR, AbilityNameForLog(), "PerformTask", TEXT("Attempting to Perform Task."));

	if (!sequenceOfActions.IsValidIndex(actionIndex))
	{
		Global::LogError(BEHAVIOR, AbilityNameForLog(), "PerformTask", TEXT("Action index %d is invalid!"), actionIndex);
		AbilityFinished(EAbilityPurposeFeedback::FinishedUninterrupted);
		return;
	}
	FActionSequenceEntry& entry = sequenceOfActions[actionIndex];

	for (int i = 0; i < taskDuplicatesForCurrentAction.Num(); ++i)
	{
		TObjectPtr<UGameplayTask> task = taskDuplicatesForCurrentAction[i];

		if (!task)
		{
			Global::Log(DATATRIVIAL, TASK, AbilityNameForLog(), "PerformTask", TEXT("Task invalid!"));
			continue;
		}

		Global::Log(DATATRIVIAL, TASK, AbilityNameForLog(), "PerformTask", TEXT("Performing %s."), *task->GetName());

		/// Now that we are ready to activate our task, we want a pre activation data adjustment
		/// This allows us to take datachunks from one task and apply them to another, modifying how the task is performed based on adjustments requested in the details panel in editor
		/// And we only perform adjustments on the current task, so that any post activation initialization may occur before data adjustments (except on the very first task)
		/// Because the tasks were duplicated from tasksForAction, the indices will align with taskDuplicatesForCurrentAction
		FActionEntry& taskEntry = entry.tasksForAction[i];

		/// Perform any data adjustments specified on this task entry
		for (auto& adjustment : taskEntry.dataAdjustmentsForThisTask)
		{
			UDataChunk* chunkToAdjust = Cast<IDataMapInterface>(task)->DataChunk(adjustment.dataToModify);
			UDataChunk* chunkToGetAdjustmentFrom = DataChunk(adjustment.dataToRetrieve);

			if (!chunkToAdjust || !chunkToGetAdjustmentFrom)
			{
				Global::Log(DATATRIVIAL, TASK, AbilityNameForLog(), "PerformTask", TEXT("DataChunk to adjust: %s; DataChunk to get adjustment from: %s.")
					, chunkToAdjust ? *chunkToAdjust->GetName() : TEXT("invalid")
					, chunkToGetAdjustmentFrom ? *chunkToGetAdjustmentFrom->GetName() : TEXT("invalid")
				);
				continue;
			}

			Global::Log(DATATRIVIAL, BEHAVIOR, AbilityNameForLog(), "PerformTask", TEXT("Copied %s to %s for %s!"), *chunkToGetAdjustmentFrom->GetName(), *chunkToAdjust->GetName(), *task->GetName());
			chunkToAdjust->CopyDataFrom(chunkToGetAdjustmentFrom);
		}

		bool bShouldPerformTask = true;

		ISequenceTaskInterface* sequenceTask = Cast<ISequenceTaskInterface>(task);
		if (sequenceTask)
		{
			bShouldPerformTask = sequenceTask->PreActivationInitialization();

			if (!bShouldPerformTask) { continue; }/// If the task notifies us it shouldn't be performed simply ignore it

			FScriptDelegate finishDel;
			finishDel.BindUFunction(this, "TaskFinished");

			sequenceTask->EstablishTaskFinishedCallback(finishDel);

			bShouldPerformTask = sequenceTask->ReceiveSequenceEntryData(entry, *this);
		}

		if (!bShouldPerformTask) { continue; }/// If the task notifies us it shouldn't be performed simply ignore it

		/// This requires that the TasksComponent be valid for task
		/// Which will be set via InitTask(), which this will call
		UGameplayTasksComponent::RunGameplayTask(*this, *task, (uint8)EAITaskPriority::Low, FGameplayResourceSet(), FGameplayResourceSet());

		/// Task activation should be synchronous, so task should be activated and running by the time we get here
		/// This is important, as some data adjustments for a task require the task to be activate (such as getting the path from a MoveTo)
		/// Refactor: AI Tasks Data; Path may not have been set because we didn't have a move goal
		if (sequenceTask)
		{
			sequenceTask->PostActivationAdjustment();
		}
	}
}

void UAI_SequentialActions::TaskFinished()
{
	Global::Log(DATATRIVIAL, BEHAVIOR, AbilityNameForLog(), "TaskFinished", TEXT("%d tasks registered as active for action %s."), taskDuplicatesForCurrentAction.Num(), *sequenceOfActions[actionIndex].description.ToString());
	for (int i = taskDuplicatesForCurrentAction.Num() - 1; i >= 0; --i)/// Iterate through all activeTasks for the current action
	{
		//TWeakObjectPtr<UGameplayTask> taskOfCurrentAction = taskDuplicatesForCurrentAction[i];
		TObjectPtr<UGameplayTask> taskOfCurrentAction = taskDuplicatesForCurrentAction[i];

		//if (!taskOfCurrentAction.IsValid())/// If invalid, reduce active tasks
		if (!taskOfCurrentAction)/// If invalid, reduce active tasks
		{
			taskDuplicatesForCurrentAction.RemoveAt(i);
			continue;
		}

		if (taskOfCurrentAction->IsFinished())/// If finished, reduce active tasks
		{
			/// Since task is finished, let's copy what DataChunks are stored on the task here so that we can access them in the future
			if (taskOfCurrentAction->Implements<UDataMapInterface>())
			{
				dataMap.Append(Cast<IDataMapInterface>(taskOfCurrentAction.Get())->CopyOfDataChunks(*this));
			}

			taskDuplicatesForCurrentAction.RemoveAt(i);
			continue;
		}
	}

	if (taskDuplicatesForCurrentAction.Num() < 1)/// If there are no more active tasks for the current action
	{
		Global::Log(DATATRIVIAL, BEHAVIOR, AbilityNameForLog(), "TaskFinished", TEXT("All tasks for action %s finished!"), *sequenceOfActions[actionIndex].description.ToString());
		AttemptNextSequenceOfActions();/// Then we can attempt the next action in the sequence
		return;
	}

	Global::Log(DATATRIVIAL, BEHAVIOR, AbilityNameForLog(), "TaskFinished", TEXT("%d active tasks left for action %s."), taskDuplicatesForCurrentAction.Num(), *sequenceOfActions[actionIndex].description.ToString());
}


