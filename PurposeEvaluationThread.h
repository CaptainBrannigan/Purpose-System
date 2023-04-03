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
struct FPurposeModificationEntry
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

USTRUCT(BlueprintType)
struct FPurpose
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere)
	FString descriptionOfPurpose = "";

	UPROPERTY(Instanced, EditAnywhere, meta = (TitleProperty = "Criteriia for Purpose Selection", ShowInnerProperties))
		/// These conditions establish how a purpose fits to a context
		/// They are critical to create legible and realistic purpose for both individual actors and groups of actors
		/// The more conditions present, the greater the potential weight of a purpose
	TArray<TObjectPtr<UCondition>> conditions;
	const TArray<TObjectPtr<UCondition>> GetConditions() const { return conditions; }

	UPROPERTY(Instanced, EditAnywhere, meta = (TitleProperty = "Criteria for Purpose Completion", ShowInnerProperties))
		/// These conditions establish how a purpose can be determined as complete
	TArray<TObjectPtr<UCondition>> completionCriteria;
	const TArray<TObjectPtr<UCondition>> GetConditions() const { return completionCriteria; }

	UPROPERTY(EditAnywhere, meta = (TitleProperty = "description"))
		/// These DataChunks will either be adjusted or created 
		/// Adjustments will be determined by enum selection provided by DataChunk
	TArray<FPurposeModificationEntry> dataAdjustmentsForPurposeEvents;

	/* This is the ideal design of FPurpose, but TArray<> prevents FPurpose from referencing itself unfortunately
	
	/// <summary>
	/// Having to choose to either add sub purposes or behaviors allows us to have an N number of layers, where the last layer is marked by having behaviors instead of sub purposes
	/// </summary>

	bool bCanEditSubPurposes = false;
	//UPROPERTY(EditAnywhere, meta = (InlineEditConditionToggle, EditCondition = "!bCanEditBehavior"))
	UPROPERTY(EditAnywhere)
	TArray<FPurpose> subPurposes;

	bool bCanEditBehavior = false;
	///UPROPERTY(EditAnywhere, meta=(InlineEditConditionToggle, EditConditionHides, EditCondition = "!canEditSubPurposes"))
	///TObjectPtr<class UBehaviorTree> BT = nullptr;
	UPROPERTY(EditAnywhere, meta = (InlineEditConditionToggle, EditCondition = "!bCanEditSubPurposes"))
		TObjectPtr<class UBehavior_AI> behaviorAbility = nullptr;

	bool bRequiresTargeting = false;
	UPROPERTY(EditAnywhere, meta = (InlineEditConditionToggle, EditCondition = "!bRequiresTargeting"))
		TObjectPtr<class UEnvQuery> targetingQuery = nullptr;
	*/

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
/// For each layer of Purpose, an address layer with the index of that purpose.subPurpose is added.
/// This works in "Event.Goal.Objective.Behavior" order as we have n number of layers by design
/// Only the stored Event will have the whole tree of purposes however, so seeking a specific address will have to be requested by whoever stores the Event
/// This also has to start with a globally relevant Event address. All Events need to be stored in a single location until shutdown, otherwise when one Event ends and is removed the addresses will all be incorrect
struct FPurposeAddress
{
	GENERATED_BODY()
public:

	FPurposeAddress() {}

	FPurposeAddress(int inAddress)
		: address(inAddress)
	{
		HierarchicalAddress.Add(address);
	}

	FPurposeAddress(FPurposeAddress previousAddress, int inAddress)
		: address(inAddress)
		, HierarchicalAddress(previousAddress.HierarchicalAddress)
	{
		/// Firstly we store the previous address to retain the hierarchical structure of purpose layers
		/// In order to have n layers of purpose, we add to the end until we no longer have a layer
		HierarchicalAddress.Add(address);
	}

	/// 0 will mean it's at the Event layer
	/// 1 means it's at the Goal layer
	/// 2 Is the Objective layer
	/// 3 is the Behavior layer
	int GetAddressLayer() const
	{
		return HierarchicalAddress.Num();
	}

	int GetAddressForLayer(const int& layer) const
	{
		return HierarchicalAddress.IsValidIndex(layer) ? HierarchicalAddress[layer] : -1;
	}

	int GetAddressOfThisPurpose() const
	{
		return address;
	}

	FString GetAddressAsString() const
	{
		FString addressAsString = "";
		for (int index = 0; index < HierarchicalAddress.Num(); ++index)
		{
			/// If the index is not at the end of the array of addresses, then add a . to separate them visually
			addressAsString += FString::Printf(TEXT("%d%s"), HierarchicalAddress[index], index == HierarchicalAddress.Num() - 1 ? "" : ".");
		}
		return addressAsString;
	}

	inline bool IsValid() const { return address > -1; }

private:

	int address = -1;

	TArray<int> HierarchicalAddress;

public:
	FORCEINLINE bool operator ==(const FPurposeAddress& otherAddress) const
	{
		if (otherAddress.address != address) { return false; }
		if (otherAddress.HierarchicalAddress.Num() != HierarchicalAddress.Num()) { return false; }

		for (int i = 0; i < HierarchicalAddress.Num(); ++i)
		{
			if (!otherAddress.HierarchicalAddress.IsValidIndex(i)) { return false; }
			if (HierarchicalAddress[i] != otherAddress.HierarchicalAddress[i]) { return false; }
		}

		return true;
	}
};
FORCEINLINE uint32 GetTypeHash(const FPurposeAddress& b)
{
	return FCrc::MemCrc32(&b, sizeof(FPurposeAddress));
}

USTRUCT(BlueprintType)
struct FSubjectMap
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

class IPurposeManagementInterface;

USTRUCT(BlueprintType)
struct FContextData
{
	GENERATED_BODY()

#pragma region ContextData
public:

	FContextData() {}

	FContextData(FPurpose inPurpose, FSubjectMap inSubjectMap, TArray<FDataMapEntry> inContextData, TScriptInterface<IPurposeManagementInterface> inPurposeOwner, FPurposeAddress inAddressForPurpose, FString descriptionOfParent = "", int32 parentID = 0)
		: purpose(inPurpose)
		, subjectMap(inSubjectMap)
		, contextData(inContextData)
		, purposeOwner(inPurposeOwner)
		, addressOfPurpose(inAddressForPurpose)
	{
		contextDataPurposeName = purpose.descriptionOfPurpose + "(" + (purposeOwner.GetObject() ? purposeOwner.GetObject()->GetName() : "Invalid") + ")";
		chainedPurposeName += descriptionOfParent.Len() > 0 ? "::" + descriptionOfParent : "";
		
		if (parentID == 0) /// If an existing ID is not provided, then we need to generate the initial unique ID for this context and all sub contexts
		{
			/// As the address of events are relevant to a single cache, each is different and thus when added to GetTicks, even if on the same Tick, will provide a different ID
			uniqueIdentifier = addressOfPurpose.GetAddressOfThisPurpose() + FDateTime::UtcNow().GetTicks();
		}
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

	/// Whenever a layer of purpose is added, the address adds a layer of address
	/// So layer 1 will have a single address entry. But layer 2 will have the main address and a sub address, and so on so forth
	FPurposeAddress addressOfPurpose;

	TScriptInterface<IPurposeManagementInterface> purposeOwner = nullptr;

	inline bool ContextIsValid() const { return addressOfPurpose.IsValid(); }

	//inline bool HasPurpose() { return purpose.SubPurposes.Num() > 0 || purpose.BehaviorAbility; }
	inline bool HasPurpose() const { return purpose.conditions.Num() > 0; }

	///@return FString: If a purpose has been found for this contextData, returns the name of the purpose itself
	FORCEINLINE FString GetName() const
	{
		return contextDataPurposeName;
	}

	///Construct a loop of parentContext->contextDataPurposeName until a parentContext is no longer valid
	///Returns a name in the format of "parentContext->contextDataPurposeName::contextDataPurposeName"
	///@return the names of all purposes up to top of chain
	FORCEINLINE FString GetPurposeChainName() const
	{
		return chainedPurposeName;
	}

	FString Description() const
	{
		FString name = GetPurposeChainName();
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

		FString name = GetPurposeChainName();
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
	///The name utilized to represent a chain of ParentContext->ChildContext
	FString chainedPurposeName = "contextData";

	const int64 GetContextID() const { return uniqueIdentifier; }

	UPROPERTY()
		/// This map links the Purpose.SubPurposes() to a "static" purpose state
		/// It allows us to reference the completion status of a sub purpose
		/// Which can in turn be used to determine completion status of this purpose
		/// FTrackedPurposeEntry is the address of each individual sub purpose linked to the unique context ID for tree of purposes
		TMap<FPurposeAddress, EPurposeState> subPurposeStatus;

	UPROPERTY()
		/// This map represents how many participants there are for sub purposes
		/// FTrackedPurposeEntry is the address of each individual sub purpose linked to the unique context ID for tree of purposes
		TMap<FPurposeAddress, int> subPurposeParticipants;

	bool UpdateSubPurposeStatus(const FPurposeAddress& subPurpose, EPurposeState status)
	{
		if (subPurposeStatus.Find(subPurpose))/// If the objective class matches update the status
		{
			Global::Log(DATATRIVIAL, PURPOSE, GetName(), "UpdateSubPurposeStatus", TEXT("SubPurpose %s status is now."), *subPurpose.GetAddressAsString(), *Global::EnumValueOnly<EPurposeState>(status));
			subPurposeStatus[subPurpose] = status;
			return true;
		}
		else
		{
			Global::LogError(PURPOSE, GetName(), "UpdateSubPurposeStatus", TEXT("SubPurpose %s was not found in subPurposeStatus."), *subPurpose.GetAddressAsString());
		}

		return false;
	}

	bool IncreaseSubPurposeParticipants(const FPurposeAddress& subPurpose)
	{
		if (subPurposeParticipants.Find(subPurpose))/// If the objective class matches update the status
		{
			subPurposeParticipants[subPurpose]++;
			Global::Log(DATATRIVIAL, PURPOSE, GetName(), "IncreaseSubPurposeParticipants", TEXT("SubPurpose %s participants now %d."), *subPurpose.GetAddressAsString(), subPurposeParticipants[subPurpose]);
			return true;
		}
		else
		{
			Global::LogError(PURPOSE, GetName(), "IncreaseSubPurposeParticipants", TEXT("SubPurpose %s was not found in ObjectiveParticipants."), *subPurpose.GetAddressAsString());
		}

		return false;
	}

	bool DecreaseSubPurposeParticipants(const FPurposeAddress& subPurpose)
	{
		if (subPurposeParticipants.Find(subPurpose))/// If the objective class matches update the status
		{
			subPurposeParticipants[subPurpose]++;
			Global::Log(DATATRIVIAL, PURPOSE, GetName(), "DecreaseSubPurposeParticipants", TEXT("SubPurpose %s participants now %d."), *subPurpose.GetAddressAsString(), subPurposeParticipants[subPurpose]);
			return true;
		}
		else
		{
			Global::LogError(PURPOSE, GetName(), "DecreaseSubPurposeParticipants", TEXT("SubPurpose %s was not found in ObjectiveParticipants."), *subPurpose.GetAddressAsString());
		}
		return false;
	}

private:

	/// This unique id is meant to provide every context data witthin a single event a unifying id
	/// This is a means of identifying tracked purposes based on the address and this ID
	/// We can not use address alone as a purpose may be reused multiple times for different contexts
	int64 uniqueIdentifier = 0;
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

	bool AdjustData(ESubject target, const TObjectPtr<UDataChunk>& adjustmentChunk)
	{
		if (IsValid(adjustmentChunk))
		{
			if (HasSubject(target))
			{
				if (DataMapInterfaceForSubject(target)->HasData(adjustmentChunk->GetClass()))/// If we have the data already
				{
					Global::Log(DATATRIVIAL, PURPOSE, GetPurposeChainName(), "AdjustData", TEXT("Adjusting %s by %d."), *adjustmentChunk->GetClass()->GetName(), (int)adjustmentChunk->DataModifier());
					DataMapInterfaceForSubject(target)->DataChunk(adjustmentChunk->GetClass())->AdjustData(adjustmentChunk->DataModifier());/// Adjust the data by the specified modifier
				}
				else/// Otherwise create the data chunk
				{
					Global::Log(DATATRIVIAL, PURPOSE, GetPurposeChainName(), "AdjustData", TEXT("Creating DataChunk %s with modification %d."), *adjustmentChunk->GetClass()->GetName(), (int)adjustmentChunk->DataModifier());
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
					Global::LogError(PURPOSE, GetPurposeChainName(), "AdjustData", TEXT("Cannot create DataChunk %s with modification %d for the context as we have no outer!"), *adjustmentChunk->GetClass()->GetName(), (int)adjustmentChunk->DataModifier());
					//DataMapInterfaceForSubject(target)->AddData(NewObject<UDataChunk>(Get, adjustmentChunk->GetClass())->AdjustData(adjustmentChunk->DataModifier()));/// And still apply the modification requested
				}
				return true;
			}
		}

		Global::LogError(PURPOSE, GetPurposeChainName(), "AdjustData", TEXT("DataAdjustment for %s and subject %s %s")
			, *GetPurposeChainName()
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
				AdjustData(chunk.subjectToAdjust, chunk.dataAdjustment);
			}
			else
			{
				/// Only log if the DataAdjustment was invalid
				if (!IsValid(chunk.dataAdjustment))
				{
					source ?
						Global::LogError(inLogCat, *source, callingMethodName, TEXT("DataAdjustment for %s has an invalid DataAdjustment chunk!"), *GetPurposeChainName())
						: Global::LogError(inLogCat, sourceName, callingMethodName, TEXT("DataAdjustment for %s has an invalid DataAdjustment chunk!"), *GetPurposeChainName());
				}
			}
		}
	}

#pragma endregion

public:
	FORCEINLINE bool operator ==(const FContextData& other) const
	{
		return addressOfPurpose == other.addressOfPurpose && uniqueIdentifier == other.uniqueIdentifier;
	}
};
FORCEINLINE uint32 GetTypeHash(const FContextData& b)
{
	return FCrc::MemCrc32(&b, sizeof(FContextData));
}

USTRUCT(BlueprintType)
struct FPotentialPurposeEntry
{
	GENERATED_BODY()
public:

	FPotentialPurposeEntry() {}

	FPotentialPurposeEntry(FPurpose inPurpose, FPurposeAddress inAddress, TArray<FSubjectMap> inUniqueSubjectMap)
		: purposeToBeEvaluated(inPurpose)
		, addressOfPurpose(inAddress)
		, mapOfUniqueSubjectEntriesForPurpose(inUniqueSubjectMap)
	{
	}

	/// This address is the full address, including parent address, for this purpose entry
	FPurposeAddress addressOfPurpose;

	/// This is the actual purpose that will be evaluated against the subject map established specifically for this purpose, + the static subject map from the context
	FPurpose purposeToBeEvaluated;

	/// The PotentialSubjectMaps are a combination of 1 UniqueSubject and any other entries desired
	/// The StaticSubjectMap will be appended to the PotentialSubjectMap at evaluation, the highest scoring pair becomes the new StaticSubjectMap
	TArray<FSubjectMap> mapOfUniqueSubjectEntriesForPurpose;

};

USTRUCT(BlueprintType)
struct FPotentialPurposes
{
	GENERATED_BODY()
public:

	FPotentialPurposes(const FPurposeAddress parentAddress, const int64 parentID)
		: addressOfParentPurpose(parentAddress)
		, uniqueIdentifierOfParent(parentID)
	{}

	/// A combination of a potential purpose and the UniqueSubject entries for that specific purpose
	TArray<FPotentialPurposeEntry> potentialPurposes;

	int AddressLayer = -1;

	/// We store the parent address here so that, when selected, the selected sub purpose may create their full address
	const FPurposeAddress addressOfParentPurpose;

	/// Subject map for the potential purposes to evaluate against
	FSubjectMap staticSubjectMapForPotentialPurposes;

	/// This is the 1 UniqueSubject for which this FPotentialPurpose exists
	TScriptInterface<IPurposeManagementInterface> purposeOwner = nullptr;

	/// Context data for the potential purposes to evaluate against
	TArray<FDataMapEntry> ContextDataForPotentialPurposes;

	FString DescriptionOfParentPurpose = "";

	/// This unique id is meant to provide every context data witthin a single event a unifying id
	/// This is a means of identifying tracked purposes based on the address and this ID
	/// We can not use address alone as a purpose may be reused multiple times for different contexts
	const int64 uniqueIdentifierOfParent;

	void SetDescriptionOfParentPurpose(TScriptInterface<IPurposeManagementInterface > parentOwner, FString parentDescription)
	{
		DescriptionOfParentPurpose = FString::Printf(TEXT("%s::%s"), *parentDescription, IsValid(parentOwner.GetObject()) ? *parentOwner.GetObject()->GetName() : TEXT("Invalid"));
	}

	void SetDescriptionOfParentPurpose(const FContextData& parentContext)
	{
		DescriptionOfParentPurpose = parentContext.GetName();
	}

};

///Umbrella type for multiple queues of UContextData_Deprecated
typedef TQueue<TObjectPtr<UContextData_Deprecated>> PurposeQueue;

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

	/// This is reliant on how each thread is setup
	/// Add specific keys to individual threads that you wish to separate by thread
	///@return bool: True when the purpose was stored to a queue to be evaluated at some point
	bool QueuePurpose(FPotentialPurposes potentialPurposesToQueue) 
	{
		if (potentialPurposeQueues.Contains(potentialPurposesToQueue.AddressLayer))
		{
			potentialPurposeQueues[potentialPurposesToQueue.AddressLayer].Enqueue(potentialPurposesToQueue);
		}
		return false;
	}

	///@param layerToDequeue: Used to dictate which layer we wish to evaluate, allowing us to dictatet an order in which they may be dequeued and evaluated
	/// @param dequeudPurpose; TQueue requires an out param to dequeue to
	///@return bool: False only when a purpose was not dequeued
	bool DequeuePurpose(uint8 layerToDequeue, FPotentialPurposes& dequeuedPurpose)
	{
		if (potentialPurposeQueues.Contains(layerToDequeue))
		{
			potentialPurposeQueues[layerToDequeue].Dequeue(dequeuedPurpose);
			return true;
		}

		return false;
	}

	/// @param purposeToEvaluate; The combination of context, subjects and potential purposes to evaluate to a single purpose for a unique subject. After evaluation, the data may be copied to further the purpose system, but this struct will be destroyed regardless.
	///@return bool: 
	bool SelectPurposeIfPossible(FPotentialPurposes& purposeToEvaluate);

protected:

	TMap<uint8, TQueue<FPotentialPurposes>> potentialPurposeQueues;

	bool CreateAsyncTask_PurposeSelected(FContextData& context);
	bool FPurposeEvaluationThread::CreateAsyncTask_ReOccurrence(TScriptInterface<class IPurposeManagementInterface> owner, const FPurposeAddress addressOfPurpose, const int64 outUniqueIDofActivePurpose);

};

UINTERFACE(BlueprintType)
class UPurposeManagmentInterface : public UInterface
{
	GENERATED_BODY()

};

/// <summary>
/// This interface is a means for providing the Purpose system with everything it requires that it cannot initialize itself
/// </summary>
class IPurposeManagementInterface
{
	GENERATED_BODY()
public:

	/// Used in order to reference up the management chain to the owner of Events and Backgrounds threads
	virtual TScriptInterface<IPurposeManagementInterface> GetHeadOfPurposeManagment() = 0;

	/// @return TScriptInterface<IPurposeManagementInterface>: Returns the immediate purpose manager above caller
	virtual TScriptInterface<IPurposeManagementInterface> GetPurposeSuperior() = 0;

	virtual TArray<FPurposeEvaluationThread*> GetBackgroundPurposeThreads() = 0;

	/// @return TArray<TScriptInterface<IDataMapInterface>>: Every candidate we wish to select a purpose for
	virtual TArray<TScriptInterface<IDataMapInterface>> GetCandidatesForSubPurposeSelection(const int PurposeLayerForUniqueSubjects) = 0;

	/// @param PurposeLayerForUniqueSubjects: Represents the purpose layer for which the PurposeOwner is meant to create new FUniqueSubjectMaps
	/// @param parentContext:
	/// @param candidate: This is the primary subject that will be combined with other subjects as needed for purpose selection
	/// @return TArray<FSubjectMap>: Each entry is a combination of the candidate and whatever other subjects required for the subpurpose indicated by addressOfSubPurpose
	virtual TArray<FSubjectMap> GetUniqueSubjectsRequiredForSubPurposeSelection(const int PurposeLayerForUniqueSubjects, const FContextData& parentContext, TScriptInterface<IDataMapInterface> candidate, FPurposeAddress addressOfSubPurpose) = 0;

	virtual bool ProvidePurposeToOwner(const FContextData& purposeToStore) = 0;

	/// Events must be stored globally for the duration of a game so that they may have a consistent PurposeAddress
	virtual TArray<FPurpose> GetEventAssets() = 0;

	/// As FPurpose can not hold an variable or TArray<> of itself, we're forced to workaround simply accessing subpurposes
	virtual TArray<FPurpose> GetSubPurposesFor(FPurposeAddress address) = 0;

	virtual const TArray<FContextData>& GetActivePurposes() = 0;

	/// When a purpose is put up for selection, but it appears to be a duplicate of a current purpose, we want to let the purpose owner handle the reoccurrence
	virtual void PurposeReOccurrence(const FPurposeAddress addressOfPurpose, const int64 uniqueIDofActivePurpose) = 0;

	/// @param uniqueIdentifierOfContextTree: This ID unique to a series of context datas starting with Event allows separation of same purposes for different contexts
	/// @param fullAddress: Tying the address to the unique ID is how we can search stored contexts for the relevant context we seek
	/// @param layerToRetrieveFor: We may not necessarily wish to find the end address of the fullAddress, so we can indicate a layer to seek out
	/// @return FContextData&: Need to check for validity as the context data may not have been found and an empty struct returned
	virtual FContextData& GetStoredPurpose(const int64 uniqueIdentifierOfContextTree, const FPurposeAddress& fullAddress, const int layerToRetrieveFor) = 0;

	/// @param parentAddress: An address which is either that of a purpose containing behaviors so that it may reference the parent, or the parent address itself
	/// @param TArray<TObjectPtr<UGA_Behavior>>: All the behaviors contained by the parent indicated
	virtual TArray<TObjectPtr<class UBehavior_AI>> GetBehaviorsFromParent(const FPurposeAddress& parentAddress) = 0;

	/// @param parentAddress: An address of the behavior containing purpose
	/// @param TObjectPtr<UGA_Behavior>: The behavior contained by the address provided
	virtual TObjectPtr<class UBehavior_AI> GetBehaviorAtAddress(const FPurposeAddress& inAddress) = 0;

	///@return bool: Determined by the implementer
	virtual bool DoesPurposeAlreadyExist(const FContextData& primary, const FSubjectMap& secondarySubjects, const TArray<FDataMapEntry>& secondaryContext, const FPurposeAddress optionalAddress = FPurposeAddress()) = 0;

	virtual void SubPurposeCompleted(const int64& uniqueContextID, const FPurposeAddress& addressOfPurpose) = 0;

	virtual void AllSubPurposesComplete(const int64& uniqueContextID, const FPurposeAddress& addressOfPurpose) = 0;
};

namespace PurposeSystem
{

	static bool QueuePurposeToBackgroundThread(FPotentialPurposes potentialPurposes, TArray<FPurposeEvaluationThread*> potentialThreadsToQueueOn)
	{
		/// Since we have a queue purpose on the background threads which queue based on a switch statement for the address level
			/// We can simply have them return a bool for whether the potentialpurpose was queued or not
			/// If not, then try the next background thread until we either get a positive queue or have to destroy the potential purpose

		for (FPurposeEvaluationThread* thread : potentialThreadsToQueueOn)
		{
			if (thread->QueuePurpose(potentialPurposes))
			{
				return true;
				break;
			}
		}
		/// If the potential purposes were not queued they will simply be GCd;
		return false;
	}

	static bool Occurrence(FSubjectMap subjectsOfContext, TArray<FDataMapEntry> context, TScriptInterface<IPurposeManagementInterface> purposeOwner)
	{
		if (!IsValid(purposeOwner.GetObject()) && IsValid(purposeOwner->GetHeadOfPurposeManagment().GetObject()))
		{
			Global::LogError(EVENT, "PurposeSystem", "Occurrence", TEXT("Provided an invalid PurposeOwner or Purpose Superior!"));
			return false;
		}

		/// We utilize the head of the purpose management system as they are responsible for storing all event assets as well as Event contexts
		TScriptInterface<IPurposeManagementInterface> headOfPurposeManagement = purposeOwner->GetHeadOfPurposeManagment();

		/// Before we even bother with potential purposes for an occurrence, let's make sure it doesn't already exist
		for (const FContextData& eventContext : headOfPurposeManagement->GetActivePurposes())
		{
			if (headOfPurposeManagement->DoesPurposeAlreadyExist(eventContext, subjectsOfContext, context))
			{
				Global::Log( DATATRIVIAL, EVENT, "PurposeSystem", "Occurrence", TEXT("Provided an invalid Occurrence already exists!"));
				return false;
				break;
			}
		}

		if (headOfPurposeManagement->GetEventAssets().Num() < 1)
		{
			Global::LogError(EVENT, "PurposeSystem", "Occurrence", TEXT("No Event Assets!"));
			return false;
		}

		FPotentialPurposes potentialPurposes(FPurposeAddress(), 0);/// We can not initialize the potential purposes with parent data as we are at the initial purpose step
		potentialPurposes.AddressLayer = (int)EPurposeLayer::Event;/// As this is an Occurrence we have to initialize which Purpose Layer this will be evaluated for
		potentialPurposes.purposeOwner = headOfPurposeManagement;

		TArray<FPurpose> events = potentialPurposes.purposeOwner->GetEventAssets();
		TArray<TScriptInterface<IDataMapInterface>> candidates = potentialPurposes.purposeOwner->GetCandidatesForSubPurposeSelection(potentialPurposes.AddressLayer);
		TArray<FSubjectMap> subjects;
		for (auto candidate : candidates)/// At least 1 entry is required as the purpose evaluation works on a for loop
		{
			subjects.Append(potentialPurposes.purposeOwner->GetUniqueSubjectsRequiredForSubPurposeSelection(potentialPurposes.AddressLayer, FContextData(), candidate, FPurposeAddress()));
		}

		TArray<FPotentialPurposeEntry> entries;

		for (int i = 0; i < events.Num(); ++i)
		{
			/// As this is the first layer of purpose, we can't provide a previous purpose address, so we simply set the address to the index of the cached EventAssets
			FPotentialPurposeEntry entry(events[i], FPurposeAddress(i), subjects);

			entries.Add(entry);
		}

		potentialPurposes.potentialPurposes = entries;
		potentialPurposes.staticSubjectMapForPotentialPurposes = subjectsOfContext;
		potentialPurposes.ContextDataForPotentialPurposes = context;

		/// Queue the subjects, context, and potential purposes to background thread
		/// At this time, we don't bother with UniqueSubjects for Occurrences, as Conditions for Events are revolving strictly around the context of the Occurrence
		return PurposeSystem::QueuePurposeToBackgroundThread(potentialPurposes, potentialPurposes.purposeOwner->GetBackgroundPurposeThreads());
	}

	static void QueueNextPurposeLayer(const FContextData& contextToParentPurpose)
	{
		int nextPurposeLayer = contextToParentPurpose.addressOfPurpose.GetAddressLayer() + 1;/// Because we are now evaluating sub purposes, we raise the address layer so the background thread is aware 
		
		TArray<FPurpose> potentialPurposesForEvaluation = contextToParentPurpose.purposeOwner->GetSubPurposesFor(contextToParentPurpose.addressOfPurpose);
		TArray<TScriptInterface<IDataMapInterface>> candidates = contextToParentPurpose.purposeOwner->GetCandidatesForSubPurposeSelection(nextPurposeLayer);

		/// For every candidate, we establish a FPotentialPurposes
			/// Which contains not only the sub purpose of the contextOfParentPurpose, but also a subject map relevant specifically to that sub purpose
		for (TScriptInterface<IDataMapInterface> candidate : candidates)
		{
			FPotentialPurposes potentialPurposes(
				contextToParentPurpose.addressOfPurpose/// By providing the potential purposes the parent address, the one selected is then able to append it's own address to create the full address
				, contextToParentPurpose.GetContextID()/// By providing the sub contexts with the ID generated by the base context data we have a unique identifier for all contexts in this tree
			);
			//potentialPurposes.addressOfParentPurpose = contextToParentPurpose.addressOfPurpose;
			//potentialPurposes.uniqueIdentifierOfParent = contextToParentPurpose.GetContextID();
			potentialPurposes.AddressLayer = nextPurposeLayer;/// Importantly we use the nextPurposeLayer, as we a queuing for sub purposes now
			potentialPurposes.purposeOwner = TScriptInterface<IPurposeManagementInterface>(candidate.GetObject());/// The candidate, who is being evaluated for a potential purpose, is stored as the purpose owner so the purpose is returned to them

			if (!IsValid(potentialPurposes.purposeOwner.GetObject()))
			{
				Global::LogError(PURPOSE, "PurposeSystem", "QueueNextPurposeLayer", TEXT("Candidate returned is invalid or does not implement IPurposeManagementInterface! Parent context: %s.")
					, *contextToParentPurpose.GetPurposeChainName()
				);
				continue;
			}

			TArray<FPotentialPurposeEntry> purposeEntries;
			/// Now we need to establish unique subject entries, based off the candidate, for each individual potential purpose
			for ( int i = 0; i < potentialPurposesForEvaluation.Num(); ++i)
			{
				FPurpose& purpose = potentialPurposesForEvaluation[i];
				FPurposeAddress purposeAddress(contextToParentPurpose.addressOfPurpose, i);/// VERY IMPORTANT, this is how the sub purpose address is established, and is a huge aspect of the PurposeSystem

				/// Each UniqueSubject entry is a combination of the candidate + any other relevant subject to this purpose, such as a target
				TArray<FSubjectMap> UniqueSubjects = contextToParentPurpose.purposeOwner->GetUniqueSubjectsRequiredForSubPurposeSelection(nextPurposeLayer, contextToParentPurpose, candidate, purposeAddress);
				FPotentialPurposeEntry entry(purpose, purposeAddress, UniqueSubjects);
				purposeEntries.Add(entry);
			}

			potentialPurposes.potentialPurposes = purposeEntries;

			potentialPurposes.staticSubjectMapForPotentialPurposes = contextToParentPurpose.subjectMap;/// We separate the static and potential subject maps to avoid duplicating the static SubjectMap per UniqueSubject enry
			potentialPurposes.ContextDataForPotentialPurposes = contextToParentPurpose.contextData;/// The Context subject, but it's static data that once added to context does not change, plus we can't store it as a TScriptInterface<IDataMapInterface>
			/// So we're forced to keep it separate

			potentialPurposes.SetDescriptionOfParentPurpose(contextToParentPurpose);/// For our own debug sanity, it's nice have a description and setting up a chain or purpose descriptions with their owner

			/// Queue the subjects, context, and potential purposes to background thread
			PurposeSystem::QueuePurposeToBackgroundThread(potentialPurposes, contextToParentPurpose.purposeOwner->GetBackgroundPurposeThreads());
		}
	}

	/// The background thread has found a purpose
	/// It then calls this from an AsyncTask
	/// It requests the owner of the purpose store the purpose
	/// Then it attempts to get the next layer of purpose
	static void PurposeSelected(FContextData contextOfSelectedPurpose)
	{
		if (!IsValid(contextOfSelectedPurpose.purposeOwner.GetObject()))
		{
			Global::LogError(EVENT, "PurposeSystem", "PurposeSelected", TEXT("Provided an invalid PurposeOwner!"));
			return;
		}

		bool purposeAccepted = contextOfSelectedPurpose.purposeOwner->ProvidePurposeToOwner(contextOfSelectedPurpose);

		if (!purposeAccepted)
		{
			Global::Log(DATADEBUG, PURPOSE, "PurposeSystem", "PurposeSelected", TEXT("Purpose Owner %s did not accept the provided purpose: %s!")
				, *contextOfSelectedPurpose.purposeOwner.GetObject()->GetName()
				, *contextOfSelectedPurpose.GetPurposeChainName()
			);

			return;
		}

		contextOfSelectedPurpose.AdjustDataIfPossible(contextOfSelectedPurpose.purpose.DataAdjustments(), EPurposeSelectionEvent::OnSelected, PURPOSE, "PurposeSelected", nullptr, "PurposeSystem");

		bool bSubParticipantsIncreased = false;

		FContextData& parentContext = contextOfSelectedPurpose.purposeOwner->GetStoredPurpose(contextOfSelectedPurpose.GetContextID(), contextOfSelectedPurpose.addressOfPurpose, contextOfSelectedPurpose.addressOfPurpose.GetAddressLayer() - 1);
		if (parentContext.ContextIsValid())
		{
			/// Given a parent purpose, we need to ensure that sub purposes are tracked
			/// They only need to be added once though, as they are already stored within a context
			parentContext.subPurposeParticipants.FindOrAdd(contextOfSelectedPurpose.addressOfPurpose, 0);
			parentContext.subPurposeStatus.FindOrAdd(contextOfSelectedPurpose.addressOfPurpose, EPurposeState::Ongoing);

			bSubParticipantsIncreased = parentContext.IncreaseSubPurposeParticipants(contextOfSelectedPurpose.addressOfPurpose);
		}

		if (!bSubParticipantsIncreased)/// If the participants were not increased it was because the address or parent context was not found 
		{
			Global::Log(DATADEBUG, PURPOSE, "PurposeSystem", "PurposeSelected", TEXT("%s for %s.")
				, parentContext.ContextIsValid() ? TEXT("Parent Context did not increase participants") : TEXT("Parent Context was not found")
				, *contextOfSelectedPurpose.GetPurposeChainName()
			);
		}

		PurposeSystem::QueueNextPurposeLayer(contextOfSelectedPurpose);
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

	FEventThread(PurposeQueue& Objective, PurposeQueue& Event, PurposeQueue& Occurrence)
		: ObjectiveQueue(Objective),
		GoalQueue(Event),
		OccurrenceQueue(Occurrence)
	{
	}

	~FEventThread();

	///Executed so long as Init() returns true
	///Runs Recursively to evaluate queues until thread is told to stop and/or shutdown
	uint32 Run() override;

	///Recurse through queues until all are emptied via DestroyData()
	void Exit() override;

protected:

	///This queue exists because actors need to be able to score an objective per target
	///Previous setup was to add all those targets to an individual UContextData_Deprecated, else they wouldn't compare against each other 
	///Also so that we don't have to waste memory with multiple objects
	PurposeQueue& ObjectiveQueue;

	/// The Event queue serves to distribute the goals of an event to the appropriate managers
	PurposeQueue& GoalQueue;

	/// The Occurrence Queue serves to match an incoming context to an Event to begin a chain of purpose
	PurposeQueue& OccurrenceQueue;
};

/// <summary>
/// The Actor thread handles:
///		Finding a Reaction Objective for direct actions
///		Finding Tasks for Objectives
/// </summary>
class FActorThread : public FPurposeEvaluationThread
{
public:

	FActorThread(PurposeQueue& Reaction, PurposeQueue& Task)
		: ReactionQueue(Reaction),
		TasksQueue(Task)
	{
	}

	~FActorThread();

	///Executed so long as Init() returns true
	///Runs Recursively to evaluate queues until thread is told to stop and/or shutdown
	uint32 Run() final;

	///Recurse through queues until all are emptied via DestroyData()
	void Exit() final;

protected:

	///The Reaction queue holds contexts requiring an immediate objective to a direct action against an actor
	///This queue is the highest priority queue on the ActorThread
	PurposeQueue& ReactionQueue;

	/// The Tasks queue is responsible for finding a Task for Actors provided an Objective
	/// As it is likely in constant evaluation, it should be lowest priority of ActorThread
	PurposeQueue& TasksQueue;

};

/// <summary>
/// The Companion thread handles:
///		Finding all companion purposes on the client side
/// </summary>
class FCompanionThread : public FEventThread
{
public:

	FCompanionThread(PurposeQueue& Occurrence, PurposeQueue& Goal, PurposeQueue& Objective, PurposeQueue& Task)
		: FEventThread(Occurrence, Goal, Objective),
		TaskQueue(Task)
	{
	}

	~FCompanionThread();

	///Executed so long as Init() returns true
	///Runs Recursively to evaluate queues until thread is told to stop and/or shutdown
	uint32 Run() final;

	///Recurse through queues until all are emptied via DestroyData()
	void Exit() final;

protected:

	/// Queue selecting most appropriate action of behavior selected for companion
	PurposeQueue& TaskQueue;

};

#pragma region TGraphTasks


/// <summary>
/// Data cannot be removed from the rootset via background thread
/// Pass it back to the gamethread on a fire and forget task to be removed from root and sweeped up by GC
/// </summary>
class FAsyncGraphTask_DestroyData
{
	TObjectPtr<UContextData_Deprecated> contextData;
	FString reasonForDeletion = "";

public:
	FAsyncGraphTask_DestroyData(TObjectPtr<UContextData_Deprecated> inContext, FString reason = "")
		: contextData(inContext),
		reasonForDeletion(reason)
	{
	}

	~FAsyncGraphTask_DestroyData()
	{
//		//Global::Log(EHierarchicalCalltraceVerbosity::DEBUG, "FAsyncGraphTask_DestroyData", "~FAsyncGraphTask_PurposeSelected", TEXT(""));
		if (IsValid(contextData)) { contextData->RemoveFromRoot(); }
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncGraphTask_DestroyData, STATGROUP_TaskGraphTasks); }
	static FORCEINLINE ENamedThreads::Type GetDesiredThread() { return ENamedThreads::GameThread; }
	static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::FireAndForget; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
//		//Global::Log(EHierarchicalCalltraceVerbosity::DEBUG, "FAsyncGraphTask_DestroyData", "DoTask", TEXT(""));

		if (IsValid(contextData))
		{
			if (reasonForDeletion.IsEmpty())
			{
				//Global::Log(Debug, PurposeLog, "FAsyncGraphTask_DestroyData", "DoTask", TEXT("Destroying %s"), *contextData->GetPurposeChainName());
			}
			else
			{
				//Global::Log(Debug, PurposeLog, "FAsyncGraphTask_DestroyData", "DoTask", TEXT("Destroying %s; %s"), *contextData->GetPurposeChainName(), *reasonForDeletion);
			}
			contextData->RemoveFromRoot();
			contextData->MarkAsGarbage();
		}
		else
		{
			//Global::Log(Debug, PurposeLog, "FAsyncGraphTask_DestroyData", "DoTask", TEXT("Context Data was not valid"));
		}
	}
};

/// <summary>
/// ASyncGraphTask_ReOccurrence is used to notify a Level Director that an Event should have its Candidates reevaluate the Objectives of that Event
/// </summary>
class FAsyncGraphTask_ReOccurrence
{
protected:
	int indexOfEvent = -1;
	TWeakObjectPtr<class ADirector_Level> levelDirector = nullptr;
	FPurposeEvaluationThread& callingThread;///Used to detect stopThread
	bool shouldAbandon = false;

public:
	FAsyncGraphTask_ReOccurrence(int inEventIndex, TWeakObjectPtr<class ADirector_Level> inLevelDirector, FPurposeEvaluationThread& inCallingThread)
		: indexOfEvent(inEventIndex),
		levelDirector(inLevelDirector),
		callingThread(inCallingThread)
	{
//		callingThread.reOccurrenceTasks.Add(this);
	}

	virtual ~FAsyncGraphTask_ReOccurrence()
	{
//		//Global::Log(EHierarchicalCalltraceVerbosity::DEBUG, "FAsyncGraphTask_PurposeSelected", "~FAsyncGraphTask_PurposeSelected", TEXT(""));
//		callingThread.reOccurrenceTasks.Remove(this);
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncGraphTask_ReOccurrence, STATGROUP_TaskGraphTasks); }
	static FORCEINLINE ENamedThreads::Type GetDesiredThread() { return ENamedThreads::GameThread; }
	static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::FireAndForget; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
//		//Global::Log(EHierarchicalCalltraceVerbosity::DEBUG, "FAsyncGraphTask_PurposeSelected", "DoTask", TEXT(""));

		shouldAbandon ? Cancel() : ReOccurrence();
	}

	void ReOccurrence();

	/// Can be called by any thread in order to ensure that the purpose chain halts
	/// When the task attempts to DoTask(), it will instead cancel
	void Abandon() { shouldAbandon = true; }

protected:
	void Cancel()
	{
//		//Global::Log(EHierarchicalCalltraceVerbosity::DEBUG, "FAsyncGraphTask_PurposeSelected", "Cancel", TEXT(""));
	}

};

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

