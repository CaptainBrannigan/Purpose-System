// Copyright Jordan Cain. All Rights Reserved.

#pragma once

#include "Engine.h"
#include "Net/UnrealNetwork.h"
#include "DataMapInterface.h"
#include "AISpawn.h"
#include "Purpose/Assets/EventAsset.h"
#include "Purpose/PurposeAbilityComponent.h"
#include "Engine/DeveloperSettings.h"
#include "Purpose/PurposeEvaluationThread.h"
#include "Manager.generated.h"

///The Manager class is the foundation of all actor gameplay. They manage all spawning, controlling, and requests of AI or Players.
///This allows us to establish a floodgate for logic, so all debugging of gameplay can be traced through that channel.
UCLASS(NotPlaceable)
class AManager : public AActor, public IDataMapInterface, public IPurposeManagementInterface
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AManager();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void BeginDestroy() override;


	UPROPERTY(Replicated)
	///This map is the link between a managed actor and their character data
	///This establishes exclusive management of data through the manager
	TArray<TObjectPtr<UPurposeAbilityComponent>> ownedPurposeCandidates;

	// Called every frame
	virtual void Tick(float DeltaTime) override;

#pragma region Purpose
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
	virtual TArray<FSubjectMap> GetUniqueSubjectsRequiredForSubPurposeSelection(const int PurposeLayerForUniqueSubjects, const FContextData& parentContext, TScriptInterface<IDataMapInterface> candidate, FPurposeAddress addressOfSubPurpose) override;

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
	FContextData& GetStoredPurpose(const int64 uniqueIdentifierOfContextTree, const FPurposeAddress& fullAddress, const int layerToRetrieveFor) final;

	/// @param parentAddress: An address which is either that of a purpose containing behaviors so that it may reference the parent, or the parent address itself
	/// @param TArray<TObjectPtr<UGA_Behavior>>: All the behaviors contained by the parent indicated
	TArray<TObjectPtr<class UBehavior_AI>> GetBehaviorsFromParent(const FPurposeAddress& parentAddress) final { return GetHeadOfPurposeManagment()->GetBehaviorsFromParent(parentAddress); }

	/// @param parentAddress: An address of the behavior containing purpose
	/// @param TObjectPtr<UGA_Behavior>: The behavior contained by the address provided
	TObjectPtr<class UBehavior_AI> GetBehaviorAtAddress(const FPurposeAddress& inAddress) final { return GetHeadOfPurposeManagment()->GetBehaviorAtAddress(inAddress); }

	///@return bool: Always false, as managers only receive Goals, which aren't an executable behavior, but rather just a filter
	bool DoesPurposeAlreadyExist(const FContextData& primary, const FSubjectMap& secondarySubjects, const TArray<FDataMapEntry>& secondaryContext, const FPurposeAddress optionalAddress = FPurposeAddress()) final { return false; }

	/// Managers will require access to the director in order to further purpose evaluation
	void EstablishAccessToPurposeThreads(TObjectPtr<class ADirector_Level> inDirector) { director = inDirector; }

	void ReevaluateObjectivesForAllCandidates(const FPurposeAddress& addressOfGoal, const int64& uniqueIDofActivePurpose);

	/// @param goalClasses: if contained, notify all managed actors that Objectives of these Goals are to be ended;
	void EndGoalsOfEvent(const int64& uniqueContextID, const FPurposeAddress& eventAddress);

	const TArray<FDataMapEntry>& DataMap() final { return data; }
	TArray<FDataMapEntry> DataMapCopy() final { return data; }

	UFUNCTION(Server, Reliable)
	void AddData(UDataChunk* inData, bool overwriteValue = true) final;

	UFUNCTION(Server, Reliable)
	void AppendData(const TArray<FDataMapEntry>& inDataMap, bool overwriteValue = true) final;

	UFUNCTION(Server, Reliable)
	void RemoveData(TSubclassOf<UDataChunk> inClass) final;

	/// @param source: actor we wish to utilize as source of targeting
	/// @param inGoal: Provides a source to determine group relationships relative to Parent Event
	/// @param targetingParams: Targeting parameters of an Objective to utilize
	TArray<TScriptInterface<IDataMapInterface>> PotentialObjectiveTargets(
		TObjectPtr<AActor> source
		, const FContextData& inGoal
		, FTargetingParameters targetingParams
	);
	/// By finding the Event of the inGoal, we establish which group the inGoal belongs to
	/// Then checking whether any of the Goals held by the target->Manager() belong to the Event
	/// We can establish if the relationship between inGoal and target->Manager()->eventGoal is the requested relationship
	/// @param source: actor we wish to utilize as source of targeting
	/// @param inGoal: Provides a source to determine group relationships relative to Parent Event
	/// @param groupRelationship; The relationship we wish to find
	bool TargetHasGroupRelationship(
		TObjectPtr<UPurposeAbilityComponent> target
		, const FContextData& inGoal
		, EGroupRelationship groupRelationship
	);


protected:

	UPROPERTY()
	/// Managers require a reference to the level director who controls them
	/// This allows them to pass any purpose evaluation logic through the director
	/// This ensure the director has full control over the background threads
	TObjectPtr<class ADirector_Level> director = nullptr;

	/// To select a reaction for the relevant actor:
	///  We need the asset manager to discover reaction assets
	///  Ensure ActorThread receives context
   void LatentGetReactionAssets(TObjectPtr<class UOccurrenceContext> inOccurrence);
   void LatentReaction(TObjectPtr<class UOccurrenceContext> occurrence, TArray<TObjectPtr<class UReactionAsset>> reactionAssets);

   /// Managers periodically will poll for EQS sight perceptions
   /// Super:: implementation determines if the EQS can be performed this tick
   virtual bool PerformVisualEQS();

   FGameTime timeSinceLastEQS;

   float timeBetweenEQSQueries = 0.25f;

   TWeakObjectPtr<class UEnvQuery> playerSightEQSCache = nullptr;

private:

	UPROPERTY(Replicated)
	///This data is representative of the subject "ESubject::Candidate"
	TArray<FDataMapEntry> data;

	/// Force implementers to provide an editable datamap
	/// The only way to the change the DataMap is through the Server RPCs
	TArray<FDataMapEntry>& DataMapInternal() final { return data; }

	/// Virtual so that individual manager types can determine when an actor should be ignored for an Objective selection
	virtual bool IgnoreActorForObjective(TObjectPtr<UPurposeAbilityComponent> actor, TObjectPtr<UContextData_Deprecated> inContext) { return false; }

#pragma endregion


};

