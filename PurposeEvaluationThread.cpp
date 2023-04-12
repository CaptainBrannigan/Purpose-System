// Copyright Jordan Cain. All Rights Reserved.


#include "Purpose/PurposeEvaluationThread.h"
#include "Purpose/Condition.h"
#include "Curves/CurveFloat.h"
#include "Purpose/PurposeAbilityComponent.h"
#include "Purpose/Abilities/GA_PurposeBase.h"

FString FContextData::GetOwnerName()
{
	return purposeOwner.IsValid() ? purposeOwner->GetName() : "Invalid";
}

#pragma region PurposeEvaluationThread

bool FPurposeEvaluationThread::Init()
{
	//Global::Log(FULLTRACE, PURPOSE, "FPurposeEvaluationThread", "Init", TEXT(""));
	return true;/// Has to be true otherwise Thread won't run
}

void FPurposeEvaluationThread::Stop()
{
	//Global::Log(FULLTRACE, PURPOSE, "FPurposeEvaluationThread", "Stop", TEXT(""));
	stopThread = true;
	tickTimer = 1000.0f;/// Just in case Run() is somehow called in the middle of shutdown, it shouldn't have time to call again
}

void FPurposeEvaluationThread::Exit()
{
	//Global::Log(FULLTRACE, PURPOSE, "FPurposeEvaluationThread", "Exit", TEXT(""));
	//for (auto task : purposeSelectionTasks)
	//{
	//	if (task)
	//	{
	//		//Global::Log(FULLTRACE, PURPOSE, "FPurposeSelectionThread", "Exit", TEXT("InGameThread? %s. Attempting to cancel AsyncAbility."), IsInGameThread() ? TEXT("True") : TEXT("False"));

	//		task->Abandon();

	//		if (IsInGameThread())
	//		{
	//			//task->Cancel();
	//		}
	//	}
	//}
}

uint32 FPurposeEvaluationThread::Run()
{
	Global::Log(CALLTRACETRIVIAL, PURPOSE, "FEventThread", "Run", TEXT(""));

	while (!stopThread) ///Loop through queues until we decide to stop the thread
	{
		/// We evaluate in a backwards order, as we want each Event evaluation to be fully resolved by the time the next Event is evaluated

		/// Refactor: Purpose Evaluation Queue; Perhaps we can make the evaluation of a thread dependent instead on the order of its potentialPurposeQueues entries?
			/// Then instead of having to specify which layer in each thread, it's dictated by the order the keys are added when the map is setup
			///auto itr = potentialPurposeQueues.CreateIterator();

		FPotentialPurposes purposeToEvaluate;
		if (DequeuePurpose(purposeToEvaluate))
		{
			SelectPurposeIfPossible(purposeToEvaluate);
		}
		FPlatformProcess::Sleep(tickTimer);///Supposedly allowing thread to sleep will help CPU optimize efficiency
	}

	return 0;/// When this point is reached, thread will shutdown
}

bool FPurposeEvaluationThread::SelectPurposeIfPossible(FPotentialPurposes& purposeToEvaluate)
{
	/// Since we have a context to evaluate against, we evaluate every candidate for a new purpose in response to the context
	for (const TObjectPtr<UPurposeAbilityComponent>& candidate : purposeToEvaluate.candidatesForNewPurpose)
	{
		if (!candidate)
		{
			Global::LogError(PURPOSE, "FPurposeEvaluationThread", "SelectPurposeIfPossible", TEXT("Candidate is invalid!"));
			continue;
		}

		/// We do not want instigators reacting to their own occurrences
		if (purposeToEvaluate.subjectMapForPotentialPurposes.subjects.Contains(ESubject::Instigator) && purposeToEvaluate.subjectMapForPotentialPurposes.subjects[ESubject::Instigator] == candidate.Get())
		{
			continue;
		}

		/// We establish the current high score as the high score of the current objective to pre-filter any lesser purposes
		/// Because the CurrentBehavior is stored as a copy it's safe to access
		float highScore = candidate->CurrentBehavior().cachedScoreOfPurpose;
		int highScorePurposeIndex = -1;
		FPurpose highScorePurpose;

		/// Firstly we need to add the candidate to the context subjects
		purposeToEvaluate.subjectMapForPotentialPurposes.subjects.Add(ESubject::Candidate, TScriptInterface<IDataMapInterface>(candidate.Get()));

		/// Now for each candidate, evaluate the context against every potential purpose
		for (const FPurpose& potentialPurpose : purposeCacheForBackgroundThread)
		{
			if (!potentialPurpose.behaviorAbility)
			{
				Global::LogError(PURPOSE, "FPurposeEvaluationThread", "SelectPurposeIfPossible", TEXT("Behavior for purpose %s not selected!")
					, *potentialPurpose.descriptionOfPurpose
				);
				continue;
			}

			/// Design: Purpose Evaluation Log; Solve how to provide the log with a category for each purpose layer
				/// Perhaps it'll need to come from purposeOwner?
				/// Can use the TMap<uint8, TQueue> as to determine which layer we're at
			Global::Log(DATADEBUG, PURPOSE, "FPurposeEvaluationThread", "SelectPurposeIfPossible", TEXT("Purpose: %s")
				, *potentialPurpose.descriptionOfPurpose
			);

			/// Potential score is used to determine whether this purpose will remain above the minimum score of previous purposes
			/// Potential score equals +1 for each condition + an exponential decay additional
			/// More conditions then give purposes a slight advantage that decays so that it doesn't stifle competition against other purposes with less conditions
			float potentialScore = 0.0f;
			///Total weight is used to adjust a condition's score by condition->weight / totalWeight
			///This is so that conditions can be given a user selected weight without having to recalculate other condition->weight for each adjustment
			float totalWeight = 0.0f;

			potentialPurpose.Potential(potentialScore, totalWeight);

			const int totalConditions = potentialPurpose.GetConditions().Num();

			/// the potential score for each condition increases with the number of conditions
			/// when we divide that total potential score by the total number of conditions we get a potential score for each condition
			/// So with 3 conditions, the potential score of each individual is higher than when just 1 condition
			const float individualPotentialScore = potentialScore / totalConditions;

			/// ConditionDetractor is the difference between how much a condition could score and how much it actually scores
			/// By continually adding that difference to a single variable, we can test whether potentialScore - conditionDetractor < min (or the current highest score)
			float conditionDetractor = 0.0f;

			float finalScore = 0.0f;
			Global::Log(DATADEBUG, PURPOSE, "FPurposeEvaluationThread", "SelectPurposeIfPossible", TEXT("Scoring: %s For Candidate: %s. Number Conditions: %d.")
				, *potentialPurpose.descriptionOfPurpose
				, *Cast<UObject>(candidate.Get())->GetName()
				, totalConditions
			);

			/// Now that we are ready to evaluate for the conditions, we will need to combine the data of the context with the data of the subjects
			/// While this will make each data chunk a copy rather than the exact current data from a pointer, the differences in time between queuing and evaluation should be milliseconds
			/// It's a minimal price to pay for the new structure of purpose, where we no longer have to manually root/unroot object pointers for background threads
			TMap<ESubject, TArray<FDataMapEntry>> subjectMapForCondition = purposeToEvaluate.subjectMapForPotentialPurposes.GetSubjectsAsDataMaps();
			subjectMapForCondition.Add(ESubject::Context, purposeToEvaluate.ContextDataForPotentialPurposes);

			for (const TObjectPtr<UCondition> condition : potentialPurpose.GetConditions())
			{
				if ((potentialScore - conditionDetractor) < highScore)///Potential score adjusted by actual condition scores must remain above min
				{
					Global::Log(DATATRIVIAL, PURPOSE, "FPurposeEvaluationThread", "SelectPurposeIfPossible", TEXT("PotentialScore of %s less than min."), *condition->description.ToString());
					finalScore = 0.0f;
					break;
				}

				if (!IsValid(condition))
				{
					Global::Log(DATATRIVIAL, PURPOSE, "FPurposeEvaluationThread", "SelectPurposeIfPossible", TEXT("Purpose->conditions returned an invalid object."));
					conditionDetractor += individualPotentialScore;///Ensure that if this condition can't evaluate it counts against purpose
					continue;
				}

				float score = condition->EvaluateCondition(subjectMapForCondition, candidate);///Get a baseline score for condition

				if (score <= 0 && condition->isRequired)
				{
					finalScore = 0.0f;
					break;
				}

				float curveScore = condition->AdjustToCurve(score);///Adjust score to fit along a curve if present

				/// If we multiply the score adjusted to the curve by the individualPotentialScore
				/// We provide an adjustment to score that results in purposes with more conditions having a slightly higher score potential
				/// This is to mitigate the higher risk of low value conditions and reward complexity of purpose scoring
				float curveScoreAdjusteByIndividualPotential = curveScore * individualPotentialScore;

				/// When we divide the current weight of the condition by the total weight and multiply the score by that, 
				/// We are actually normalizing the the entire purpose's score to its maxpotentialscore / totalweight
				/// While allowing each condition to make up a larger bulk of that score
				float adjustConditionScore = curveScoreAdjusteByIndividualPotential * (condition->weight / totalWeight);

				/// Get the difference between it's potential score by its curve adjusted score (both including weight of condition)
				conditionDetractor += (individualPotentialScore * (condition->weight / totalWeight)) - adjustConditionScore;///if curveScore is < 1, then conditionDetractor will increase

				/// Scores are normalized to their max, so we just add them up for the final score
				finalScore += adjustConditionScore;
				Global::Log(DATATRIVIAL, PURPOSE, "FPurposeEvaluationThread", "SelectPurposeIfPossible", TEXT("Original Score for %s: %f; CurveScore: %f. IndividualPotential: %f. TotalPotential = %f. CurveScoreAdjustedByPotential: %f. Condition->Weight: %f. TotalWeight: %f. TotalDeductionFromPurposeScore: %f. AdjustedConditionScore: %f. Final Score: %f")
					, *condition->description.ToString()
					, score
					, curveScore
					, individualPotentialScore
					, potentialScore
					, curveScoreAdjusteByIndividualPotential
					, condition->weight
					, totalWeight
					, conditionDetractor
					, adjustConditionScore
					, finalScore
				);

				Global::Log(DATADEBUG, PURPOSE, "FPurposeEvaluationThread", "SelectPurposeIfPossible", TEXT("Score for Condition: %s = %f; Potential Score = %f."), *condition->description.ToString(), finalScore, potentialScore);
			}

			Global::Log(DATAESSENTIAL, PURPOSE, "FPurposeEvaluationThread", "SelectPurposeIfPossible", TEXT("Candidate %s. Score of %s is %f. Instigator %s. %s.")
				, *Cast<UObject>(candidate)->GetName()
				, *potentialPurpose.descriptionOfPurpose
				, finalScore
				, purposeToEvaluate.subjectMapForPotentialPurposes.subjects.Contains(ESubject::Instigator) ? *purposeToEvaluate.subjectMapForPotentialPurposes.subjects[ESubject::Instigator].GetObject()->GetFullGroupName(false) : TEXT("Unknown")
				, purposeToEvaluate.subjectMapForPotentialPurposes.subjects.Contains(ESubject::ObjectiveTarget) ? *FString::Printf(TEXT("ObjectiveTarget %s"), *purposeToEvaluate.subjectMapForPotentialPurposes.subjects[ESubject::ObjectiveTarget].GetObject()->GetFullGroupName(false))
				: purposeToEvaluate.subjectMapForPotentialPurposes.subjects.Contains(ESubject::EventTarget) ? *FString::Printf(TEXT("ObjectiveTarget %s"), *purposeToEvaluate.subjectMapForPotentialPurposes.subjects[ESubject::EventTarget].GetObject()->GetFullGroupName(false))
				: TEXT("Unknown Target")
			);

			//contextData->ScoreCache = finalScore;/// Ensure the context has a the score stored for future use
			//return finalScore;

			if (finalScore > highScore)
			{
				/// Ensure new high score is reflected
				highScore = finalScore;
				highScorePurpose = potentialPurpose;
			}
		}

		/// if highScore was set, a purpose was found
		if (highScore > 0)
		{
			/// So now we want to pass the purpose back to the owner and game thread
			FContextData context(
				highScorePurpose
				, purposeToEvaluate.subjectMapForPotentialPurposes
				, purposeToEvaluate.ContextDataForPotentialPurposes
				, candidate
			);

			context.cachedScoreOfPurpose = highScore;

			if (!context.purpose.behaviorAbility) { 
				continue; }/// If the behavior is invalid, we shouldn't be receiving calls to this

			if ( context.HasSubject(ESubject::EventTarget) && candidate->CurrentBehavior().HasSubject(ESubject::EventTarget)
				&& context.Subject(ESubject::EventTarget) == candidate->CurrentBehavior().Subject(ESubject::EventTarget) /// If the target
				&& context.purpose.behaviorAbility == candidate->CurrentBehavior().purpose.behaviorAbility)/// And the ability are both the same
				///Then we don't want to restart behavior, and we don't want it passed back to the Game Thread at all
			{
				Global::Log(DATADEBUG, PURPOSE, "FPurposeEvaluationThread", "SelectPurposeIfPossible", TEXT("Incoming purpose %s already being performed by %s.")
					, *context.purpose.descriptionOfPurpose
					, *candidate->GetFullGroupName(false)
				);
				continue;
			}

			CreateAsyncTask_PurposeSelected(context);
		}
	}

	return false;
}

bool FPurposeEvaluationThread::CreateAsyncTask_PurposeSelected(FContextData context)
{
	TGraphTask<FAsyncGraphTask_PurposeSelected>::CreateTask().ConstructAndDispatchWhenReady(context);
	return true;
}

#pragma endregion

#pragma region Event Thread

FEventThread::~FEventThread()
{
	Global::Log(FULLTRACE, PURPOSE, "FEventThread", "~FEventThread", TEXT(""));
}

#pragma endregion

#pragma region Actor Thread

FActorThread::~FActorThread()
{
	//Global::Log(FULLTRACE, PURPOSE, "FActorThread", "~FActorThread", TEXT(""));
}

#pragma endregion

#pragma region Companion Thread

FCompanionThread::~FCompanionThread()
{
	//Global::Log(FULLTRACE, PURPOSE, "CompanionThread", "~CompanionThread", TEXT(""));
}

#pragma endregion

#pragma region TAsyncGraphTasks

void FAsyncGraphTask_PurposeSelected::PurposeSelected()
{
	Global::Log(FULLTRACE, PURPOSE, "FAsyncGraphTask_PurposeSelected", "PurposeSelected", TEXT("Purpose: %s, IsInGameThread: %s")
		, *contextData.GetName()
		, IsInGameThread() ? TEXT("True") : TEXT("False")
	);

	PurposeSystem::PurposeSelected(contextData);
}

#pragma endregion

bool PurposeSystem::Private::AttemptToPassSelectedPurposeToOwner(const FContextData& selectedPurpose)
{
	return selectedPurpose.purposeOwner->ProvidePurposeToOwner(selectedPurpose);
}


