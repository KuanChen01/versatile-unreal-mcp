#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UK2Node_CallFunction;

/**
 * Handler class for Blueprint Node-related MCP commands
 */
class UNREALMCP_API FUnrealMCPBlueprintNodeCommands
{
public:
	FUnrealMCPBlueprintNodeCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleConnectBlueprintNodes(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddBlueprintGetSelfComponentReference(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddBlueprintEvent(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddBlueprintFunctionCall(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddBlueprintVariable(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddBlueprintInputActionNode(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddBlueprintSelfReference(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleFindBlueprintNodes(const TSharedPtr<FJsonObject>& Params);

	/** Atomically (best-effort) rebuild event graph from a declarative graph_spec. */
	TSharedPtr<FJsonObject> HandleRebuildBlueprintGraph(const TSharedPtr<FJsonObject>& Params);

	/** Batch-connect existing nodes by GUID (or by local id map from a prior rebuild). */
	TSharedPtr<FJsonObject> HandleBatchConnectBlueprintNodes(const TSharedPtr<FJsonObject>& Params);

	// --- graph_spec helpers ---
	bool ClearEventGraphNodes(UBlueprint* Blueprint, UEdGraph* EventGraph, FString& OutError) const;
	UEdGraphNode* CreateNodeFromSpec(
		UBlueprint* Blueprint,
		UEdGraph* EventGraph,
		const TSharedPtr<FJsonObject>& NodeSpec,
		FString& OutLocalId,
		FString& OutError) const;
	bool ApplyCallFunctionDefaults(
		UEdGraph* EventGraph,
		UK2Node_CallFunction* FunctionNode,
		const TSharedPtr<FJsonObject>& ParamsObj,
		FString& OutError) const;
	UClass* ResolveFunctionTargetClass(const FString& Target) const;
	UFunction* ResolveFunction(UClass* TargetClass, UBlueprint* Blueprint, const FString& FunctionName) const;
	bool ConnectByLocalIds(
		UEdGraph* EventGraph,
		const TMap<FString, UEdGraphNode*>& IdToNode,
		const TSharedPtr<FJsonObject>& ConnectionSpec,
		FString& OutError) const;
};
