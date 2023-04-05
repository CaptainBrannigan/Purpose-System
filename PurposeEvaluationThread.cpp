// Copyright Jordan Cain. All Rights Reserved.


#include "Purpose/PurposeEvaluationThread.h"
#include "Purpose/Condition.h"
#include "Purpose/Assets/ObjectiveAsset.h"
#include "Purpose/Director_Level.h"
#include "Purpose/Manager.h"
#include "Purpose/DataChunks/ActorAction.h"
#include "Purpose/DataChunks/TrackedPurposes.h"
#include "Curves/CurveFloat.h"
#include "Purpose/PurposeAbilityComponent.h"
#include "Purpose/Abilities/GA_PurposeBase.h"

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
}

bool FPurposeEvaluationThread::SelectPurposeIfPossible(FPotentialPurposes& purposeToEvaluate)
{
	/// The FPotentialPurposes is created to represent 1 single candidate (the purpose owner of the FPotentialPurposes)
		/// with any number of entries of uniquesubjects that are a combination of that candidate and other subjects desired by the purpose owner who created this FPotentialPurposes
	float highScore = 0;
	int highScorePurposeIndex = -1;
	FPurposeAddress highScorePurposeAddress;
	FSubjectMap highScoreSubjectCombination;
	FPurpose highScorePurpose;

	for (FPotentialPurposeEntry& purpose : purposeToEvaluate.potentialPurposes)
	{
		/// Every unique subject may have n number of combinations with other subjects, so we need to iterate through each
		/// We will score each of these individual combinations for a potential purpose against each other to find the best combination
		for (FSubjectMap& subjectCombination : purpose.mapOfUniqueSubjectEntriesForPurpose)
		{
			/// Firstly we need to combine the subject map of the context with the unique subject entry to present evaluation a single subject map to pull from
			subjectCombination.subjects.Append(purposeToEvaluate.staticSubjectMapForPotentialPurposes.subjects);

			/// Now that we have a single subject map, we can score it against each potential purpose in order to find the best purpose for each combination
			/// The end result desired is to have the best purpose for the best combination of the unique subject
			const FPurpose& potentialPurpose = purpose.purposeToBeEvaluated;

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
			Global::Log(DATADEBUG, PURPOSE, "FPurposeEvaluationThread", "SelectPurposeIfPossible", TEXT("Scoring: %s For Candidate: %s. Context Chain: %s, Number Conditions: %d.")
				, *potentialPurpose.descriptionOfPurpose
				, subjectCombination.subjects.Contains(ESubject::Candidate) ? *subjectCombination.subjects[ESubject::Candidate].GetObject()->GetFullGroupName(false) : TEXT("Invalid")
				, *purposeToEvaluate.DescriptionOfParentPurpose
				, totalConditions
			);

			/// Now that we are ready to evaluate for the conditions, we will need to comine the data of the context with the data of the subjects
			/// While this will make each data chunk a copy rather than the exact current data from a pointer, the differences in time between occurrence and evaluation should be milliseconds
			/// It's a minimal price to pay for the new structure of purpose, where we no longer have to manually root/unroot object pointers for background threads
			TMap<ESubject, TArray<FDataMapEntry>> subjectMapForCondition = subjectCombination.GetSubjectsAsDataMaps();
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

				float score = condition->EvaluateCondition(subjectMapForCondition, purposeToEvaluate.purposeOwner, purposeToEvaluate.uniqueIdentifierOfParent, purposeToEvaluate.addressOfParentPurpose);///Get a baseline score for condition

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
				, subjectCombination.subjects.Contains(ESubject::Candidate) ? *subjectCombination.subjects[ESubject::Candidate].GetObject()->GetFullGroupName(false) : TEXT("Invalid")
				, *potentialPurpose.descriptionOfPurpose
				, finalScore
				, subjectCombination.subjects.Contains(ESubject::Instigator) ? *subjectCombination.subjects[ESubject::Instigator].GetObject()->GetFullGroupName(false) : TEXT("Unknown")
				, subjectCombination.subjects.Contains(ESubject::ObjectiveTarget) ? *FString::Printf(TEXT("ObjectiveTarget %s"), *subjectCombination.subjects[ESubject::ObjectiveTarget].GetObject()->GetFullGroupName(false))
				: subjectCombination.subjects.Contains(ESubject::EventTarget) ? *FString::Printf(TEXT("ObjectiveTarget %s"), *subjectCombination.subjects[ESubject::EventTarget].GetObject()->GetFullGroupName(false))
				: TEXT("Unknown Target")
			);

			//contextData->ScoreCache = finalScore;/// Ensure the context has a the score stored for future use
			//return finalScore;

			if (finalScore > highScore)
			{
				/// Ensure new high score is reflected
				highScore = finalScore;

				highScorePurposeAddress = purpose.addressOfPurpose;

				/// Lastly we store which combination of UniqueSubject + potential purpose scored absolute highest
				highScoreSubjectCombination = subjectCombination;
				highScorePurpose = potentialPurpose;
			}
		}
	}

	/// if highScore was set, a purpose was found
	if (highScore > 0)
	{
		/// So now we want to pass the purpose back to the owner and game thread
		FContextData context(
			highScorePurpose
			, highScoreSubjectCombination
			, purposeToEvaluate.ContextDataForPotentialPurposes
			, purposeToEvaluate.purposeOwner
			, highScorePurposeAddress
			, purposeToEvaluate.DescriptionOfParentPurpose /// This is how we create a chain of purpose names for log debugging purposes
			, purposeToEvaluate.uniqueIdentifierOfParent /// If the FPotentialPurposes had a parent, we need to ensure we pass that ID along to the context
		);
		
		context.cachedScoreOfPurpose = highScore;

		/// We want to check if this potential purpose is already an active purpose
		/// We allow purposes to evaluate prior to a similarity check so as not to affect the scoring process
		/// If we immediately eliminated similar purposes before scoring, we may allow a lesser purpose to be selected when it wouldn't have been
		bool bPurposeAlreadyActive = false;
		int64 IDofActiveContext = 0;
		for (const FContextData& activeContext : purposeToEvaluate.purposeOwner->GetActivePurposes())
		{
			bPurposeAlreadyActive = purposeToEvaluate.purposeOwner->DoesPurposeAlreadyExist(activeContext, context.subjectMap, context.contextData, context.addressOfPurpose);

			if (bPurposeAlreadyActive) 
			{ 
				IDofActiveContext = context.GetContextID();
				break; 
			}
		}

		!bPurposeAlreadyActive ? CreateAsyncTask_PurposeSelected(context) : CreateAsyncTask_ReOccurrence(context.purposeOwner, context.addressOfPurpose, IDofActiveContext);
		return true;
	}
	return false;
}

bool FPurposeEvaluationThread::CreateAsyncTask_PurposeSelected(FContextData& context)
{
	TGraphTask<FAsyncGraphTask_PurposeSelected>::CreateTask().ConstructAndDispatchWhenReady(context);
	return true;
}

bool FPurposeEvaluationThread::CreateAsyncTask_ReOccurrence(TScriptInterface<IPurposeManagementInterface> owner, const FPurposeAddress addressOfPurpose, const int64 outUniqueIDofActivePurpose)
{
	TGraphTask<FAsyncGraphTask_ReOccurrence>::CreateTask().ConstructAndDispatchWhenReady(addressOfPurpose, outUniqueIDofActivePurpose);
	return true;
}

#pragma endregion

#pragma region Event Thread

FEventThread::~FEventThread()
{
	Global::Log(FULLTRACE, PURPOSE, "FEventThread", "~FEventThread", TEXT(""));
}

uint32 FEventThread::Run()
{
	Global::Log(CALLTRACETRIVIAL, PURPOSE, "FEventThread", "Run", TEXT(""));

	while (!stopThread) ///Loop through queues until we decide to stop the thread
	{
		/// We evaluate in a backwards order, as we want each Event evaluation to be fully resolved by the time the next Event is evaluated
		
		/// Refactor: Purpose Evaluation Queue; Perhaps we can make the evaluation of a thread dependent instead on the order of its potentialPurposeQueues entries?
			/// Then instead of having to specify which layer in each thread, it's dictated by the order the keys are added when the map is setup
			///auto itr = potentialPurposeQueues.CreateIterator();

		FPotentialPurposes purposeToEvaluate(FPurposeAddress(), 0);
		if (DequeuePurpose((uint8)EPurposeLayer::Objective, purposeToEvaluate))
		{
			SelectPurposeIfPossible(purposeToEvaluate);
		}
		else if (DequeuePurpose((uint8)EPurposeLayer::Goal, purposeToEvaluate))
		{
			SelectPurposeIfPossible(purposeToEvaluate);
		}
		else if (DequeuePurpose((uint8)EPurposeLayer::Event, purposeToEvaluate))
		{
			SelectPurposeIfPossible(purposeToEvaluate);
		}
		FPlatformProcess::Sleep(tickTimer);///Supposedly allowing thread to sleep will help CPU optimize efficiency
	}

	return 0;/// When this point is reached, thread will shutdown
}

void FEventThread::Exit()
{
	FPurposeEvaluationThread::Exit();
	//Global::Log(FULLTRACE, PURPOSE, "EventThread", "Exit", TEXT(""));
}

#pragma endregion

#pragma region Actor Thread

FActorThread::~FActorThread()
{
	//Global::Log(FULLTRACE, PURPOSE, "FActorThread", "~FActorThread", TEXT(""));
}

uint32 FActorThread::Run()
{
	while (!stopThread) ///Loop through queues until we decide to stop the thread
	{
		//Global::Log(FULLTRACE, PURPOSE, "FActorThread", "Run", TEXT("ReactionQueue: %s."), ReactionQueue.IsEmpty() ? TEXT("Is Empty") : TEXT("Is not Empty"));
		//Global::Log(FULLTRACE, PURPOSE, "FActorThread", "Run", TEXT("AbilitiesQueue: %s."), AbilitiesQueue.IsEmpty() ? TEXT("Is Empty") : TEXT("Is not Empty"));

		FPotentialPurposes purposeToEvaluate(FPurposeAddress(), 0);
		if (DequeuePurpose((uint8)EPurposeLayer::Behavior, purposeToEvaluate))
		{
			SelectPurposeIfPossible(purposeToEvaluate);
		}
	/*	else if (DequeuePurpose((uint8)EPurposeLayer::Reaction, purposeToEvaluate))
		{
			SelectPurposeIfPossible(purposeToEvaluate);
		}*/

		FPlatformProcess::Sleep(tickTimer);///Supposedly allowing thread to sleep will help CPU optimize efficiency
	}

	return 0;/// When this point is reached, thread will shutdown
}

void FActorThread::Exit()
{
	FPurposeEvaluationThread::Exit();
	//Global::Log(FULLTRACE, PURPOSE, "FActorThread", "Exit", TEXT(""));
}

#pragma endregion

#pragma region Companion Thread

FCompanionThread::~FCompanionThread()
{
	//Global::Log(FULLTRACE, PURPOSE, "CompanionThread", "~CompanionThread", TEXT(""));
}

uint32 FCompanionThread::Run()
{
	//Global::Log(FULLTRACE, PURPOSE, "FCompanionThread", "Run", TEXT(""));

	while (!stopThread) ///Loop through queues until we decide to stop the thread
	{
		/// We evaluate in a backwards order, as we want each Event evaluation to be fully resolved by the time the next Event is evaluated
		
		FPotentialPurposes purposeToEvaluate(FPurposeAddress(), 0);
		if (DequeuePurpose((uint8)EPurposeLayer::Behavior, purposeToEvaluate))
		{
			SelectPurposeIfPossible(purposeToEvaluate);
		}
		else if (DequeuePurpose((uint8)EPurposeLayer::Objective, purposeToEvaluate))
		{
			SelectPurposeIfPossible(purposeToEvaluate);
		}
		else if (DequeuePurpose((uint8)EPurposeLayer::Goal, purposeToEvaluate))
		{
			SelectPurposeIfPossible(purposeToEvaluate);
		}
		else if (DequeuePurpose((uint8)EPurposeLayer::Event, purposeToEvaluate))
		{
			SelectPurposeIfPossible(purposeToEvaluate);
		}

		FPlatformProcess::Sleep(tickTimer);///Supposedly allowing thread to sleep will help CPU optimize efficiency
	}

	return 0;/// When this point is reached, thread will shutdown
}

void FCompanionThread::Exit()
{
	FPurposeEvaluationThread::Exit();
	//Global::Log(FULLTRACE, PURPOSE, "FCompanionThread", "Exit", TEXT(""));
}

#pragma endregion

#pragma region TAsyncGraphTasks

void FAsyncGraphTask_PurposeSelected::PurposeSelected()
{
	Global::Log(FULLTRACE, PURPOSE, "FAsyncGraphTask_PurposeSelected", "PurposeSelected", TEXT("Purpose: %s, IsInGameThread: %s")
		, *contextData.chainedPurposeName
		, IsInGameThread() ? TEXT("True") : TEXT("False")
	);

	PurposeSystem::PurposeSelected(contextData);
}

void FAsyncGraphTask_ReOccurrence::ReOccurrence()
{
	Global::Log(FULLTRACE, PURPOSE, "FAsyncGraphTask_ReOccurrence", "ReOccurrence", TEXT("IsInGameThread: %s"),
		IsInGameThread() ? TEXT("True") : TEXT("False")
	);

	if (levelDirector.IsValid())
	{
		levelDirector->ReOccurrenceOfEventObjectives(indexOfEvent);
	}
	else
	{
		Global::LogError(PURPOSE, "FAsynceGraphTask_ReOccurrence", "ReOccurrence", TEXT("Level Director invalid!"));
	}
}

#pragma endregion

