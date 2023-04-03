// Copyright Jordan Cain. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Purpose/PurposeAsset.h"
#include "Purpose/Assets/GoalAsset.h"
#include "EventAsset.generated.h"

UENUM(BlueprintType)
/// <summary>
/// These layers serve as identifiers for which the Purpose Management outside the PurposeSystem uses to provide different logic dependent on the layer
/// </summary>
enum class EPurposeLayer : uint8
{
	Event = 0
	, Goal = 1
	, Objective = 2
	, Behavior = 3
};

UENUM(BlueprintType)
/// <summary>
/// This enum allows for representation of different groups within an Event
/// </summary>
enum class EEventGroup : uint8
{
	None = 99,/// Not 0, as Group ABC... need to align with indices in array of subpurposes, which starts at 0
	GroupA = 0,
	GroupB = 1,
	GroupC = 2,
	GroupD = 3,
	GroupE = 4,
	GroupF = 5,
	GroupG = 6
	, GroupH = 7
	, GroupI = 8
	, GroupJ = 9
};

USTRUCT()
/// <summary>
/// Links two groups together by a relationship
/// Utilized by FEventRelationships
/// </summary>
struct FGroupRelationship
{
	GENERATED_BODY()
public:

	FGroupRelationship(){}
	FGroupRelationship(EEventGroup inGroup1, EEventGroup inGroup2)
		:group1(inGroup1),
		group2(inGroup2)
	{}

	UPROPERTY(EditAnywhere)
		EEventGroup group1 = EEventGroup::None;

	UPROPERTY(EditAnywhere)
		EGroupRelationship relationship = EGroupRelationship::None;

	UPROPERTY(EditAnywhere)
		EEventGroup group2 = EEventGroup::None;

	FORCEINLINE bool operator ==(const FGroupRelationship& other) const
	{
		return group1 == other.group1 && group2 == other.group2;
	}
};

/// May be able to just convert these structs to fPurpose in the purpose system after getting all the subs
	/// So get sub purposes, conver to base fpurpose, pass through system
	/// How can we get the sub purposes of those though?
	/// After storing all the Events, we could provide a reference of that array of Events to the background thread, or at least a reference to access it. Then we just ask for tthe sub layer of a PurposeAddress we've been provided

USTRUCT(BlueprintType)
struct FTaskLayer : public FPurpose
{
	GENERATED_BODY()
public:

	//UPROPERTY(EditAnywhere, meta=(InlineEditConditionToggle, EditConditionHides, EditCondition = "!canEditSubPurposes"))
	//TObjectPtr<class UBehaviorTree> BT = nullptr;
	UPROPERTY(EditAnywhere, meta = (DisplayPriority = "0"))
	TObjectPtr<class UBehavior_AI> behaviorAbility = nullptr;

};

USTRUCT(BlueprintType)
struct FTargetingParameters
{
	GENERATED_BODY()

	FTargetingParameters()
	{
	}

	UPROPERTY(EditAnywhere, DisplayName = "Query to find targets for Purpose")
		TObjectPtr<class UEnvQuery> targetingQuery = nullptr;

	UPROPERTY(EditAnywhere, DisplayName = "Optional Subject Location Radius")
		/// Subject will be sought in ContextData, and ActorLocation will be sought for subject
		ESubject targetLocation = ESubject::None;

	UPROPERTY(EditAnywhere, DisplayName = "Intent against Group")
		/// Rather than a direct action between individuals, this allows us to generalize to an objective level of action without committing to a specific task
		EIntentTowardsGroup intent = EIntentTowardsGroup::None;
};


USTRUCT(BlueprintType)
struct FObjectiveLayer : public FPurpose
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, meta = (TitleProperty = "purpose.descriptionOfPurpose", DisplayPriority = "0"))
	TArray<FTaskLayer> tasks;

	UPROPERTY(EditAnywhere, meta = (DisplayPriority = "1"))
	FTargetingParameters targetingParams;
};

USTRUCT(BlueprintType)
struct FGoalLayer : public FPurpose
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, meta = (TitleProperty = "purpose.descriptionOfPurpose", DisplayPriority = "0"))
	TArray<FObjectiveLayer> objectives;
};

USTRUCT(BlueprintType)
struct FEventLayer : public FPurpose
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, meta = (TitleProperty = "purpose.descriptionOfPurpose", DisplayPriority = "0"))
	TArray<FGoalLayer> goals;

	UPROPERTY(EditAnywhere, meta = (DisplayPriority = "1"))
	/// Groups are dictated by the index of the Goal
	/// So the first Goal is GroupA, and the second Goal is GroupB
	TArray<FGroupRelationship> GroupRelationships;

	EEventGroup GroupingForGoal(FPurposeAddress inGoal)
	{
		//int32 goalIndex = goals.IndexOfByKey(inGoal.GetAddressOfThisPurpose());
		int32 goalIndex = inGoal.GetAddressOfThisPurpose();

		if (goals.IsValidIndex(goalIndex))
		{
			Global::LogError(PURPOSE, "FEventLayer", "GroupingForGoal", TEXT("GoalIndex: %d not found!."), goalIndex);
			return EEventGroup::None;
		}
		////Global::Log(Debug, PurposeLog, *this, "GroupingForGoal", TEXT("GoalIndex: %d."), goalIndex);
		return (goalIndex == INDEX_NONE || goalIndex <= -1) ? EEventGroup::None : StaticCast<EEventGroup>(goalIndex);
	}

	EGroupRelationship RelationshipBetweenGroups(EEventGroup group1, EEventGroup group2)
	{
		//Global::Log(FULLTRACE, PurposeLog, *this, "RelationshipBetweenGroups", TEXT("Seeking Groups: %s & %s."), *Global::EnumValueOnly<EEventGroup>(group1), *Global::EnumValueOnly<EEventGroup>(group2));

		if (FGroupRelationship* grouping = GroupRelationships.FindByKey(FGroupRelationship(group1, group2)))
		{
			//Global::Log(FULLTRACE, PurposeLog, *this, "RelationshipBetweenGroups", TEXT("Grouping found! %s."), *Global::EnumValueOnly<EGroupRelationship>(grouping->relationship));
			return grouping->relationship;
		}
		else if (FGroupRelationship* grouping2 = GroupRelationships.FindByKey(FGroupRelationship(group2, group1)))/// In the case that group relatinship is declared in reverse
		{
			//Global::Log(FULLTRACE, PurposeLog, *this, "RelationshipBetweenGroups", TEXT("Grouping found! %s."), *Global::EnumValueOnly<EGroupRelationship>(grouping2->relationship));
			return grouping2->relationship;
		}

		return EGroupRelationship::None;
	}
};

UCLASS()
/// The Event purpose is the highest layer, establishing conditions to represent specific happenings
/// EventAssets are made up of GoalAssets in order to establish differing perspectives against one purpose
class UEventAsset : public UDataAsset
{
	GENERATED_BODY()
public:

	UEventAsset();

public:
	UPROPERTY(EditAnywhere)
	FEventLayer eventLayer;

	/// Static definitions to establish consistency when seeking assets
	/// Event and Reaction are the only two types an asset manager will need to discover
	/// All other asset types are contained by Event Structure
	static const FPrimaryAssetType EventAssetType;

	static FPrimaryAssetType PrimaryAssetType() { return EventAssetType; }
	//FPrimaryAssetId GetPrimaryAssetId() const final { return FPrimaryAssetId(EventAssetType, EventAssetType); }
	FPrimaryAssetId GetPrimaryAssetId() const final { return FPrimaryAssetId(PrimaryAssetType(), GetFName()); }

};
