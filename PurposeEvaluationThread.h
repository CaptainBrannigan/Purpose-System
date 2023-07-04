// Copyright Jordan Cain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Public/HAL/Runnable.h"
#include "GlobalLog.h"
#include "Data.h"
#include "Purpose/Condition.h"
#include "UObject/Interface.h"
#include "DataMapInterface.h"
#include "Misc/Timespan.h"
#include "PurposeEvaluationThread.generated.h"

#pragma region PurposeSystem

class UPurposeAbilityComponent;

UENUM(BlueprintType)//"BlueprintType" is essential to include
/// Utilized by managers to determine how to handle various states of Purpose
enum class EPurposeState : uint8
{
	None UMETA(DisplayName = "None") ///Default to ensure data is set with HasData() for enums
	, Ongoing/// The purpose has not yet been completed
	, Complete
	, Ending/// This purpose is in the process of being ended and cleaned up
};

UENUM(BlueprintType)
enum class EPurposeSelectionEvent : uint8
{
	None/// Default to ensure a value is actually selected or not
	, OnSelected
	, OnFinished
};

USTRUCT()
struct LYRAGAME_API FPurposeModificationEntry
{
	GENERATED_BODY()

		UPROPERTY(EditAnywhere)
		/// A specifier indicating when to make an adjustment
		FText description;

	UPROPERTY(EditAnywhere)
		/// A specifier indicating when to make an adjustment
		EPurposeSelectionEvent selectionEvent = EPurposeSelectionEvent::None;

	UPROPERTY(EditAnywhere, DisplayName = "Target of Adjusted Data")
		/// 
		ESubject subjectToAdjust = ESubject::None;

	UPROPERTY(Instanced, EditAnywhere, meta = (ShowInnerProperties))
		/// These DataChunks will either be adjusted or created 
		/// Adjustments will be determined by enum selection provided by DataChunk
		TObjectPtr<UDataChunk> dataAdjustment = nullptr;

};

/// Refactor: Purpose; How can we retain purpose assets while allowing instanced versions?
	/// Can we separate the core functionality of purpose assets, which is linking a set of conditions representing a layer of purpose
	/// So in an AI Activity, it would be convenient to have the option to directly create purpose layers without using Purpose Assets
	/// So how do we work sequentially on a purpose hierarchy?
		/// Occurrence happens. We gather all the Event assets 
			/// Each asset holds a struct which contains the entire hierarchy, from Event to Behavior
			/// We want to be able to evaluate each layer in a specific order, not necessarily within that Hierarchy
				/// For example, Objectives have highest priority. Until all Objectives are evaluated, we do not evaluate Goals or Events.
				/// So if each layer is the same struct, how can we differentiate for queueing? 
					/// It would need to account for index. Perhaps we could just translate the index into a uint8?
					/// OR we could use a TMap for the asset, and pass in only the tpair to the evaluation thread
					/// Except we can neither use the Index nor a tmap because we want to have multiple sub purposes, so it needs to spiderweb
					/// We could use the class of the ContextData rather than the class of the data asset
				///So we can evaluate in order. Now in order to evaluate, we need potential purposes
					/// Currently his is stored on the Context Data. It calls the ParentPurposeAsset->SubPurposes
					/// What do we need?
						/// A context data
							/// With a unique candidate
							/// And unique targets per unique candidate
						/// potential purposes
						/// The context data is created for each layer per candidate as a duplicate of the parent layer
						/// So we want to send potential purposes, with the same context but different candidate to the background thread
						/// And each potential purpose needs to evaluate against one another per unique target per unique candidate
						/// So we have a concept of unique subjects for a static context data
							/// Could we create a layered UniqueSubjects struct that we evaluate alongside the static subjects of the context data?
							/// It could work the same was as the purpose hierarchy, where each struct has an array of sub structs
							/// So we pass in the Conditions, Potential Purposes, Static Subjects, and Unique Subjects
							/// So for each potential purpose ( the number of potential purposes never changes)
								/// For each Condition (the number of conditions never changes)
									/// For each UniqueSubject
										/// This will need to be where recursion happens
										/// For every UniqueSubject, it may have it's own UniqueSubjects added on
											/// Every sub UniqueSubject creates a unique SubjectMap with the parent
											/// So we could create an array for each SubjectMap, where each base recursive call is a unique entry, then loop through that with the StaticSubjectMap
												/// So say we have 3 candidates with X number of potential targets
													/// Candidate1-Target1
													/// Candidate1-Target2
													/// Candidate1-Target3-Subject1
													/// Candidate1-Target3-Subject2
														/// Evaluate for PotentialPurpose + Conditions + StaticSubjects + Each UniqueSubjectEntry
													/// Candidate2-Target1
														/// Evaluate for Candidate2 
													/// Candidate3
														/// Evaluate for Candidate3 
										/// So for each UniqueSubject
											///	We return an array of FSubjectMap
											/// We then append the StaticSubjects to that, and evaluate
											/// So the evaluation should actually work as
												/// For each UniqueSubject
													/// For each SubjectMap returned + StaticSubjects
														/// For each potential purpose
															/// Evaluate against Conditions
													/// Then we return the SubjectMap+Purpose as the context 
										/// Can we somehow make a "Subject Request" on a purpose?
											/// This could allow us to no longer require unique callback functions for each type of context
											/// As the same with the purpose hierarchy and purpose address, it could be a layered SubjectRequest
												/// So we could make a base SubjectRequest of candidate
													/// Then as an additional layer to tie to that SubjectRequest, we also request objectivetarget
												/// finding the correct subjects could then be handled by a switch statement
												/// Then these would be returned as UniqueSubject layers
				/// When finished the ContextData is used to pass the data back
		/// A couple miscellaneous thoughts on Purpose system
			/// How about if only Objectives selected for an actor trigger Occurrences?
			/// How about if ObjectiveTargets always become EventTarget
				/// This could be handled normally for Objectives, but also by Reaction Objectives
			/// What about ObjectiveTargeting?
				/// This actually sort of breaks the purpose of the PurposeSystem
				/// The purpose system is in place to provide a reactionary purpose to every action
				/// So theoretically there should be an Objective for each actor as a target who performs an action
				/// Which would mean each EventTarget is an ObjectiveTarget
				/// However, this also makes it so there isn't necessarily an overarching umbrella of purpose
					/// As in there isn't one goal in which many actors participate
					/// In the above system it would be best to remove Goals and simply have an Objective to an Event
					/// Which would also abandon the goal relationships unfortunately
					/// Would also detract from an overarching sense of purpose. How would AI be doing anything other than reacting?
					/// Which could also be another system actually. If every action is a reaction (I'm hungry so I eat), that could give more realistic sense of purpose.
						/// Example: Event: Here's a job: AI needs purpose so they start the job. Event: Player shoots at AI; AIs need to survive is greater than it's sense of purpose so it now chooses to react to player

USTRUCT(BlueprintType)
struct LYRAGAME_API FPurpose
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, meta = (DisplayPriority = "0"))
	FString descriptionOfPurpose = "";

	//UPROPERTY(EditAnywhere, meta=(InlineEditConditionToggle, EditConditionHides, EditCondition = "!canEditSubPurposes"))
	//TObjectPtr<class UBehaviorTree> BT = nullptr;
	UPROPERTY(Instanced, EditAnywhere, meta = (DisplayPriority = "1"))
	TObjectPtr<class UBehavior_AI> behaviorAbility = nullptr;

	UPROPERTY(Instanced, EditAnywhere, meta = (TitleProperty = "Criteriia for Purpose Selection", ShowInnerProperties))
		/// These conditions establish how a purpose fits to a context
		/// They are critical to create legible and realistic purpose for both individual actors and groups of actors
		/// The more conditions present, the greater the potential weight of a purpose
	TArray<TObjectPtr<UCondition>> conditions;
	const TArray<TObjectPtr<UCondition>>& GetConditions() const { return conditions; }

	UPROPERTY(EditAnywhere, meta = (TitleProperty = "description"))
		/// These DataChunks will either be adjusted or created 
		/// Adjustments will be determined by enum selection provided by DataChunk
	TArray<FPurposeModificationEntry> dataAdjustmentsForPurposeEvents;

	/// @param potentialScore = every condition with a exponential decay additional (1 + decaying additional)
	/// @param totalWeight = The weight of each condition->weight added together
	void Potential(float& potentialScore, float& totalWeight) const
	{
		float num = conditions.Num();
		if (num == 0) { return; }

		for (float i = 1.0f; i <= num; ++i)
		{
			float x = (1.0f / i);

			potentialScore += FMath::Pow(i, x);///For every condition, there is a diminishing addition to a base 0-1 score
			///This means that purposes with more conditions will have a higher score potential than those with less
			///Allowing us to prioritize purposes that meet more conditions without ruining the potential of purposes with less conditions
			totalWeight += conditions[i - 1]->weight;///i-1 as the above utilizes index as position in array
		}
	}

	const TObjectPtr<UCondition> ConditionData(int iteration) const
	{
		if (conditions.IsValidIndex(iteration))
		{
			return conditions[iteration];
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("%d is not a valid index for conditions. EventAsset::ConditionData()"), iteration);
		}

		return nullptr;
	}

	const TArray<FPurposeModificationEntry>& DataAdjustments() const { return dataAdjustmentsForPurposeEvents; }
};

USTRUCT(BlueprintType)
struct LYRAGAME_API FSubjectMap
{
	GENERATED_BODY()
public:

	FSubjectMap() {}

	FSubjectMap(TMap<ESubject, TScriptInterface<IDataMapInterface>> inSubjects)
		: subjects(inSubjects)
	{

	}

	UPROPERTY(VisibleAnywhere)
	///Link an enum representing a subject with a corresponding object which holds DataChunks
	TMap<ESubject, TScriptInterface<IDataMapInterface>> subjects;
	
	TMap<ESubject, TArray<FDataMapEntry>> GetSubjectsAsDataMaps() const
	{
		TMap<ESubject, TArray<FDataMapEntry>> SubjectDataMap;

		for (TPair<ESubject, TScriptInterface<IDataMapInterface>> subject : subjects)
		{
			if (!IsValid(subject.Value.GetObject()))
			{
				Global::LogError(PURPOSE, "FContextData", "GetSubjectAsDataMaps", TEXT("Subject %s value is invalid!."), *Global::EnumValueOnly<ESubject>(subject.Key));
				continue;
			}

			SubjectDataMap.Add(subject.Key, subject.Value->DataMapCopy());
		}

		return SubjectDataMap;
	}
};

USTRUCT(BlueprintType)
struct LYRAGAME_API FContextData
{
	GENERATED_BODY()

#pragma region ContextData
public:

	FContextData() {}

	FContextData(FPurpose inPurpose, FSubjectMap inSubjectMap, TArray<FDataMapEntry> inContextData, TWeakObjectPtr<UPurposeAbilityComponent> inPurposeOwner)
		: purpose(inPurpose)
		, subjectMap(inSubjectMap)
		, contextData(inContextData)
		, purposeOwner(inPurposeOwner)
	{
		contextDataPurposeName = purpose.descriptionOfPurpose + "(" + GetOwnerName() + ")";
	}

	FPurpose purpose;

	/// We store the score of the purpose at the time of it's selection so that we may easily compare purposes against each other outside of purpose selection for an individual
	float cachedScoreOfPurpose = 0;

	/// Essentially this is the context
	/// Every context will store relevant ESubjects with their data maps to be evaluated against Conditions
	FSubjectMap subjectMap;

	/// Refactor: purpose PurposeEvaluation; We can actually combine the contextData and FSubjectMap by turning the FSubjectMap into a TMap<ESubject, FDataMapEntry> at evaluation time
	/// We can store data specific to the context and not a subject here, such as a last known position, a type of sound heard, etc.
	TArray<FDataMapEntry> contextData;

	TWeakObjectPtr<UPurposeAbilityComponent> purposeOwner = nullptr;

	inline bool ContextIsValid() const { return purpose.GetConditions().Num() > 0; }

	//inline bool HasPurpose() { return purpose.SubPurposes.Num() > 0 || purpose.BehaviorAbility; }
	inline bool HasPurpose() const { return purpose.conditions.Num() > 0; }

	FString GetOwnerName();

	///@return FString: If a purpose has been found for this contextData, returns the name of the purpose itself
	FORCEINLINE FString GetName() const
	{
		return contextDataPurposeName;
	}

	FString Description() const
	{
		FString name = GetName();
		//Global::Log(DATADEBUG, PURPOSE, "FContextData", "Description", TEXT("Description of %s."), *name);
		for (const TPair<ESubject, TScriptInterface<IDataMapInterface>>& subject : subjectMap.subjects)
		{
			//Global::Log(DATADEBUG, PURPOSE, "FContextData", "Description", TEXT("Subject: %s Value: %s. FContextData::Description"), *UEnum::GetValueAsString(subject.Key), *subject.Value.GetObject()->GetName());
			if (!subject.Value.GetObject())
			{
				/*Global::Log(DATADEBUG, PURPOSE, "FContextData", "Description", TEXT("Subject invalid! %s: Subject: %s. Actor: %s.")
					, *name
					, *Global::EnumValueOnly<ESubject>(subject.Key)
					, IsValid(subject.Value.GetObject()) ? *subject.Value.GetObject()->GetName() : TEXT("Invalid")
				);*/
				name += LINE_TERMINATOR;
				name += FString::Printf(TEXT("Subject invalid! Subject: %s. Actor: %s.")
					, *Global::EnumValueOnly<ESubject>(subject.Key)
					, IsValid(subject.Value.GetObject()) ? *subject.Value.GetObject()->GetName() : TEXT("Invalid")
				);
				continue;
			}
			for (const FDataMapEntry& dataChunk : subject.Value->DataMap())
			{
				/*Global::Log( DATADEBUG, PURPOSE, "FContextData", "Description", TEXT("%s: Subject: %s. Actor: %s. DataChunk: %s.")
					, *name
					, *Global::EnumValueOnly<ESubject>(subject.Key)
					, IsValid(subject.Value.GetObject()) ?  *subject.Value.GetObject()->GetName() : TEXT("Invalid")
					, IsValid(dataChunk.Chunk) ? *dataChunk.Chunk->Description() : TEXT("Invalid")
				);	*/
				name += LINE_TERMINATOR;
				name += FString::Printf(TEXT("Subject: %s. Actor: %s. DataChunk: %s.")
					, *Global::EnumValueOnly<ESubject>(subject.Key)
					, IsValid(subject.Value.GetObject()) ? *subject.Value.GetObject()->GetName() : TEXT("Invalid")
					, IsValid(dataChunk.Chunk) ? *dataChunk.Chunk->Description() : TEXT("Invalid")
				);
			}
		}

		return *name;
	}

	/// This could be unnecessarily expensive, and if we aren't actually going to log it no reason to bother
	FString Description(EHierarchicalCalltraceVerbosity verbosity)
	{
		const UGlobalLogSettings* LogSettings = GetDefault<UGlobalLogSettings>();

		if (!LogSettings)
		{
			return "";
		}

		if (static_cast<int>(LogSettings->GlobalLogVerbosity) < static_cast<int>(verbosity))
		{
			//		UE_LOG(LogTemp, Log, TEXT("HV: %d ; V: %d"), static_cast<int>(HierarchicalVerbosity), static_cast<int>(verbosity));
			return "";
		}///Verbosity is too high, so we don't want to log

		FString name = GetName();
		//Global::Log(DATADEBUG, PURPOSE, "FContextData", callLocation, TEXT("Description of %s."), *name);
		for (const TPair<ESubject, TScriptInterface<IDataMapInterface>>& subject : subjectMap.subjects)
		{
			//Global::Log(DATADEBUG, PURPOSE, "FContextData", callLocation, TEXT("Subject: %s Value: %s. FContextData::Description"), *UEnum::GetValueAsString(subject.Key), *subject.Value.GetObject()->GetName());
			if (!subject.Value.GetObject())
			{
				/*Global::Log(DATADEBUG, PURPOSE, "FContextData", "Description", TEXT("Subject invalid! %s: Subject: %s. Actor: %s.")
					, *name
					, *Global::EnumValueOnly<ESubject>(subject.Key)
					, IsValid(subject.Value.GetObject()) ? *subject.Value.GetObject()->GetName() : TEXT("Invalid")
				);*/
				name += LINE_TERMINATOR;
				name += FString::Printf(TEXT("Subject invalid! Subject: %s. Actor: %s.")
					, *Global::EnumValueOnly<ESubject>(subject.Key)
					, IsValid(subject.Value.GetObject()) ? *subject.Value.GetObject()->GetName() : TEXT("Invalid")
				);
				continue;
			}
			for (const FDataMapEntry& dataChunk : subject.Value->DataMap())
			{
				/*Global::Log( DATADEBUG, PURPOSE, "FContextData", "Description", TEXT("%s: Subject: %s. Actor: %s. DataChunk: %s.")
					, *name
					, *Global::EnumValueOnly<ESubject>(subject.Key)
					, IsValid(subject.Value.GetObject()) ?  *subject.Value.GetObject()->GetName() : TEXT("Invalid")
					, IsValid(dataChunk.Chunk) ? *dataChunk.Chunk->Description() : TEXT("Invalid")
				);	*/
				name += LINE_TERMINATOR;
				name += FString::Printf(TEXT("Subject: %s. Actor: %s. DataChunk: %s.")
					, *Global::EnumValueOnly<ESubject>(subject.Key)
					, IsValid(subject.Value.GetObject()) ? *subject.Value.GetObject()->GetName() : TEXT("Invalid")
					, IsValid(dataChunk.Chunk) ? *dataChunk.Chunk->Description() : TEXT("Invalid")
				);
			}
		}
		name += LINE_TERMINATOR;/// So that the calling method is shown below rather than after last line

		return *name;
	}

	///The name adjusted to represent the selected purpose
	FString contextDataPurposeName = "contextData";

private:

#pragma endregion

#pragma region Subjects
public:

	TObjectPtr<UObject> Subject(ESubject inSubject) const
	{
		if (HasSubject(inSubject))
		{
			return subjectMap.subjects[inSubject].GetObject();
		}
		else
		{
			Global::Log(DATADEBUG, PURPOSE, "FContextData", "Subject", TEXT("Subject %s is not contained."), *Global::EnumValueOnly<ESubject>(inSubject));
		}

		return nullptr;
	}

	template<class T>
	TObjectPtr<T> Subject(ESubject inSubject) const
	{
		return Cast<T>(Subject(inSubject));
	}

	///Returns the data map interface of a given subject
	TScriptInterface<IDataMapInterface> DataMapInterfaceForSubject(ESubject inSubject) const
	{
		if (HasSubject(inSubject))
		{
			return subjectMap.subjects[inSubject];
		}
		else
		{
			Global::Log(DATADEBUG, PURPOSE, "FContextData", "DataMapInterfaceForSubject", TEXT("Subject %s is not contained or invalid."), *Global::EnumValueOnly<ESubject>(inSubject));
		}

		return nullptr;
	}

	bool HasSubject(ESubject inSubject) const
	{
		return subjectMap.subjects.Contains(inSubject) && subjectMap.subjects[inSubject] && IsValid(subjectMap.subjects[inSubject].GetObject());
	}

	bool HasData(ESubject inSubject, TSubclassOf<UDataChunk> inType) const
	{
		if (HasSubject(inSubject))
		{
			return DataMapInterfaceForSubject(inSubject)->HasData(inType);
		}

		return false;
	}

	///Add a DataMapInterface object to a given subject
	///@Param allowSwap == true: If there is already a DataMapInterface object linked to subject, replace
	bool AddSubject(ESubject inSubject, TScriptInterface<IDataMapInterface> inDataObject, bool allowSwap = false)
	{
		if (!IsValid(inDataObject.GetObject()))
		{
			Global::LogError(PURPOSE, "FContextData", "AddSubject", TEXT("Attempting to add null data for type: %s."), *Global::EnumValueOnly<ESubject>(inSubject));
			return false;
		}

		if (HasSubject(inSubject))
		{
			if (allowSwap)
			{
				subjectMap.subjects[inSubject] = inDataObject;
				return true;
			}

			return false;
		}
		else
		{
			subjectMap.subjects.Add(inSubject, inDataObject);
			return true;
		}
	}

	bool RemoveSubject(ESubject inSubject)
	{
		if (HasSubject(inSubject))
		{
			subjectMap.subjects.Remove(inSubject);
			return true;
		}
		else { return true; }

		return false;
	}

	template<class DataChunkClass>///<T> Must be a UDataChunk subclass
	///Get DataChunk of T from ESubject
	DataChunkClass* DataChunk(ESubject subject)
	{
		if (DataChunkClass* dataChunk = DataMapInterfaceForSubject(subject)->DataChunk<DataChunkClass>())
		{
			return dataChunk;
		}
		else
		{

			Global::LogError(PURPOSE, "FContextData", "DataChunk", TEXT("Object of %s for subject %s is nullptr, returning empty new object. FContextData::DataChunk<T>(ESubject)"), *DataChunkClass::StaticClass()->GetName(), *UEnum::GetValueAsString(subject));
			return NewObject<DataChunkClass>();
		}

		return nullptr;
	}

	///Get DataChunk from ESubject, TSubclassOf is used to find the datachunk in TMap
	const TObjectPtr<UDataChunk> DataChunk(ESubject subject, TSubclassOf<UDataChunk> inType)
	{
		//If the data isn't contained, the value remains a 0
		if (HasData(subject, inType))
		{
			return DataMapInterfaceForSubject(subject)->DataChunk(inType);
		}

		return nullptr;
	}

	bool AdjustData(ESubject target, const TObjectPtr<UDataChunk>& adjustmentChunk) const
	{
		if (adjustmentChunk)
		{
			if (HasSubject(target))
			{
				if (DataMapInterfaceForSubject(target)->HasData(adjustmentChunk->GetClass()))/// If we have the data already
				{
					Global::Log(DATATRIVIAL, PURPOSE, GetName(), "AdjustData", TEXT("Adjusting %s by %d."), *adjustmentChunk->GetClass()->GetName(), (int)adjustmentChunk->DataModifier());
					DataMapInterfaceForSubject(target)->DataChunk(adjustmentChunk->GetClass())->AdjustData(adjustmentChunk->DataModifier());/// Adjust the data by the specified modifier
				}
				else/// Otherwise create the data chunk
				{
					Global::Log(DATATRIVIAL, PURPOSE, GetName(), "AdjustData", TEXT("Creating DataChunk %s with modification %d."), *adjustmentChunk->GetClass()->GetName(), (int)adjustmentChunk->DataModifier());
					DataMapInterfaceForSubject(target)->AddData(NewObject<UDataChunk>(Subject(target), adjustmentChunk->GetClass())->AdjustData(adjustmentChunk->DataModifier()));/// And still apply the modification requested
				}
				return true;
			}
			else if (target == ESubject::Context)/// As the subject map and context data are held separately, we have to have a separate case for when we try to adjust the Context subject
			{
				if (DataMapGlobals::HasData(contextData, adjustmentChunk->GetClass()))
				{
					DataMapGlobals::DataChunk(contextData, adjustmentChunk->GetClass())->AdjustData(adjustmentChunk->DataModifier());/// Adjust the data by the specified modifier
				}
				else/// Otherwise create the data chunk
				{
					Global::LogError(PURPOSE, GetName(), "AdjustData", TEXT("Cannot create DataChunk %s with modification %d for the context as we have no outer!"), *adjustmentChunk->GetClass()->GetName(), (int)adjustmentChunk->DataModifier());
					//DataMapInterfaceForSubject(target)->AddData(NewObject<UDataChunk>(Get, adjustmentChunk->GetClass())->AdjustData(adjustmentChunk->DataModifier()));/// And still apply the modification requested
				}
				return true;
			}
		}

		Global::LogError(PURPOSE, GetName(), "AdjustData", TEXT("DataAdjustment for %s and subject %s %s")
			, *GetName()
			, *Global::EnumValueOnly<ESubject>(target)
			, HasSubject(target) ? TEXT("has an invalid DataAdjustment chunk!") : TEXT("does not contain subject!")
		);
		
		return false;
	}

	/// Will perform Purpose's DataAdjustements as appropriate
	/// @param inEventTypeToAdjust: Determines what data can be adjusted based on selection made in Asset
	/// @param inLogCat: Allows us to clarify where adjustment is coming from in fail case
	/// @param callingMethodName: Allows us to clarify where adjustment is coming from in fail case
	/// @param source: Allows us to clarify where adjustment is coming from in fail case
	/// @param sourceName: Allows us to clarify where adjustment is coming from in fail case
	void AdjustDataIfPossible(const TArray<FPurposeModificationEntry>& dataAdjustments, EPurposeSelectionEvent inEventTypeToAdjust, const FLogCategoryBase& inLogCat, FString callingMethodName, TObjectPtr<UObject> source = nullptr, FString sourceName = "") const
	{
		for (const FPurposeModificationEntry& chunk : dataAdjustments)/// For every piece of data we want to adjust based on the selected purpose
		{
			if (IsValid(chunk.dataAdjustment) && chunk.selectionEvent == inEventTypeToAdjust)
			{
				AdjustData(chunk.subjectToAdjust, chunk.dataAdjustment.Get());
			}
			else
			{
				/// Only log if the DataAdjustment was invalid
				if (!IsValid(chunk.dataAdjustment))
				{
					source ?
						Global::LogError(inLogCat, *source, callingMethodName, TEXT("DataAdjustment for %s has an invalid DataAdjustment chunk!"), *GetName())
						: Global::LogError(inLogCat, sourceName, callingMethodName, TEXT("DataAdjustment for %s has an invalid DataAdjustment chunk!"), *GetName());
				}
			}
		}
	}

#pragma endregion

public:
	//FORCEINLINE bool operator ==(const FContextData& other) const
	//{
	//	return addressOfPurpose == other.addressOfPurpose && uniqueIdentifier == other.uniqueIdentifier;
	//}
};
/*
FORCEINLINE uint32 GetTypeHash(const FContextData& b)
{
	return FCrc::MemCrc32(&b, sizeof(FContextData));
}*/

USTRUCT(BlueprintType)
struct LYRAGAME_API FPotentialPurposes
{
	GENERATED_BODY()
public:

	/// These are all the candidates we wish to evaluate for a new purpose against tthe provided context
	TArray<TObjectPtr<UPurposeAbilityComponent>> candidatesForNewPurpose;

	/// Subject map for the potential purposes to evaluate against
	FSubjectMap subjectMapForPotentialPurposes;

	/// This is the 1 UniqueSubject for which this FPotentialPurpose exists
	TWeakObjectPtr<UPurposeAbilityComponent> purposeOwner = nullptr;

	/// Context data for the potential purposes to evaluate against
	TArray<FDataMapEntry> ContextDataForPotentialPurposes;
};

///Umbrella type for multiple queues of UContextData_Deprecated
typedef TQueue<FPotentialPurposes> PurposeQueue;

class FAsyncGraphTask_PurposeSelected;

/// <summary>
///The purpose evaluation thread is the foundation of all gameplay logic
///Receiving a context data, the thread will compare that context data to a relevant layer of purpose
///If a purpose is found, it is then sent back to the owner
///
///As such, a large number of calculations must be performed on a large number of queued context datas
///While there is no guarantee of timely evaluation, it allows a huge number of purposes to be evaluated and selected without locking up the gamethread
///This allows the Event System to be very robust, allowing not only consideration of each action each actor makes
///But also allowing all other actors to potentially react to that action
/// </summary>
class FPurposeEvaluationThread : public FRunnable
{
public:

	FPurposeEvaluationThread(const TArray<FPurpose>& inPurposeCache, const TArray<TObjectPtr<UPurposeAbilityComponent>>& inCandidateCache)
		: purposeCacheForBackgroundThread(inPurposeCache)
		, candidateCache(inCandidateCache)
	{

	}

	///Controls while loop execution of Run() 
	bool stopThread = true;

	///Essentially the speed that the background thread will call Run(); thanks to FPlatformMisc::Sleep()
	float tickTimer = 0.05f;

	/// Design: BackgroundThread Purpose; to implement a pause using FRunnable::Suspend
		/// halt any evaluation regardless of status
		/// Throw context data into a tgraphtask to re-add to it's queue

	///Virtual methods from FRunnable
	bool Init() final;
	void Stop() final;
	virtual void Exit() override;

	///Executed so long as Init() returns true
	///Runs Recursively to evaluate queues until thread is told to stop and/or shutdown
	uint32 Run() override;

	//void AddCandidatesToThreadCache(TArray<TObjectPtr<UPurposeAbilityComponent>> inCandidates)
	//{
	//	candidateCache.Append(inCandidates);
	//}

	///@return bool: True when the purpose was stored to a queue to be evaluated at some point
	bool QueuePurpose(FPotentialPurposes potentialPurposesToQueue)
	{
		if(potentialPurposeQueue.Enqueue(potentialPurposesToQueue))
		{
			/// Since TQueue doesn't have a .Num()
			numQueueItems++;/// ensure this thread has an increased number of queue items
			return true;
		}

		return false;
	}

	/// @param dequeudPurpose; TQueue requires an out param to dequeue to
	///@return bool: False only when a purpose was not dequeued
	bool DequeuePurpose(FPotentialPurposes& dequeuedPurpose)
	{
		if (potentialPurposeQueue.Dequeue(dequeuedPurpose))
		{
			/// Since TQueue doesn't have a .Num()
			numQueueItems--;/// ensure this thread has a decreased number of queue items
			return true;
		}

		return false;
	}

	/// @param purposeToEvaluate; The combination of context, subjects and potential purposes to evaluate to a single purpose for a unique subject. After evaluation, the data may be copied to further the purpose system, but this struct will be destroyed regardless.
	///@return bool: 
	bool SelectPurposeIfPossible(FPotentialPurposes& purposeToEvaluate);

	const TArray<FPurpose>& GetPurposeCacheForBackgroundThread() const { return purposeCacheForBackgroundThread; }
	const TArray<TObjectPtr<UPurposeAbilityComponent>>& GetCandidateCache() const { return candidateCache; }

	const int GetNumQueuedPurposes() { return numQueueItems; }
protected:
	const TArray<FPurpose>& purposeCacheForBackgroundThread;

	const TArray<TObjectPtr<UPurposeAbilityComponent>>& candidateCache;

	PurposeQueue potentialPurposeQueue;

	/// TQueues do not have a .Num() to track how many queued items
	/// We can track the number of queued items so that if we want to have multiple threads we can simply compare queue to the thread with less items
	int numQueueItems = 0;

	bool CreateAsyncTask_PurposeSelected(FContextData context);

};

namespace PurposeSystem
{

	static bool QueuePurposeToBackgroundThread(FPotentialPurposes potentialPurposes, FPurposeEvaluationThread& backgroundThread)
	{
		if (backgroundThread.QueuePurpose(potentialPurposes))
		{
			return true;
		}
		/// If the potential purposes were not queued they will simply be GCd;
		return false;
	}

	static bool Occurrence(FSubjectMap subjectsOfContext, TArray<FDataMapEntry> context, const TArray<FPurposeEvaluationThread*>& backgroundThreads)
	{
		FPurposeEvaluationThread* selectedThread = nullptr;
		int leastNumQueuedPurposes = 0;
		for (FPurposeEvaluationThread* thread : backgroundThreads)
		{
			if (!thread)
			{
				continue;
			}
			if (thread->GetPurposeCacheForBackgroundThread().Num() < 1)
			{
				Global::LogError(EVENT, "PurposeSystem", "Occurrence", TEXT("No Event Assets stored on background thread!"));
				continue;
			}

			if (thread->GetCandidateCache().Num() < 1)
			{
				Global::LogError(EVENT, "PurposeSystem", "Occurrence", TEXT("No Event Assets stored on background thread!"));
				continue;
			}

			if (thread->GetNumQueuedPurposes() <= leastNumQueuedPurposes)
			{
				leastNumQueuedPurposes = thread->GetNumQueuedPurposes();
				selectedThread = thread;
			}
		}

		if (!selectedThread)
		{
			Global::LogError(EVENT, "PurposeSystem", "Occurrence", TEXT("Selected background thread is invalid!!"));
			return false;
		}

		FPurposeEvaluationThread& backgroundThread = *selectedThread;

		FPotentialPurposes potentialPurposes;

		potentialPurposes.candidatesForNewPurpose = backgroundThread.GetCandidateCache();

		potentialPurposes.subjectMapForPotentialPurposes = subjectsOfContext;
		potentialPurposes.ContextDataForPotentialPurposes = context;

		/// Queue the subjects, context, and potential purposes to background thread
		/// At this time, we don't bother with UniqueSubjects for Occurrences, as Conditions for Events are revolving strictly around the context of the Occurrence
		return PurposeSystem::QueuePurposeToBackgroundThread(potentialPurposes, backgroundThread);
	}
	
	static bool Occurrence(const TArray<TObjectPtr<UPurposeAbilityComponent>>& inCandidates, FSubjectMap subjectsOfContext, TArray<FDataMapEntry> context, const TArray<FPurposeEvaluationThread*>& backgroundThreads)
	{
		FPurposeEvaluationThread* selectedThread = nullptr;
		int leastNumQueuedPurposes = 0;
		for (FPurposeEvaluationThread* thread : backgroundThreads)
		{
			if (!thread)
			{
				continue;
			}
			if (thread->GetPurposeCacheForBackgroundThread().Num() < 1)
			{
				Global::LogError(EVENT, "PurposeSystem", "Occurrence", TEXT("No Event Assets stored on background thread!"));
				continue;
			}

			if (thread->GetCandidateCache().Num() < 1)
			{
				Global::LogError(EVENT, "PurposeSystem", "Occurrence", TEXT("No Candidates stored on background thread!"));
				continue;
			}

			if (thread->GetNumQueuedPurposes() <= leastNumQueuedPurposes)
			{
				leastNumQueuedPurposes = thread->GetNumQueuedPurposes();
				selectedThread = thread;
			}
		}

		if (!selectedThread)
		{
			Global::LogError(EVENT, "PurposeSystem", "Occurrence", TEXT("No valid background thread was selected!"));
			return false;
		}

		FPurposeEvaluationThread& backgroundThread = *selectedThread;

		FPotentialPurposes potentialPurposes;

		potentialPurposes.candidatesForNewPurpose = inCandidates;

		potentialPurposes.subjectMapForPotentialPurposes = subjectsOfContext;
		potentialPurposes.ContextDataForPotentialPurposes = context;

		/// Queue the subjects, context, and potential purposes to background thread
		/// At this time, we don't bother with UniqueSubjects for Occurrences, as Conditions for Events are revolving strictly around the context of the Occurrence
		return PurposeSystem::QueuePurposeToBackgroundThread(potentialPurposes, backgroundThread);
	}

	namespace Private
	{
		bool AttemptToPassSelectedPurposeToOwner(const FContextData& selectedPurpose);
	}

	/// The background thread has found a purpose
	/// It then calls this from an AsyncTask
	/// It requests the owner of the purpose store the purpose
	/// Then it attempts to get the next layer of purpose
	static void PurposeSelected(FContextData contextOfSelectedPurpose)
	{
		if (!contextOfSelectedPurpose.purposeOwner.IsValid())
		{
			Global::LogError(EVENT, "PurposeSystem", "PurposeSelected", TEXT("Provided an invalid PurposeOwner!"));
			return;
		}

		/// Refactor: Purpose Evaluation; We can't call back to purpose ability component from here
			/// So how can we get the context back to the purpose component?
				/// Easiest solution is to just move this to the cpp, but that's sloppy design
				/// We need a way to call an occurrence on ability start, and a way to return a purpose to the ability component
		bool purposeAccepted = PurposeSystem::Private::AttemptToPassSelectedPurposeToOwner(contextOfSelectedPurpose);

		if (!purposeAccepted)
		{
			Global::Log(DATADEBUG, PURPOSE, "PurposeSystem", "PurposeSelected", TEXT("Purpose Owner %s did not accept the provided purpose: %s!")
				, *contextOfSelectedPurpose.GetOwnerName()
				, *contextOfSelectedPurpose.GetName()
			);

			return;
		}

		contextOfSelectedPurpose.AdjustDataIfPossible(contextOfSelectedPurpose.purpose.DataAdjustments(), EPurposeSelectionEvent::OnSelected, PURPOSE, "PurposeSelected", nullptr, "PurposeSystem");
	}

}

#pragma endregion

/// <summary>
/// The Event thread handles:
///		Selecting Objectives for the AI belonging to a Manager with a selected Goal
///		Selecting Goals for Managers receiving an Event
///		Finding Events for Occurrences
/// </summary>
class FEventThread : public FPurposeEvaluationThread
{
public:

	~FEventThread();

protected:

};

/// <summary>
/// The Actor thread handles:
///		Finding a Reaction Objective for direct actions
///		Finding Tasks for Objectives
/// </summary>
class FActorThread : public FPurposeEvaluationThread
{
public:

	~FActorThread();



};

/// <summary>
/// The Companion thread handles:
///		Finding all companion purposes on the client side
/// </summary>
class FCompanionThread : public FEventThread
{
public:
	~FCompanionThread();


};

#pragma region TGraphTasks

/// <summary>
/// ASyncGraphTask_PurposeSelected is utilized by the background thread to send a ContextData with a purpose back to the gamethread
/// </summary>
class FAsyncGraphTask_PurposeSelected
{
protected:
	FContextData contextData;
//	FPurposeEvaluationThread& callingThread;///Used to detect stopThread
	bool shouldAbandon = false;

public:
	FAsyncGraphTask_PurposeSelected(FContextData inContext)
		: contextData(inContext)
	{
	}

	virtual ~FAsyncGraphTask_PurposeSelected()
	{
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncGraphTask_PurposeSelected, STATGROUP_TaskGraphTasks); }
	static FORCEINLINE ENamedThreads::Type GetDesiredThread() { return ENamedThreads::GameThread; }
	static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::FireAndForget; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		//		//Global::Log(EHierarchicalCalltraceVerbosity::DEBUG, "FAsyncGraphTask_PurposeSelected", "DoTask", TEXT(""));

		shouldAbandon ? Cancel() : PurposeSelected();
	}

	void PurposeSelected();

	/// Can be called by any thread in order to ensure that the purpose chain halts
	/// When the task attempts to DoTask(), it will instead cancel
	void Abandon() { shouldAbandon = true; }

protected:
	void Cancel()
	{
		//Global::Log(EHierarchicalCalltraceVerbosity::DEBUG, "FAsyncGraphTask_PurposeSelected", "Cancel", TEXT(""));
	}

};

#pragma endregion

