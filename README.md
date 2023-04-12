# Purpose-System
The branch of the Purpose System repository represents my revised logical framework for a video game AI behavior selection system.

# Version 2 Outline

_ The main difference of version 2 is that there is only the Behavior layer of purpose. The design choice is meant to enforce the idea that every action can create a reaction, which then becomes a recursive system of action->reaction->action.

## FPurpose

Purpose is nothing other than a set of criteria that are to be scored to represent the viability of the purpose it represents. The purpose in this instance is a behavior, which are an AI executable behavior (I used gameplay abilities but you could also use Behavior Trees or most anything, even just inputs).

## FContextData

Context data is a representation of a purpose linked to a specific group of subjects. I use context data as a storage for a purpose that was selected via utility criteria scoring against that group of subjects deemed relevant at the time of an occurrence. An occurrence being any action an AI performs, from shooting at another character or making a verbal declaration. So context data is meant literally as the context of an occurrence.

## ESubject and FDataMapEntry

Context data recognizes "subjects" by a map of key ESubject with a value of TArray<FDataMapEntry>. In my use case I actually store a TScriptInterface<IDataMapInterface> which requires the implementer to return a TArray<FDataMapEntry>. This way I can get up to data values on a subject. FDataMapEntry holds a UDataChunk, which in my use case is a generic virtual UObject serving as a container to any type of data, from a primitive float to the current velocity of the character. The purpose of UDataChunk is to store all this data in a searchable array. Primarily UDataChunks are used by my conditions, which are established against UDataChunks which are then sought on the specified subject by class.

## Purpose Establishment
  
Purposes that can be selected for an AI are established in 2 ways, either globally through DataAssets, or local to a level via an AI Activity actor. Both provide the Level Director with an FPurpose which is then cached for as long as the level director is active.
  
## Conditions of a Purpose

What is most important about this version of the Purpose System is how conditions are meant to be designed. Conditions are meant to imitate the decision making process of a human being reacting to any action. This whole revision is actually inspired by the philosophy that every human action is just a reaction. Occurrences aren't a declaration, but a simple notification that something has occurred, and we should now determine if we want to respond by making what observations we can and grouping them into a single behavior. Conditions are these observations and should attempt to roughly outline what the occurrence was in the context of a reaction

## Purpose Selection

Given the previous subjects+datamaps and conditions, selection is just a matter of queuing these occurrences and scoring the conditions against the subjects+datamaps. Starting with an occurrence, we gather a number of subjects (with their data maps), any extra FDataMapEntries that are specific to the context but not necessarily a subject (such as time of occurrence) actually winds up referenced via a ESubject::Context), and queue them onto the background threads. 

Importantly, the Purpose System framework is encapsulated in that each logical step, from queuing to distribution, do not require external input. The Purpose component binds to an ability activation callback of the TasksComponent, so on any ability activation an Occurrence is created and sent to the Purpose threads
  
### Queuing Purposes

I queue purposes based on the current number of queued items on a thread, so that when multiple threads are established for Purpose selection it will automatically distribute the items evenly.
  
  
### Distribution of Purposes

Once the most relevant purpose to an Occurrence is established, if it is not identical to the behavior already being performed, the purpose becomes the candidates current behavior and is immediately executed.
