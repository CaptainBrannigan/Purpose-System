# Purpose-System
The repository represents my initial logical framework for an video game AI behavior selection system.

# Version 1 Outline

## FPurpose

Purpose is nothing other than a set of criteria that are to be scored linked to either another purpose, or to a behavior. When linked to another purpose, we establist a chain of purposes that can serve as an umbrella. In my use case I would use 4 layers of purpose: Event, Goal, Objective and Behavior. This allowed me to have multiple Goals which can establish when an Event is completed and the relationship between the Goals; Objectives which are meant to complete a Goal. I used Objectives to sort AI into behavior groups relative to the Goal; Behaviors are meant to complete an Objective, and are the end of the purpose chain resulting in an executable behavior (I used gameplay abilities but you could also use Behavior Trees or most anything, even just inputs. The point being Task is where the AI actually chooses to do something in game).

## FContextData

Context data is a representation of a purpose linked to a specific group of subjects. I use context data as a storage for a purpose that was selected via utility criteria scoring against that group of subjects deemed relevant at the time of an occurrence. An occurrence being anything from shooting at another character or making a verbal declaration. So context data is meant literally as the context of an occurrence.

## ESubject and FDataMapEntry

Context data recognizes "subjects" by a map of key ESubject with a value of TArray<FDataMapEntry>. In my use case I actually store a TScriptInterface<IDataMapInterface> which requires the implementer to return a TArray<FDataMapEntry>. This way I can get up to data values on a subject. FDataMapEntry holds a UDataChunk, which in my use case is a generic virtual UObject serving as a container to any type of data, from a primitive float to the current velocity of the character. The purpose of UDataChunk is to store all this data in a searchable array. Primarily UDataChunks are used by my conditions, which are established against UDataChunks which are then sought on the specified subject by class.

## FEventLayer

FEventLayer is the initial struct, derived of FPurpose, which will hold sub purpose Goals. Unfortuently, due to restrictions of the reflection system, FPurpose can not hold TArray<FPurpose>, so we have to create a separate struct for each layer. FEventLayer is used to establish a purpose chain in two ways, either an EventAsset (UDataAsset), or an AI activity actor placed in the world.
  
Important to note, both EventAssets and AI activities are meant to be gathered around begin play or before and stored in a TArray<FEventLayer> cache. Primarily this is because we use an address (FPurposeAddress) to refer to all purposes, and the head of the address (representing the FEventLayer) must be static and unique. With that specification, we can then find any purpose layer based on their index within the parent layer and the initial Event index. For context datas we add a further identifier that identifies each context as unique from one another. So we may have multiple stored context datas with the same Purpose Goal for different subjects.
  
## Purpose Selection

Given the previous two pillars, selection is just a matter of queuing and scoring. Starting with an occurrence, we gather a number of subjects (with their data maps), any extra FDataMapEntries that are specific to the context but not necessarily a subject (such as time of occurrence; actually winds up referenced via a ESubject::Context), and queue them onto the background threads. 

Importantly, the Purpose System framework is encapsulated in that each logical step, from queuing to distribution, do not require external input, only modification. Only Occurrences are currently required externally (in my case for AI Activities in the level). In the future I may look into a Behavior interface that will fully encapsulate Purpose System logic by forcing an Occurrence on execution.
  
### Queuing Purposes

I queue purposes based on their purpose layer (Event, Goal, Objective, Behavior). The super class I have for the background thread holds a TMap<int, TQueue>, so that I can have multiple threads which exist for specific layers. I then iterate through all provided threads and attempt to queue, if one fails then we try the next. The reason I separate by layer is so there are priorities in selecting purposes. The lowest being Event selection. When an Event is selected, I want every Goal to be distributed before the next Event is evaluated. Likewise, I want every Objective to be distributed prior to the next Goal. But for Behaviors I queue them on a separate thread as AI are meant to constantly be executing and reevaluating behaviors, and I want to isolate that from the rest of purpose selection.
  
### External Logic
  
#### Modifications to a context for Purpose Selection
  
When a purpose is about to be evaluated, we request some modifications from a "purpose owner". The primary modification is the establishment of candidates for purposes. This is important, as we wish to find a purpose for each candidate. The candidate then becomes the purpose owner of the selected purpose, and the context data is created and passed to the owner. Another important modification is establishing additional subjects for a purpose evaluation. An example from my use case is I have an instigator and target subject of an occurrence, but for purposes such as Objectives I also desire an ObjectiveTarget which may be the occurrence target or a different character, determined by the conditions of the Objective purpose being evaluated. These additional subjects I store as an entry I refer to as a UniqueSubject. So for each Candidate I retrieve additional subjects. For each of these additional subjects I create a single UniqueSubject entry with the current Candidate. This way I can now find the most suitable purpose with the most suitable subjects for 1 Candidate.
  
#### Distribution of Purposes
  
As the Purpose system is meant to be encapsulated, distribution is a one way transaction, where the receiver will store (and execute if a behavior).
  
