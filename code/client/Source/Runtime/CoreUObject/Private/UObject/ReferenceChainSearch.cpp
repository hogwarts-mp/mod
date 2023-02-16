// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ReferenceChainSearch.h"
#include "HAL/PlatformStackWalk.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/FastReferenceCollector.h"
#include "UObject/GCObject.h"
#include "HAL/ThreadHeartBeat.h"

DEFINE_LOG_CATEGORY_STATIC(LogReferenceChain, Log, All);

// Returns true if the object can't be collected by GC
static FORCEINLINE bool IsNonGCObject(UObject* Object, EReferenceChainSearchMode SearchMode)
{
	FUObjectItem* ObjectItem = GUObjectArray.ObjectToObjectItem(Object);
	return (ObjectItem->IsRootSet() ||
		ObjectItem->HasAnyFlags(EInternalObjectFlags::GarbageCollectionKeepFlags) ||
		(GARBAGE_COLLECTION_KEEPFLAGS != RF_NoFlags && Object->HasAnyFlags(GARBAGE_COLLECTION_KEEPFLAGS) && !(SearchMode & EReferenceChainSearchMode::FullChain))
		
		);
}

FReferenceChainSearch::FGraphNode* FReferenceChainSearch::FindOrAddNode(TMap<UObject*, FGraphNode*>& AllNodes, UObject* InObjectToFindNodeFor)
{
	FGraphNode* ObjectNode = nullptr;
	FGraphNode** ExistingObjectNode = AllNodes.Find(InObjectToFindNodeFor);
	if (ExistingObjectNode)
	{
		ObjectNode = *ExistingObjectNode;
		check(ObjectNode->Object == InObjectToFindNodeFor);
	}
	else
	{
		ObjectNode = new FGraphNode();
		ObjectNode->Object = InObjectToFindNodeFor;
		AllNodes.Add(InObjectToFindNodeFor, ObjectNode);
	}
	return ObjectNode;
}

int32 FReferenceChainSearch::BuildReferenceChains(FGraphNode* TargetNode, TArray<FReferenceChain*>& ProducedChains, int32 ChainDepth, const int32 VisitCounter, EReferenceChainSearchMode SearchMode)
{
	int32 ProducedChainsCount = 0;
	if (TargetNode->Visited != VisitCounter)
	{
		TargetNode->Visited = VisitCounter;

		// Stop at root objects
		if (!IsNonGCObject(TargetNode->Object, SearchMode))
		{			
			for (FGraphNode* ReferencedByNode : TargetNode->ReferencedByObjects)
			{
				// For each of the referencers of this node, duplicate the current chain and continue processing
				if (ReferencedByNode->Visited != VisitCounter)
				{
					int32 OldChainsCount = ProducedChains.Num();
					int32 NewChainsCount = BuildReferenceChains(ReferencedByNode, ProducedChains, ChainDepth + 1, VisitCounter, SearchMode);
					// Insert the current node to all chains produced recursively
					for (int32 NewChainIndex = OldChainsCount; NewChainIndex < (NewChainsCount + OldChainsCount); ++NewChainIndex)
					{
						FReferenceChain* Chain = ProducedChains[NewChainIndex];
						Chain->InsertNode(TargetNode);
					}
					ProducedChainsCount += NewChainsCount;
				}
			}			
		}
		else
		{
			// This is a root so we can construct a chain from this node up to the target node
			FReferenceChain* Chain = new FReferenceChain(ChainDepth);
			Chain->InsertNode(TargetNode);
			ProducedChains.Add(Chain);
			ProducedChainsCount = 1;
		}
	}

	return ProducedChainsCount;
}

void FReferenceChainSearch::RemoveChainsWithDuplicatedRoots(TArray<FReferenceChain*>& AllChains)
{
	// This is going to be rather slow but it depends on the number of chains which shouldn't be too bad (usually)
	for (int32 FirstChainIndex = 0; FirstChainIndex < AllChains.Num(); ++FirstChainIndex)
	{
		const FGraphNode* RootNode = AllChains[FirstChainIndex]->GetRootNode();
		for (int32 SecondChainIndex = AllChains.Num() - 1; SecondChainIndex > FirstChainIndex; --SecondChainIndex)
		{
			if (AllChains[SecondChainIndex]->GetRootNode() == RootNode)
			{
				delete AllChains[SecondChainIndex];
				AllChains.RemoveAt(SecondChainIndex);
			}
		}
	}
}

typedef TPair<FReferenceChainSearch::FGraphNode*, FReferenceChainSearch::FGraphNode*> FRootAndReferencerPair;

void FReferenceChainSearch::RemoveDuplicatedChains(TArray<FReferenceChain*>& AllChains)
{
	// We consider chains identical if the direct referencer of the target node and the root node are identical
	TMap<FRootAndReferencerPair, FReferenceChain*> UniqueChains;

	for (int32 ChainIndex = 0; ChainIndex < AllChains.Num(); ++ChainIndex)
	{
		FReferenceChain* Chain = AllChains[ChainIndex];
		FRootAndReferencerPair ChainRootAndReferencer(Chain->Nodes[1], Chain->Nodes.Last());
		FReferenceChain** ExistingChain = UniqueChains.Find(ChainRootAndReferencer);
		if (ExistingChain)
		{
			if ((*ExistingChain)->Nodes.Num() > Chain->Nodes.Num())
			{
				delete (*ExistingChain);
				UniqueChains[ChainRootAndReferencer] = Chain;
			}
			else
			{
				delete Chain;
			}
		}
		else
		{
			UniqueChains.Add(ChainRootAndReferencer, Chain);
		}
	}
	AllChains.Reset();
	UniqueChains.GenerateValueArray(AllChains);
}

void FReferenceChainSearch::BuildReferenceChains(FGraphNode* TargetNode, TArray<FReferenceChain*>& Chains, EReferenceChainSearchMode SearchMode)
{	
	TArray<FReferenceChain*> AllChains;

	// Recursively construct reference chains	
	int32 VisitCounter = 0;
	for (FGraphNode* ReferencedByNode : TargetNode->ReferencedByObjects)
	{
		TargetNode->Visited = ++VisitCounter;
		
		AllChains.Reset();
		const int32 MinChainDepth = 2; // The chain will contain at least the TargetNode and the ReferencedByNode
		BuildReferenceChains(ReferencedByNode, AllChains, MinChainDepth, VisitCounter, SearchMode);
		for (FReferenceChain* Chain : AllChains)
		{
			Chain->InsertNode(TargetNode);
		}

		// Filter based on search mode	
		if (!!(SearchMode & EReferenceChainSearchMode::ExternalOnly))
		{
			for (int32 ChainIndex = AllChains.Num() - 1; ChainIndex >= 0; --ChainIndex)
			{
				FReferenceChain* Chain = AllChains[ChainIndex];	
				if (!Chain->IsExternal())
				{
					// Discard the chain
					delete Chain;
					AllChains.RemoveAtSwap(ChainIndex);
				}
			}
		}

		Chains.Append(AllChains);
	}

	// Reject duplicates
	if (!!(SearchMode & (EReferenceChainSearchMode::Longest | EReferenceChainSearchMode::Shortest)))
	{
		RemoveChainsWithDuplicatedRoots(Chains);
	}
	else
	{
		RemoveDuplicatedChains(Chains);
	}

	// Sort all chains based on the search criteria
	if (!(SearchMode & EReferenceChainSearchMode::Longest))
	{
		// Sort from the shortest to the longest chain
		Chains.Sort([](const FReferenceChain& LHS, const FReferenceChain& RHS) { return LHS.Num() < RHS.Num(); });
	}
	else
	{
		// Sort from the longest to the shortest chain
		Chains.Sort([](const FReferenceChain& LHS, const FReferenceChain& RHS) { return LHS.Num() > RHS.Num(); });
	}

	// Finally, fill extended reference info for the remaining chains
	for (FReferenceChain* Chain : Chains)
	{
		Chain->FillReferenceInfo();
	}
}

void FReferenceChainSearch::BuildReferenceChainsForDirectReferences(FGraphNode* TargetNode, TArray<FReferenceChain*>& AllChains, EReferenceChainSearchMode SearchMode)
{
	for (FGraphNode* ReferencedByNode : TargetNode->ReferencedByObjects)
	{
		if (!(SearchMode & EReferenceChainSearchMode::ExternalOnly) || !ReferencedByNode->Object->IsIn(TargetNode->Object))
		{
			FReferenceChain* Chain = new FReferenceChain();
			Chain->AddNode(TargetNode);
			Chain->AddNode(ReferencedByNode);
			Chain->FillReferenceInfo();
			AllChains.Add(Chain);
		}
	}
}

FString FReferenceChainSearch::GetObjectFlags(UObject* InObject)
{
	FString Flags;
	if (InObject->IsRooted())
	{
		Flags += TEXT("(root) ");
	}

	CA_SUPPRESS(6011)
		if (InObject->IsNative())
		{
			Flags += TEXT("(native) ");
		}

	if (InObject->IsPendingKill())
	{
		Flags += TEXT("(PendingKill) ");
	}

	if (InObject->HasAnyFlags(RF_Standalone))
	{
		Flags += TEXT("(standalone) ");
	}

	if (InObject->HasAnyInternalFlags(EInternalObjectFlags::Async))
	{
		Flags += TEXT("(async) ");
	}

	if (InObject->HasAnyInternalFlags(EInternalObjectFlags::AsyncLoading))
	{
		Flags += TEXT("(asyncloading) ");
	}

	if (GUObjectArray.IsDisregardForGC(InObject))
	{
		Flags += TEXT("(NeverGCed) ");
	}

	FUObjectItem* ReferencedByObjectItem = GUObjectArray.ObjectToObjectItem(InObject);
	if (ReferencedByObjectItem->HasAnyFlags(EInternalObjectFlags::ClusterRoot))
	{
		Flags += TEXT("(ClusterRoot) ");
	}
	if (ReferencedByObjectItem->GetOwnerIndex() > 0)
	{
		Flags += TEXT("(Clustered) ");
	}
	return Flags;
}

void FReferenceChainSearch::DumpChain(FReferenceChain* Chain)
{
	if (Chain->Num())
	{
		// Roots are at the end so iterate from the last to the first node
		for (int32 NodeIndex = Chain->Num() - 1; NodeIndex >= 0; --NodeIndex)
		{
			UObject* Object = Chain->GetNode(NodeIndex)->Object;
			const FNodeReferenceInfo& ReferenceInfo = Chain->GetReferenceInfo(NodeIndex);

			UE_LOG(LogReferenceChain, Log, TEXT("%s%s%s%s"),
				FCString::Spc(FMath::Min<int32>(TCStringSpcHelper<TCHAR>::MAX_SPACES, Chain->Num() - NodeIndex - 1)),
				*GetObjectFlags(Object),
				*Object->GetFullName(),
				*ReferenceInfo.ToString()
			);
		}
		UE_LOG(LogReferenceChain, Log, TEXT("  "));
	}
}

void FReferenceChainSearch::WriteChain(FReferenceChain* Chain, FString& OutString)
{
	if (Chain->Num())
	{
		// Roots are at the end so iterate from the last to the first node
		for (int32 NodeIndex = Chain->Num() - 1; NodeIndex >= 0; --NodeIndex)
		{
			UObject* Object = Chain->GetNode(NodeIndex)->Object;
			const FNodeReferenceInfo& ReferenceInfo = Chain->GetReferenceInfo(NodeIndex);

			if (NodeIndex != Chain->Num() - 1)
			{
				OutString.Append(LINE_TERMINATOR);
				OutString.Append(FCString::Spc(Chain->Num() - NodeIndex - 1));
			}

			OutString.Append(*GetObjectFlags(Object));
			OutString.Append(*Object->GetFullName());
			OutString.Append(*ReferenceInfo.ToString());
		}
	}
}

void FReferenceChainSearch::FReferenceChain::FillReferenceInfo()
{
	// The first entry is the object we were looking for references to so add an empty entry for it
	ReferenceInfos.Add(FNodeReferenceInfo());

	// Iterate over all nodes and add reference info based on the next node (which is the object that referenced the current node)
	for (int32 NodeIndex = 1; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		FGraphNode* PreviousNode = Nodes[NodeIndex - 1];
		FGraphNode* CurrentNode = Nodes[NodeIndex];

		// Found the PreviousNode in the list of objects referenced by the CurrentNode
		FNodeReferenceInfo* FoundInfo = nullptr;
		for (FNodeReferenceInfo& Info : CurrentNode->ReferencedObjects)
		{
			if (Info.Object == PreviousNode)
			{
				FoundInfo = &Info;
				break;
			}
		}
		check(FoundInfo); // because there must have been a reference since we created this chain using it
		check(FoundInfo->Object == PreviousNode);
		ReferenceInfos.Add(*FoundInfo);
	}
	check(ReferenceInfos.Num() == Nodes.Num());
}

bool FReferenceChainSearch::FReferenceChain::IsExternal() const
{
	if (Nodes.Num() > 1)
	{
		// Reference is external if the root (the last node) is not in the first node (target)
		return !Nodes.Last()->Object->IsIn(Nodes[0]->Object);
	}
	else
	{
		return false;
	}
}

/**
* Handles UObject references found by TFastReferenceCollector
*/
class FDirectReferenceProcessor : public FSimpleReferenceProcessorBase
{	
	UObject* ObjectToFindReferencesTo;
	TSet<FReferenceChainSearch::FObjectReferenceInfo>& ReferencedObjects;

public:

	FDirectReferenceProcessor(UObject* InObjectToFindReferencesTo, TSet<FReferenceChainSearch::FObjectReferenceInfo>& InReferencedObjects)
		: ObjectToFindReferencesTo(InObjectToFindReferencesTo)
		, ReferencedObjects(InReferencedObjects)		
	{
	}
	FORCEINLINE void HandleTokenStreamObjectReference(TArray<UObject*>& ObjectsToSerialize, UObject* ReferencingObject, UObject*& Object, const int32 TokenIndex, bool bAllowReferenceElimination)
	{
		FReferenceChainSearch::FObjectReferenceInfo RefInfo(Object);
		if (Object && !ReferencedObjects.Contains(RefInfo))
		{
#if ENABLE_GC_OBJECT_CHECKS
			if (TokenIndex >= 0)
			{
				FTokenInfo TokenInfo = ReferencingObject->GetClass()->ReferenceTokenStream.GetTokenInfo(TokenIndex);
				RefInfo.ReferencerName = TokenInfo.Name;
				RefInfo.Type = FReferenceChainSearch::EReferenceType::Property;
			}
			else
			{
				RefInfo.Type = FReferenceChainSearch::EReferenceType::AddReferencedObjects;
				if (FGCObject::GGCObjectReferencer && (!ReferencingObject || ReferencingObject == FGCObject::GGCObjectReferencer))
				{
					FString RefName;
					if (FGCObject::GGCObjectReferencer->GetReferencerName(Object, RefName, true))
					{
						RefInfo.ReferencerName = FName(*RefName);
					}
					else if (ReferencingObject)
					{
						RefInfo.ReferencerName = ReferencingObject->GetFName();
					}
				}
				else if (ReferencingObject)
				{
					RefInfo.ReferencerName = ReferencingObject->GetFName();
				}
			}
#endif
			ReferencedObjects.Add(RefInfo);
		}
	}
};

typedef TDefaultReferenceCollector<FDirectReferenceProcessor> FDirectReferenceCollector;

FReferenceChainSearch::FReferenceChainSearch(UObject* InObjectToFindReferencesTo, EReferenceChainSearchMode Mode /*= EReferenceChainSearchMode::PrintResults*/)
	: ObjectToFindReferencesTo(InObjectToFindReferencesTo)
{
	check(InObjectToFindReferencesTo);

	PerformSearch(Mode);

	if (!!(Mode & (EReferenceChainSearchMode::PrintResults|EReferenceChainSearchMode::PrintAllResults)))
	{
		PrintResults(!!(Mode & EReferenceChainSearchMode::PrintAllResults));
	}
}

FReferenceChainSearch::~FReferenceChainSearch()
{
	Cleanup();
}

void FReferenceChainSearch::PerformSearch(EReferenceChainSearchMode SearchMode)
{
	FSlowHeartBeatScope DisableHangDetection; // This function can be very slow

	// First pass is to find all direct references for each object
	{
		FindDirectReferencesForObjects();
	}

	FGraphNode* ObjectNodeToFindReferencesTo = FindOrAddNode(AllNodes, ObjectToFindReferencesTo);
	check(ObjectNodeToFindReferencesTo);

	// Now it's time to build the reference chain from all of the objects that reference the object to find references to
	if (!(SearchMode & EReferenceChainSearchMode::Direct))
	{		
		BuildReferenceChains(ObjectNodeToFindReferencesTo, ReferenceChains, SearchMode);
	}
	else
	{
		BuildReferenceChainsForDirectReferences(ObjectNodeToFindReferencesTo, ReferenceChains, SearchMode);
	}
}

void FReferenceChainSearch::FindDirectReferencesForObjects()
{
	TSet<FObjectReferenceInfo> ReferencedObjects;
	FDirectReferenceProcessor Processor(ObjectToFindReferencesTo, ReferencedObjects);
	TFastReferenceCollector<
		FDirectReferenceProcessor, 
		FDirectReferenceCollector, 
		FGCArrayPool, 
		EFastReferenceCollectorOptions::AutogenerateTokenStream | EFastReferenceCollectorOptions::ProcessNoOpTokens
	> ReferenceCollector(Processor, FGCArrayPool::Get());
	FGCArrayStruct ArrayStruct;
	TArray<UObject*>& ObjectsToProcess = ArrayStruct.ObjectsToSerialize;

	for (FRawObjectIterator It; It; ++It)
	{
		FUObjectItem* ObjItem = *It;
		UObject* Object = static_cast<UObject*>(ObjItem->Object);
		FGraphNode* ObjectNode = FindOrAddNode(AllNodes, Object);

		// Find direct references
		ReferencedObjects.Reset();
		ObjectsToProcess.Reset();
		ObjectsToProcess.Add(Object);
		ReferenceCollector.CollectReferences(ArrayStruct);

		// Build the reference tree
		for (FObjectReferenceInfo& ReferenceInfo : ReferencedObjects)
		{
			FGraphNode* ReferencedObjectNode = FindOrAddNode(AllNodes, ReferenceInfo.Object);
			ObjectNode->ReferencedObjects.Add(FNodeReferenceInfo(ReferencedObjectNode, ReferenceInfo.Type, ReferenceInfo.ReferencerName));
			ReferencedObjectNode->ReferencedByObjects.Add(ObjectNode);
		}
	}
}

void FReferenceChainSearch::PrintResults(bool bDumpAllChains /*= false*/) const
{
	if (ReferenceChains.Num())
	{
		FSlowHeartBeatScope DisableHangDetection; // This function can be very slow

		const int32 MaxChainsToPrint = 100;
		int32 NumPrintedChains = 0;

		for (FReferenceChain* Chain : ReferenceChains)
		{
			if (bDumpAllChains || NumPrintedChains < MaxChainsToPrint)
			{
				DumpChain(Chain);
				NumPrintedChains++;
			}
			else
			{
				UE_LOG(LogReferenceChain, Log, TEXT("Referenced by %d more reference chain(s)."), ReferenceChains.Num() - NumPrintedChains);
				break;
			}
		}
	}
	else
	{
		check(ObjectToFindReferencesTo);
		UE_LOG(LogReferenceChain, Log, TEXT("%s%s is not currently reachable."),
			*GetObjectFlags(ObjectToFindReferencesTo),
			*ObjectToFindReferencesTo->GetFullName()
		);
	}
}

FString FReferenceChainSearch::GetRootPath() const
{
	if (ReferenceChains.Num())
	{
		FReferenceChain* Chain = ReferenceChains[0];
		FString OutString;

		WriteChain(Chain, OutString);
		return OutString;
	}
	else
	{
		return FString::Printf(TEXT("%s%s is not currently reachable."),
			*GetObjectFlags(ObjectToFindReferencesTo),
			*ObjectToFindReferencesTo->GetFullName()
		);
	}
}

void FReferenceChainSearch::Cleanup()
{
	for (int32 ChainIndex = 0; ChainIndex < ReferenceChains.Num(); ++ChainIndex)
	{
		delete ReferenceChains[ChainIndex];
	}
	ReferenceChains.Empty();

	for (TPair<UObject*, FGraphNode*>& ObjectNodePair : AllNodes)
	{
		delete ObjectNodePair.Value;
	}
	AllNodes.Empty();
}
