#include "Commands/UnrealMCPBlueprintNodeCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "GameFramework/InputSettings.h"
#include "Camera/CameraActor.h"
#include "Kismet/GameplayStatics.h"
#include "EdGraphSchema_K2.h"

// Declare the log category
DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCP, Log, All);

FUnrealMCPBlueprintNodeCommands::FUnrealMCPBlueprintNodeCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintNodeCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("connect_blueprint_nodes"))
    {
        return HandleConnectBlueprintNodes(Params);
    }
    else if (CommandType == TEXT("add_blueprint_get_self_component_reference"))
    {
        return HandleAddBlueprintGetSelfComponentReference(Params);
    }
    else if (CommandType == TEXT("add_blueprint_event_node"))
    {
        return HandleAddBlueprintEvent(Params);
    }
    else if (CommandType == TEXT("add_blueprint_function_node"))
    {
        return HandleAddBlueprintFunctionCall(Params);
    }
    else if (CommandType == TEXT("add_blueprint_variable"))
    {
        return HandleAddBlueprintVariable(Params);
    }
    else if (CommandType == TEXT("add_blueprint_input_action_node"))
    {
        return HandleAddBlueprintInputActionNode(Params);
    }
    else if (CommandType == TEXT("add_blueprint_self_reference"))
    {
        return HandleAddBlueprintSelfReference(Params);
    }
    else if (CommandType == TEXT("find_blueprint_nodes"))
    {
        return HandleFindBlueprintNodes(Params);
    }
    else if (CommandType == TEXT("rebuild_blueprint_graph"))
    {
        return HandleRebuildBlueprintGraph(Params);
    }
    else if (CommandType == TEXT("batch_connect_blueprint_nodes"))
    {
        return HandleBatchConnectBlueprintNodes(Params);
    }
    
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown blueprint node command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintNodeCommands::HandleConnectBlueprintNodes(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString SourceNodeId;
    if (!Params->TryGetStringField(TEXT("source_node_id"), SourceNodeId))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_node_id' parameter"));
    }

    FString TargetNodeId;
    if (!Params->TryGetStringField(TEXT("target_node_id"), TargetNodeId))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_node_id' parameter"));
    }

    FString SourcePinName;
    if (!Params->TryGetStringField(TEXT("source_pin"), SourcePinName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_pin' parameter"));
    }

    FString TargetPinName;
    if (!Params->TryGetStringField(TEXT("target_pin"), TargetPinName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_pin' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the event graph
    UEdGraph* EventGraph = FUnrealMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
    if (!EventGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get event graph"));
    }

    // Find the nodes
    UEdGraphNode* SourceNode = nullptr;
    UEdGraphNode* TargetNode = nullptr;
    for (UEdGraphNode* Node : EventGraph->Nodes)
    {
        if (Node->NodeGuid.ToString() == SourceNodeId)
        {
            SourceNode = Node;
        }
        else if (Node->NodeGuid.ToString() == TargetNodeId)
        {
            TargetNode = Node;
        }
    }

    if (!SourceNode || !TargetNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Source or target node not found"));
    }

    // Connect the nodes
    if (FUnrealMCPCommonUtils::ConnectGraphNodes(EventGraph, SourceNode, SourcePinName, TargetNode, TargetPinName))
    {
        // Mark the blueprint as modified
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("source_node_id"), SourceNodeId);
        ResultObj->SetStringField(TEXT("target_node_id"), TargetNodeId);
        return ResultObj;
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to connect nodes"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintNodeCommands::HandleAddBlueprintGetSelfComponentReference(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Get position parameters (optional)
    FVector2D NodePosition(0.0f, 0.0f);
    if (Params->HasField(TEXT("node_position")))
    {
        NodePosition = FUnrealMCPCommonUtils::GetVector2DFromJson(Params, TEXT("node_position"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the event graph
    UEdGraph* EventGraph = FUnrealMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
    if (!EventGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get event graph"));
    }
    
    // We'll skip component verification since the GetAllNodes API may have changed in UE5.5
    
    // Create the variable get node directly
    UK2Node_VariableGet* GetComponentNode = NewObject<UK2Node_VariableGet>(EventGraph);
    if (!GetComponentNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create get component node"));
    }
    
    // Set up the variable reference properly for UE5.5
    FMemberReference& VarRef = GetComponentNode->VariableReference;
    VarRef.SetSelfMember(FName(*ComponentName));
    
    // Set node position
    GetComponentNode->NodePosX = NodePosition.X;
    GetComponentNode->NodePosY = NodePosition.Y;
    
    // Add to graph
    EventGraph->AddNode(GetComponentNode);
    GetComponentNode->CreateNewGuid();
    GetComponentNode->PostPlacedNewNode();
    GetComponentNode->AllocateDefaultPins();
    
    // Explicitly reconstruct node for UE5.5
    GetComponentNode->ReconstructNode();
    
    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("node_id"), GetComponentNode->NodeGuid.ToString());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintNodeCommands::HandleAddBlueprintEvent(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString EventName;
    if (!Params->TryGetStringField(TEXT("event_name"), EventName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'event_name' parameter"));
    }

    // Get position parameters (optional)
    FVector2D NodePosition(0.0f, 0.0f);
    if (Params->HasField(TEXT("node_position")))
    {
        NodePosition = FUnrealMCPCommonUtils::GetVector2DFromJson(Params, TEXT("node_position"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the event graph
    UEdGraph* EventGraph = FUnrealMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
    if (!EventGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get event graph"));
    }

    // Create the event node
    UK2Node_Event* EventNode = FUnrealMCPCommonUtils::CreateEventNode(EventGraph, EventName, NodePosition);
    if (!EventNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create event node"));
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("node_id"), EventNode->NodeGuid.ToString());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintNodeCommands::HandleAddBlueprintFunctionCall(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    // Get position parameters (optional)
    FVector2D NodePosition(0.0f, 0.0f);
    if (Params->HasField(TEXT("node_position")))
    {
        NodePosition = FUnrealMCPCommonUtils::GetVector2DFromJson(Params, TEXT("node_position"));
    }

    // Check for target parameter (optional)
    FString Target;
    Params->TryGetStringField(TEXT("target"), Target);

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the event graph
    UEdGraph* EventGraph = FUnrealMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
    if (!EventGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get event graph"));
    }

    // Find the function
    UFunction* Function = nullptr;
    UK2Node_CallFunction* FunctionNode = nullptr;
    
    // Add extensive logging for debugging
    UE_LOG(LogTemp, Display, TEXT("Looking for function '%s' in target '%s'"), 
           *FunctionName, Target.IsEmpty() ? TEXT("Blueprint") : *Target);
    
    // Check if we have a target class specified
    if (!Target.IsEmpty())
    {
        // Try to find the target class
        UClass* TargetClass = nullptr;
        
        // First try without a prefix
        TargetClass = FindFirstObjectSafe<UClass>(*Target);
        UE_LOG(LogTemp, Display, TEXT("Tried to find class '%s': %s"), 
               *Target, TargetClass ? TEXT("Found") : TEXT("Not found"));
        
        // If not found, try with U prefix (common convention for UE classes)
        if (!TargetClass && !Target.StartsWith(TEXT("U")))
        {
            FString TargetWithPrefix = FString(TEXT("U")) + Target;
            TargetClass = FindFirstObjectSafe<UClass>(*TargetWithPrefix);
            UE_LOG(LogTemp, Display, TEXT("Tried to find class '%s': %s"), 
                   *TargetWithPrefix, TargetClass ? TEXT("Found") : TEXT("Not found"));
        }
        
        // If still not found, try with common component names
        if (!TargetClass)
        {
            // Try some common component class names
            TArray<FString> PossibleClassNames;
            PossibleClassNames.Add(FString(TEXT("U")) + Target + TEXT("Component"));
            PossibleClassNames.Add(Target + TEXT("Component"));
            
            for (const FString& ClassName : PossibleClassNames)
            {
                TargetClass = FindFirstObjectSafe<UClass>(*ClassName);
                if (TargetClass)
                {
                    UE_LOG(LogTemp, Display, TEXT("Found class using alternative name '%s'"), *ClassName);
                    break;
                }
            }
        }
        
        // Special case handling for common classes like UGameplayStatics
        if (!TargetClass && Target == TEXT("UGameplayStatics"))
        {
            // For UGameplayStatics, use a direct reference to known class
            TargetClass = FindFirstObjectSafe<UClass>(TEXT("UGameplayStatics"));
            if (!TargetClass)
            {
                // Try loading it from its known package
                TargetClass = LoadObject<UClass>(nullptr, TEXT("/Script/Engine.GameplayStatics"));
                UE_LOG(LogTemp, Display, TEXT("Explicitly loading GameplayStatics: %s"), 
                       TargetClass ? TEXT("Success") : TEXT("Failed"));
            }
        }
        
        // If we found a target class, look for the function there
        if (TargetClass)
        {
            UE_LOG(LogTemp, Display, TEXT("Looking for function '%s' in class '%s'"), 
                   *FunctionName, *TargetClass->GetName());
                   
            // First try exact name
            Function = TargetClass->FindFunctionByName(*FunctionName);
            
            // If not found, try class hierarchy
            UClass* CurrentClass = TargetClass;
            while (!Function && CurrentClass)
            {
                UE_LOG(LogTemp, Display, TEXT("Searching in class: %s"), *CurrentClass->GetName());
                
                // Try exact match
                Function = CurrentClass->FindFunctionByName(*FunctionName);
                
                // Try case-insensitive match
                if (!Function)
                {
                    for (TFieldIterator<UFunction> FuncIt(CurrentClass); FuncIt; ++FuncIt)
                    {
                        UFunction* AvailableFunc = *FuncIt;
                        UE_LOG(LogTemp, Display, TEXT("  - Available function: %s"), *AvailableFunc->GetName());
                        
                        if (AvailableFunc->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
                        {
                            UE_LOG(LogTemp, Display, TEXT("  - Found case-insensitive match: %s"), *AvailableFunc->GetName());
                            Function = AvailableFunc;
                            break;
                        }
                    }
                }
                
                // Move to parent class
                CurrentClass = CurrentClass->GetSuperClass();
            }
            
            // Special handling for known functions
            if (!Function)
            {
                if (TargetClass->GetName() == TEXT("GameplayStatics") && 
                    (FunctionName == TEXT("GetActorOfClass") || FunctionName.Equals(TEXT("GetActorOfClass"), ESearchCase::IgnoreCase)))
                {
                    UE_LOG(LogTemp, Display, TEXT("Using special case handling for GameplayStatics::GetActorOfClass"));
                    
                    // Create the function node directly
                    FunctionNode = NewObject<UK2Node_CallFunction>(EventGraph);
                    if (FunctionNode)
                    {
                        // Direct setup for known function
                        FunctionNode->FunctionReference.SetExternalMember(
                            FName(TEXT("GetActorOfClass")), 
                            TargetClass
                        );
                        
                        FunctionNode->NodePosX = NodePosition.X;
                        FunctionNode->NodePosY = NodePosition.Y;
                        EventGraph->AddNode(FunctionNode);
                        FunctionNode->CreateNewGuid();
                        FunctionNode->PostPlacedNewNode();
                        FunctionNode->AllocateDefaultPins();
                        
                        UE_LOG(LogTemp, Display, TEXT("Created GetActorOfClass node directly"));
                        
                        // List all pins
                        for (UEdGraphPin* Pin : FunctionNode->Pins)
                        {
                            UE_LOG(LogTemp, Display, TEXT("  - Pin: %s, Direction: %d, Category: %s"), 
                                   *Pin->PinName.ToString(), (int32)Pin->Direction, *Pin->PinType.PinCategory.ToString());
                        }
                    }
                }
            }
        }
    }
    
    // If we still haven't found the function, try in the blueprint's class
    if (!Function && !FunctionNode)
    {
        UE_LOG(LogTemp, Display, TEXT("Trying to find function in blueprint class"));
        Function = Blueprint->GeneratedClass->FindFunctionByName(*FunctionName);
    }
    
    // Create the function call node if we found the function
    if (Function && !FunctionNode)
    {
        FunctionNode = FUnrealMCPCommonUtils::CreateFunctionCallNode(EventGraph, Function, NodePosition);
    }
    
    if (!FunctionNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Function not found: %s in target %s"), *FunctionName, Target.IsEmpty() ? TEXT("Blueprint") : *Target));
    }

    // Set parameters if provided
    if (Params->HasField(TEXT("params")))
    {
        const TSharedPtr<FJsonObject>* ParamsObj;
        if (Params->TryGetObjectField(TEXT("params"), ParamsObj))
        {
            // Process parameters
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Param : (*ParamsObj)->Values)
            {
                const FString& ParamName = Param.Key;
                const TSharedPtr<FJsonValue>& ParamValue = Param.Value;
                
                // Find the parameter pin
                UEdGraphPin* ParamPin = FUnrealMCPCommonUtils::FindPin(FunctionNode, ParamName, EGPD_Input);
                if (ParamPin)
                {
                    UE_LOG(LogTemp, Display, TEXT("Found parameter pin '%s' of category '%s'"), 
                           *ParamName, *ParamPin->PinType.PinCategory.ToString());
                    UE_LOG(LogTemp, Display, TEXT("  Current default value: '%s'"), *ParamPin->DefaultValue);
                    if (ParamPin->PinType.PinSubCategoryObject.IsValid())
                    {
                        UE_LOG(LogTemp, Display, TEXT("  Pin subcategory: '%s'"), 
                               *ParamPin->PinType.PinSubCategoryObject->GetName());
                    }
                    
                    // Set parameter based on type
                    if (ParamValue->Type == EJson::String)
                    {
                        FString StringVal = ParamValue->AsString();
                        UE_LOG(LogTemp, Display, TEXT("  Setting string parameter '%s' to: '%s'"), 
                               *ParamName, *StringVal);
                        
                        // Handle class reference parameters (e.g., ActorClass in GetActorOfClass)
                        if (ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
                        {
                            // For class references, we require the exact class name with proper prefix
                            // - Actor classes must start with 'A' (e.g., ACameraActor)
                            // - Non-actor classes must start with 'U' (e.g., UObject)
                            const FString& ClassName = StringVal;
                            
                            // TODO: This likely won't work in UE5.5+, so don't rely on it.
                            UClass* Class = FindFirstObjectSafe<UClass>(*ClassName);

                            if (!Class)
                            {
                                Class = LoadObject<UClass>(nullptr, *ClassName);
                                UE_LOG(LogUnrealMCP, Display, TEXT("FindObject<UClass> failed. Assuming soft path  path: %s"), *ClassName);
                            }
                            
                            // If not found, try with Engine module path
                            if (!Class)
                            {
                                FString EngineClassName = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
                                Class = LoadObject<UClass>(nullptr, *EngineClassName);
                                UE_LOG(LogUnrealMCP, Display, TEXT("Trying Engine module path: %s"), *EngineClassName);
                            }
                            
                            if (!Class)
                            {
                                UE_LOG(LogUnrealMCP, Error, TEXT("Failed to find class '%s'. Make sure to use the exact class name with proper prefix (A for actors, U for non-actors)"), *ClassName);
                                return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to find class '%s'"), *ClassName));
                            }

                            const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(EventGraph->GetSchema());
                            if (!K2Schema)
                            {
                                UE_LOG(LogUnrealMCP, Error, TEXT("Failed to get K2Schema"));
                                return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get K2Schema"));
                            }

                            K2Schema->TrySetDefaultObject(*ParamPin, Class);
                            if (ParamPin->DefaultObject != Class)
                            {
                                UE_LOG(LogUnrealMCP, Error, TEXT("Failed to set class reference for pin '%s' to '%s'"), *ParamPin->PinName.ToString(), *ClassName);
                                return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to set class reference for pin '%s'"), *ParamPin->PinName.ToString()));
                            }

                            UE_LOG(LogUnrealMCP, Log, TEXT("Successfully set class reference for pin '%s' to '%s'"), *ParamPin->PinName.ToString(), *ClassName);
                            continue;
                        }
                        else if (ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
                        {
                            // Ensure we're using an integer value (no decimal)
                            int32 IntValue = FMath::RoundToInt(ParamValue->AsNumber());
                            ParamPin->DefaultValue = FString::FromInt(IntValue);
                            UE_LOG(LogTemp, Display, TEXT("  Set integer parameter '%s' to: %d (string: '%s')"), 
                                   *ParamName, IntValue, *ParamPin->DefaultValue);
                        }
                        else if (ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Float)
                        {
                            // For other numeric types
                            float FloatValue = ParamValue->AsNumber();
                            ParamPin->DefaultValue = FString::SanitizeFloat(FloatValue);
                            UE_LOG(LogTemp, Display, TEXT("  Set float parameter '%s' to: %f (string: '%s')"), 
                                   *ParamName, FloatValue, *ParamPin->DefaultValue);
                        }
                        else if (ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
                        {
                            bool BoolValue = ParamValue->AsBool();
                            ParamPin->DefaultValue = BoolValue ? TEXT("true") : TEXT("false");
                            UE_LOG(LogTemp, Display, TEXT("  Set boolean parameter '%s' to: %s"), 
                                   *ParamName, *ParamPin->DefaultValue);
                        }
                        else if (ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && ParamPin->PinType.PinSubCategoryObject == TBaseStructure<FVector>::Get())
                        {
                            // Handle array parameters - like Vector parameters
                            const TArray<TSharedPtr<FJsonValue>>* ArrayValue;
                            if (ParamValue->TryGetArray(ArrayValue))
                            {
                                // Check if this could be a vector (array of 3 numbers)
                                if (ArrayValue->Num() == 3)
                                {
                                    // Create a proper vector string: (X=0.0,Y=0.0,Z=1000.0)
                                    float X = (*ArrayValue)[0]->AsNumber();
                                    float Y = (*ArrayValue)[1]->AsNumber();
                                    float Z = (*ArrayValue)[2]->AsNumber();
                                    
                                    FString VectorString = FString::Printf(TEXT("(X=%f,Y=%f,Z=%f)"), X, Y, Z);
                                    ParamPin->DefaultValue = VectorString;
                                    
                                    UE_LOG(LogTemp, Display, TEXT("  Set vector parameter '%s' to: %s"), 
                                           *ParamName, *VectorString);
                                    UE_LOG(LogTemp, Display, TEXT("  Final pin value: '%s'"), 
                                           *ParamPin->DefaultValue);
                                }
                                else
                                {
                                    UE_LOG(LogTemp, Warning, TEXT("Array parameter type not fully supported yet"));
                                }
                            }
                        }
                    }
                    else if (ParamValue->Type == EJson::Number)
                    {
                        // Handle integer vs float parameters correctly
                        if (ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
                        {
                            // Ensure we're using an integer value (no decimal)
                            int32 IntValue = FMath::RoundToInt(ParamValue->AsNumber());
                            ParamPin->DefaultValue = FString::FromInt(IntValue);
                            UE_LOG(LogTemp, Display, TEXT("  Set integer parameter '%s' to: %d (string: '%s')"), 
                                   *ParamName, IntValue, *ParamPin->DefaultValue);
                        }
                        else
                        {
                            // For other numeric types
                            float FloatValue = ParamValue->AsNumber();
                            ParamPin->DefaultValue = FString::SanitizeFloat(FloatValue);
                            UE_LOG(LogTemp, Display, TEXT("  Set float parameter '%s' to: %f (string: '%s')"), 
                                   *ParamName, FloatValue, *ParamPin->DefaultValue);
                        }
                    }
                    else if (ParamValue->Type == EJson::Boolean)
                    {
                        bool BoolValue = ParamValue->AsBool();
                        ParamPin->DefaultValue = BoolValue ? TEXT("true") : TEXT("false");
                        UE_LOG(LogTemp, Display, TEXT("  Set boolean parameter '%s' to: %s"), 
                               *ParamName, *ParamPin->DefaultValue);
                    }
                    else if (ParamValue->Type == EJson::Array)
                    {
                        UE_LOG(LogTemp, Display, TEXT("  Processing array parameter '%s'"), *ParamName);
                        // Handle array parameters - like Vector parameters
                        const TArray<TSharedPtr<FJsonValue>>* ArrayValue;
                        if (ParamValue->TryGetArray(ArrayValue))
                        {
                            // Check if this could be a vector (array of 3 numbers)
                            if (ArrayValue->Num() == 3 && 
                                (ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct) &&
                                (ParamPin->PinType.PinSubCategoryObject == TBaseStructure<FVector>::Get()))
                            {
                                // Create a proper vector string: (X=0.0,Y=0.0,Z=1000.0)
                                float X = (*ArrayValue)[0]->AsNumber();
                                float Y = (*ArrayValue)[1]->AsNumber();
                                float Z = (*ArrayValue)[2]->AsNumber();
                                
                                FString VectorString = FString::Printf(TEXT("(X=%f,Y=%f,Z=%f)"), X, Y, Z);
                                ParamPin->DefaultValue = VectorString;
                                
                                UE_LOG(LogTemp, Display, TEXT("  Set vector parameter '%s' to: %s"), 
                                       *ParamName, *VectorString);
                                UE_LOG(LogTemp, Display, TEXT("  Final pin value: '%s'"), 
                                       *ParamPin->DefaultValue);
                            }
                            else
                            {
                                UE_LOG(LogTemp, Warning, TEXT("Array parameter type not fully supported yet"));
                            }
                        }
                    }
                    // Add handling for other types as needed
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("Parameter pin '%s' not found"), *ParamName);
                }
            }
        }
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("node_id"), FunctionNode->NodeGuid.ToString());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintNodeCommands::HandleAddBlueprintVariable(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString VariableName;
    if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
    }

    FString VariableType;
    if (!Params->TryGetStringField(TEXT("variable_type"), VariableType))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_type' parameter"));
    }

    // Get optional parameters
    bool IsExposed = false;
    if (Params->HasField(TEXT("is_exposed")))
    {
        IsExposed = Params->GetBoolField(TEXT("is_exposed"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Create variable based on type
    FEdGraphPinType PinType;
    
    // Set up pin type based on variable_type string
    if (VariableType == TEXT("Boolean"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    }
    else if (VariableType == TEXT("Integer") || VariableType == TEXT("Int"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
    }
    else if (VariableType == TEXT("Float"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
    }
    else if (VariableType == TEXT("String"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_String;
    }
    else if (VariableType == TEXT("Vector"))
    {
        PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported variable type: %s"), *VariableType));
    }

    // Create the variable
    FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VariableName), PinType);

    // Set variable properties
    FBPVariableDescription* NewVar = nullptr;
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (Variable.VarName == FName(*VariableName))
        {
            NewVar = &Variable;
            break;
        }
    }

    if (NewVar)
    {
        // Set exposure in editor
        if (IsExposed)
        {
            NewVar->PropertyFlags |= CPF_Edit;
        }
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("variable_name"), VariableName);
    ResultObj->SetStringField(TEXT("variable_type"), VariableType);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintNodeCommands::HandleAddBlueprintInputActionNode(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ActionName;
    if (!Params->TryGetStringField(TEXT("action_name"), ActionName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'action_name' parameter"));
    }

    // Get position parameters (optional)
    FVector2D NodePosition(0.0f, 0.0f);
    if (Params->HasField(TEXT("node_position")))
    {
        NodePosition = FUnrealMCPCommonUtils::GetVector2DFromJson(Params, TEXT("node_position"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the event graph
    UEdGraph* EventGraph = FUnrealMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
    if (!EventGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get event graph"));
    }

    // Create the input action node
    UK2Node_InputAction* InputActionNode = FUnrealMCPCommonUtils::CreateInputActionNode(EventGraph, ActionName, NodePosition);
    if (!InputActionNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create input action node"));
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("node_id"), InputActionNode->NodeGuid.ToString());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintNodeCommands::HandleAddBlueprintSelfReference(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    // Get position parameters (optional)
    FVector2D NodePosition(0.0f, 0.0f);
    if (Params->HasField(TEXT("node_position")))
    {
        NodePosition = FUnrealMCPCommonUtils::GetVector2DFromJson(Params, TEXT("node_position"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the event graph
    UEdGraph* EventGraph = FUnrealMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
    if (!EventGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get event graph"));
    }

    // Create the self node
    UK2Node_Self* SelfNode = FUnrealMCPCommonUtils::CreateSelfReferenceNode(EventGraph, NodePosition);
    if (!SelfNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create self node"));
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("node_id"), SelfNode->NodeGuid.ToString());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintNodeCommands::HandleFindBlueprintNodes(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString NodeType;
    if (!Params->TryGetStringField(TEXT("node_type"), NodeType))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_type' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the event graph
    UEdGraph* EventGraph = FUnrealMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
    if (!EventGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get event graph"));
    }

    // Create a JSON array for the node GUIDs
    TArray<TSharedPtr<FJsonValue>> NodeGuidArray;
    
    // Filter nodes by the exact requested type
    if (NodeType == TEXT("Event"))
    {
        FString EventName;
        if (!Params->TryGetStringField(TEXT("event_name"), EventName))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'event_name' parameter for Event node search"));
        }
        
        // Look for nodes with exact event name (e.g., ReceiveBeginPlay)
        for (UEdGraphNode* Node : EventGraph->Nodes)
        {
            UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
            if (EventNode && EventNode->EventReference.GetMemberName() == FName(*EventName))
            {
                UE_LOG(LogTemp, Display, TEXT("Found event node with name %s: %s"), *EventName, *EventNode->NodeGuid.ToString());
                NodeGuidArray.Add(MakeShared<FJsonValueString>(EventNode->NodeGuid.ToString()));
            }
        }
    }
    // Add other node types as needed (InputAction, etc.)
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("node_guids"), NodeGuidArray);
    
    return ResultObj;
} 

// =============================================================================
// Blueprint graph_spec rebuild / batch connect (atomic-style authoring)
// =============================================================================

bool FUnrealMCPBlueprintNodeCommands::ClearEventGraphNodes(UBlueprint* Blueprint, UEdGraph* EventGraph, FString& OutError) const
{
    if (!Blueprint || !EventGraph)
    {
        OutError = TEXT("Invalid blueprint or event graph");
        return false;
    }

    // Copy first — RemoveNode mutates the Nodes array.
    TArray<UEdGraphNode*> NodesCopy = EventGraph->Nodes;
    for (UEdGraphNode* Node : NodesCopy)
    {
        if (Node)
        {
            FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
        }
    }
    return true;
}

UClass* FUnrealMCPBlueprintNodeCommands::ResolveFunctionTargetClass(const FString& Target) const
{
    if (Target.IsEmpty())
    {
        return nullptr;
    }

    UClass* TargetClass = FindFirstObjectSafe<UClass>(*Target);
    if (!TargetClass && !Target.StartsWith(TEXT("U")))
    {
        TargetClass = FindFirstObjectSafe<UClass>(*(FString(TEXT("U")) + Target));
    }
    if (!TargetClass)
    {
        TargetClass = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *Target));
    }
    if (!TargetClass && Target.Contains(TEXT("GameplayStatics")))
    {
        TargetClass = LoadObject<UClass>(nullptr, TEXT("/Script/Engine.GameplayStatics"));
    }
    if (!TargetClass && (Target.Contains(TEXT("KismetSystemLibrary")) || Target == TEXT("System")))
    {
        TargetClass = LoadObject<UClass>(nullptr, TEXT("/Script/Engine.KismetSystemLibrary"));
    }
    if (!TargetClass && Target.Contains(TEXT("KismetMathLibrary")))
    {
        TargetClass = LoadObject<UClass>(nullptr, TEXT("/Script/Engine.KismetMathLibrary"));
    }
    return TargetClass;
}

UFunction* FUnrealMCPBlueprintNodeCommands::ResolveFunction(UClass* TargetClass, UBlueprint* Blueprint, const FString& FunctionName) const
{
    if (FunctionName.IsEmpty())
    {
        return nullptr;
    }

    if (TargetClass)
    {
        if (UFunction* Function = TargetClass->FindFunctionByName(*FunctionName))
        {
            return Function;
        }
        for (UClass* Current = TargetClass; Current; Current = Current->GetSuperClass())
        {
            for (TFieldIterator<UFunction> FuncIt(Current); FuncIt; ++FuncIt)
            {
                if ((*FuncIt)->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
                {
                    return *FuncIt;
                }
            }
        }
    }

    if (Blueprint && Blueprint->GeneratedClass)
    {
        return Blueprint->GeneratedClass->FindFunctionByName(*FunctionName);
    }
    return nullptr;
}

bool FUnrealMCPBlueprintNodeCommands::ApplyCallFunctionDefaults(
    UEdGraph* EventGraph,
    UK2Node_CallFunction* FunctionNode,
    const TSharedPtr<FJsonObject>& ParamsObj,
    FString& OutError) const
{
    if (!FunctionNode || !ParamsObj.IsValid())
    {
        return true;
    }

    const UEdGraphSchema_K2* K2Schema = EventGraph ? Cast<const UEdGraphSchema_K2>(EventGraph->GetSchema()) : nullptr;

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Param : ParamsObj->Values)
    {
        UEdGraphPin* ParamPin = FUnrealMCPCommonUtils::FindPin(FunctionNode, Param.Key, EGPD_Input);
        if (!ParamPin || !Param.Value.IsValid())
        {
            continue;
        }

        if (Param.Value->Type == EJson::String)
        {
            const FString StringVal = Param.Value->AsString();
            if (ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
            {
                UClass* Class = FindFirstObjectSafe<UClass>(*StringVal);
                if (!Class)
                {
                    Class = LoadObject<UClass>(nullptr, *StringVal);
                }
                if (!Class)
                {
                    Class = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *StringVal));
                }
                if (!Class)
                {
                    OutError = FString::Printf(TEXT("Failed to resolve class default for pin '%s': %s"), *Param.Key, *StringVal);
                    return false;
                }
                if (K2Schema)
                {
                    K2Schema->TrySetDefaultObject(*ParamPin, Class);
                }
                else
                {
                    ParamPin->DefaultObject = Class;
                }
            }
            else
            {
                ParamPin->DefaultValue = StringVal;
            }
        }
        else if (Param.Value->Type == EJson::Number)
        {
            if (ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
            {
                ParamPin->DefaultValue = FString::FromInt(FMath::RoundToInt(Param.Value->AsNumber()));
            }
            else
            {
                ParamPin->DefaultValue = FString::SanitizeFloat(static_cast<float>(Param.Value->AsNumber()));
            }
        }
        else if (Param.Value->Type == EJson::Boolean)
        {
            ParamPin->DefaultValue = Param.Value->AsBool() ? TEXT("true") : TEXT("false");
        }
        else if (Param.Value->Type == EJson::Array)
        {
            const TArray<TSharedPtr<FJsonValue>>* ArrayValue = nullptr;
            if (Param.Value->TryGetArray(ArrayValue) && ArrayValue && ArrayValue->Num() == 3 &&
                ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct &&
                ParamPin->PinType.PinSubCategoryObject == TBaseStructure<FVector>::Get())
            {
                const float X = (*ArrayValue)[0]->AsNumber();
                const float Y = (*ArrayValue)[1]->AsNumber();
                const float Z = (*ArrayValue)[2]->AsNumber();
                ParamPin->DefaultValue = FString::Printf(TEXT("(X=%f,Y=%f,Z=%f)"), X, Y, Z);
            }
        }
    }
    return true;
}

UEdGraphNode* FUnrealMCPBlueprintNodeCommands::CreateNodeFromSpec(
    UBlueprint* Blueprint,
    UEdGraph* EventGraph,
    const TSharedPtr<FJsonObject>& NodeSpec,
    FString& OutLocalId,
    FString& OutError) const
{
    if (!NodeSpec.IsValid())
    {
        OutError = TEXT("Invalid node spec");
        return nullptr;
    }

    if (!NodeSpec->TryGetStringField(TEXT("id"), OutLocalId) || OutLocalId.IsEmpty())
    {
        OutError = TEXT("Each node requires a non-empty 'id'");
        return nullptr;
    }

    FString NodeType;
    if (!NodeSpec->TryGetStringField(TEXT("type"), NodeType))
    {
        OutError = FString::Printf(TEXT("Node '%s' missing 'type'"), *OutLocalId);
        return nullptr;
    }

    FVector2D Position(0.f, 0.f);
    if (NodeSpec->HasField(TEXT("position")))
    {
        Position = FUnrealMCPCommonUtils::GetVector2DFromJson(NodeSpec, TEXT("position"));
    }
    else if (NodeSpec->HasField(TEXT("node_position")))
    {
        Position = FUnrealMCPCommonUtils::GetVector2DFromJson(NodeSpec, TEXT("node_position"));
    }

    const FString TypeLower = NodeType.ToLower();

    if (TypeLower == TEXT("event"))
    {
        FString EventName;
        if (!NodeSpec->TryGetStringField(TEXT("event_name"), EventName))
        {
            OutError = FString::Printf(TEXT("Event node '%s' missing event_name"), *OutLocalId);
            return nullptr;
        }
        // Prefer existing event if present (when not clearing graph)
        if (UK2Node_Event* Existing = FUnrealMCPCommonUtils::FindExistingEventNode(EventGraph, EventName))
        {
            Existing->NodePosX = Position.X;
            Existing->NodePosY = Position.Y;
            return Existing;
        }
        UK2Node_Event* EventNode = FUnrealMCPCommonUtils::CreateEventNode(EventGraph, EventName, Position);
        if (!EventNode)
        {
            OutError = FString::Printf(TEXT("Failed to create event '%s'"), *EventName);
        }
        return EventNode;
    }

    if (TypeLower == TEXT("function") || TypeLower == TEXT("call_function") || TypeLower == TEXT("function_call"))
    {
        FString FunctionName;
        if (!NodeSpec->TryGetStringField(TEXT("function_name"), FunctionName))
        {
            OutError = FString::Printf(TEXT("Function node '%s' missing function_name"), *OutLocalId);
            return nullptr;
        }
        FString Target;
        NodeSpec->TryGetStringField(TEXT("target"), Target);

        UClass* TargetClass = ResolveFunctionTargetClass(Target);
        UFunction* Function = ResolveFunction(TargetClass, Blueprint, FunctionName);
        UK2Node_CallFunction* FunctionNode = nullptr;

        if (Function)
        {
            FunctionNode = FUnrealMCPCommonUtils::CreateFunctionCallNode(EventGraph, Function, Position);
        }
        else if (TargetClass)
        {
            // External member reference even if FindFunctionByName failed in hierarchy edge cases
            FunctionNode = NewObject<UK2Node_CallFunction>(EventGraph);
            FunctionNode->FunctionReference.SetExternalMember(FName(*FunctionName), TargetClass);
            FunctionNode->NodePosX = Position.X;
            FunctionNode->NodePosY = Position.Y;
            EventGraph->AddNode(FunctionNode);
            FunctionNode->CreateNewGuid();
            FunctionNode->PostPlacedNewNode();
            FunctionNode->AllocateDefaultPins();
        }

        if (!FunctionNode)
        {
            OutError = FString::Printf(TEXT("Function not found: %s (target=%s)"), *FunctionName, *Target);
            return nullptr;
        }

        if (NodeSpec->HasField(TEXT("params")))
        {
            const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
            if (NodeSpec->TryGetObjectField(TEXT("params"), ParamsObj) && ParamsObj)
            {
                if (!ApplyCallFunctionDefaults(EventGraph, FunctionNode, *ParamsObj, OutError))
                {
                    return nullptr;
                }
            }
        }
        return FunctionNode;
    }

    if (TypeLower == TEXT("self"))
    {
        return FUnrealMCPCommonUtils::CreateSelfReferenceNode(EventGraph, Position);
    }

    if (TypeLower == TEXT("input_action") || TypeLower == TEXT("input"))
    {
        FString ActionName;
        if (!NodeSpec->TryGetStringField(TEXT("action_name"), ActionName))
        {
            OutError = FString::Printf(TEXT("input_action node '%s' missing action_name"), *OutLocalId);
            return nullptr;
        }
        return FUnrealMCPCommonUtils::CreateInputActionNode(EventGraph, ActionName, Position);
    }

    if (TypeLower == TEXT("get_component") || TypeLower == TEXT("component_get") || TypeLower == TEXT("get_self_component"))
    {
        FString ComponentName;
        if (!NodeSpec->TryGetStringField(TEXT("component_name"), ComponentName))
        {
            OutError = FString::Printf(TEXT("get_component node '%s' missing component_name"), *OutLocalId);
            return nullptr;
        }
        UK2Node_VariableGet* GetComponentNode = NewObject<UK2Node_VariableGet>(EventGraph);
        GetComponentNode->VariableReference.SetSelfMember(FName(*ComponentName));
        GetComponentNode->NodePosX = Position.X;
        GetComponentNode->NodePosY = Position.Y;
        EventGraph->AddNode(GetComponentNode);
        GetComponentNode->CreateNewGuid();
        GetComponentNode->PostPlacedNewNode();
        GetComponentNode->AllocateDefaultPins();
        GetComponentNode->ReconstructNode();
        return GetComponentNode;
    }

    if (TypeLower == TEXT("variable_get") || TypeLower == TEXT("get_variable"))
    {
        FString VariableName;
        if (!NodeSpec->TryGetStringField(TEXT("variable_name"), VariableName))
        {
            OutError = FString::Printf(TEXT("variable_get node '%s' missing variable_name"), *OutLocalId);
            return nullptr;
        }
        UK2Node_VariableGet* Node = FUnrealMCPCommonUtils::CreateVariableGetNode(EventGraph, Blueprint, VariableName, Position);
        if (!Node)
        {
            // Fallback self member (component-style)
            Node = NewObject<UK2Node_VariableGet>(EventGraph);
            Node->VariableReference.SetSelfMember(FName(*VariableName));
            Node->NodePosX = Position.X;
            Node->NodePosY = Position.Y;
            EventGraph->AddNode(Node);
            Node->CreateNewGuid();
            Node->PostPlacedNewNode();
            Node->AllocateDefaultPins();
            Node->ReconstructNode();
        }
        return Node;
    }

    if (TypeLower == TEXT("variable_set") || TypeLower == TEXT("set_variable"))
    {
        FString VariableName;
        if (!NodeSpec->TryGetStringField(TEXT("variable_name"), VariableName))
        {
            OutError = FString::Printf(TEXT("variable_set node '%s' missing variable_name"), *OutLocalId);
            return nullptr;
        }
        UK2Node_VariableSet* Node = FUnrealMCPCommonUtils::CreateVariableSetNode(EventGraph, Blueprint, VariableName, Position);
        if (!Node)
        {
            OutError = FString::Printf(TEXT("Failed to create variable_set for '%s' (variable may not exist)"), *VariableName);
        }
        return Node;
    }

    OutError = FString::Printf(
        TEXT("Unsupported node type '%s' on id '%s'. Supported: event, function, self, input_action, get_component, variable_get, variable_set"),
        *NodeType, *OutLocalId);
    return nullptr;
}

bool FUnrealMCPBlueprintNodeCommands::ConnectByLocalIds(
    UEdGraph* EventGraph,
    const TMap<FString, UEdGraphNode*>& IdToNode,
    const TSharedPtr<FJsonObject>& ConnectionSpec,
    FString& OutError) const
{
    if (!ConnectionSpec.IsValid())
    {
        OutError = TEXT("Invalid connection spec");
        return false;
    }

    FString SourceId;
    FString TargetId;
    FString SourcePin;
    FString TargetPin;

    // Preferred shape: { "from": {"node","pin"}, "to": {"node","pin"} }
    const TSharedPtr<FJsonObject>* FromObj = nullptr;
    const TSharedPtr<FJsonObject>* ToObj = nullptr;
    if (ConnectionSpec->TryGetObjectField(TEXT("from"), FromObj) && ConnectionSpec->TryGetObjectField(TEXT("to"), ToObj))
    {
        (*FromObj)->TryGetStringField(TEXT("node"), SourceId);
        (*FromObj)->TryGetStringField(TEXT("pin"), SourcePin);
        (*ToObj)->TryGetStringField(TEXT("node"), TargetId);
        (*ToObj)->TryGetStringField(TEXT("pin"), TargetPin);
    }
    else
    {
        // Flat shape used by incremental tools
        ConnectionSpec->TryGetStringField(TEXT("source_node_id"), SourceId);
        ConnectionSpec->TryGetStringField(TEXT("target_node_id"), TargetId);
        ConnectionSpec->TryGetStringField(TEXT("source_pin"), SourcePin);
        ConnectionSpec->TryGetStringField(TEXT("target_pin"), TargetPin);
        if (SourceId.IsEmpty())
        {
            ConnectionSpec->TryGetStringField(TEXT("from_node"), SourceId);
        }
        if (TargetId.IsEmpty())
        {
            ConnectionSpec->TryGetStringField(TEXT("to_node"), TargetId);
        }
    }

    if (SourceId.IsEmpty() || TargetId.IsEmpty() || SourcePin.IsEmpty() || TargetPin.IsEmpty())
    {
        OutError = TEXT("Connection requires from/to node and pin (or source_node_id/source_pin/target_node_id/target_pin)");
        return false;
    }

    UEdGraphNode* const* SourceNode = IdToNode.Find(SourceId);
    UEdGraphNode* const* TargetNode = IdToNode.Find(TargetId);

    // Also allow connecting by raw NodeGuid strings
    auto FindByGuid = [&](const FString& Id) -> UEdGraphNode*
    {
        for (UEdGraphNode* Node : EventGraph->Nodes)
        {
            if (Node && Node->NodeGuid.ToString() == Id)
            {
                return Node;
            }
        }
        return nullptr;
    };

    UEdGraphNode* Source = SourceNode ? *SourceNode : FindByGuid(SourceId);
    UEdGraphNode* Target = TargetNode ? *TargetNode : FindByGuid(TargetId);

    if (!Source || !Target)
    {
        OutError = FString::Printf(TEXT("Connection nodes not found: %s -> %s"), *SourceId, *TargetId);
        return false;
    }

    if (!FUnrealMCPCommonUtils::ConnectGraphNodes(EventGraph, Source, SourcePin, Target, TargetPin))
    {
        // Try common exec pin aliases
        const TArray<TPair<FString, FString>> Aliases = {
            {TEXT("then"), TEXT("execute")},
            {TEXT("execute"), TEXT("then")},
            {TEXT("Then"), TEXT("execute")},
            {TEXT("Execute"), TEXT("then")},
        };
        bool bConnected = false;
        for (const auto& Alias : Aliases)
        {
            if (SourcePin.Equals(Alias.Key, ESearchCase::IgnoreCase) || TargetPin.Equals(Alias.Value, ESearchCase::IgnoreCase))
            {
                if (FUnrealMCPCommonUtils::ConnectGraphNodes(EventGraph, Source, Alias.Key, Target, Alias.Value))
                {
                    bConnected = true;
                    break;
                }
            }
        }
        if (!bConnected)
        {
            OutError = FString::Printf(TEXT("Failed to connect %s.%s -> %s.%s"), *SourceId, *SourcePin, *TargetId, *TargetPin);
            return false;
        }
    }
    return true;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintNodeCommands::HandleRebuildBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    if (!Params->HasTypedField<EJson::Object>(TEXT("graph_spec")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'graph_spec' object parameter"));
    }
    TSharedPtr<FJsonObject> GraphSpec = Params->GetObjectField(TEXT("graph_spec"));
    if (!GraphSpec.IsValid())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid graph_spec"));
    }

    int32 Version = 1;
    if (GraphSpec->HasField(TEXT("version")))
    {
        Version = static_cast<int32>(GraphSpec->GetNumberField(TEXT("version")));
    }
    if (Version != 1)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Unsupported graph_spec version; expected version 1"));
    }

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    UEdGraph* EventGraph = FUnrealMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
    if (!EventGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get event graph"));
    }

    bool bClear = false;
    bool bCompile = true;
    if (GraphSpec->HasTypedField<EJson::Object>(TEXT("options")))
    {
        TSharedPtr<FJsonObject> Options = GraphSpec->GetObjectField(TEXT("options"));
        if (Options->HasField(TEXT("clear_event_graph")))
        {
            bClear = Options->GetBoolField(TEXT("clear_event_graph"));
        }
        if (Options->HasField(TEXT("compile")))
        {
            bCompile = Options->GetBoolField(TEXT("compile"));
        }
    }
    // Top-level convenience flags
    if (Params->HasField(TEXT("clear_event_graph")))
    {
        bClear = Params->GetBoolField(TEXT("clear_event_graph"));
    }
    if (Params->HasField(TEXT("compile")))
    {
        bCompile = Params->GetBoolField(TEXT("compile"));
    }

    // Variables first (so get/set nodes can resolve)
    if (GraphSpec->HasTypedField<EJson::Array>(TEXT("variables")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
        if (GraphSpec->TryGetArrayField(TEXT("variables"), Variables) && Variables)
        {
            for (const TSharedPtr<FJsonValue>& VarValue : *Variables)
            {
                const TSharedPtr<FJsonObject>* VarObj = nullptr;
                if (!VarValue->TryGetObject(VarObj) || !VarObj)
                {
                    continue;
                }
                FString VariableName;
                FString VariableType = TEXT("Boolean");
                bool bExposed = false;
                (*VarObj)->TryGetStringField(TEXT("name"), VariableName);
                (*VarObj)->TryGetStringField(TEXT("type"), VariableType);
                if ((*VarObj)->HasField(TEXT("is_exposed")))
                {
                    bExposed = (*VarObj)->GetBoolField(TEXT("is_exposed"));
                }
                if (VariableName.IsEmpty())
                {
                    continue;
                }

                // Skip if already exists
                bool bExists = false;
                for (const FBPVariableDescription& Existing : Blueprint->NewVariables)
                {
                    if (Existing.VarName == FName(*VariableName))
                    {
                        bExists = true;
                        break;
                    }
                }
                if (bExists)
                {
                    continue;
                }

                FEdGraphPinType PinType;
                if (VariableType == TEXT("Boolean") || VariableType == TEXT("Bool"))
                {
                    PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
                }
                else if (VariableType == TEXT("Integer") || VariableType == TEXT("Int"))
                {
                    PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
                }
                else if (VariableType == TEXT("Float") || VariableType == TEXT("Double"))
                {
                    PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
                }
                else if (VariableType == TEXT("String"))
                {
                    PinType.PinCategory = UEdGraphSchema_K2::PC_String;
                }
                else if (VariableType == TEXT("Vector"))
                {
                    PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
                    PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
                }
                else
                {
                    return FUnrealMCPCommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Unsupported variable type in graph_spec: %s"), *VariableType));
                }

                FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*VariableName), PinType);
                if (bExposed)
                {
                    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
                    {
                        if (Variable.VarName == FName(*VariableName))
                        {
                            Variable.PropertyFlags |= CPF_Edit;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (bClear)
    {
        FString ClearError;
        if (!ClearEventGraphNodes(Blueprint, EventGraph, ClearError))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ClearError);
        }
    }

    TMap<FString, UEdGraphNode*> IdToNode;
    TArray<TSharedPtr<FJsonValue>> CreatedNodes;

    if (GraphSpec->HasTypedField<EJson::Array>(TEXT("nodes")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (GraphSpec->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes)
        {
            for (const TSharedPtr<FJsonValue>& NodeValue : *Nodes)
            {
                const TSharedPtr<FJsonObject>* NodeObj = nullptr;
                if (!NodeValue->TryGetObject(NodeObj) || !NodeObj)
                {
                    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Each nodes[] entry must be an object"));
                }

                FString LocalId;
                FString NodeError;
                UEdGraphNode* Created = CreateNodeFromSpec(Blueprint, EventGraph, *NodeObj, LocalId, NodeError);
                if (!Created)
                {
                    return FUnrealMCPCommonUtils::CreateErrorResponse(NodeError);
                }
                if (IdToNode.Contains(LocalId))
                {
                    return FUnrealMCPCommonUtils::CreateErrorResponse(
                        FString::Printf(TEXT("Duplicate node id in graph_spec: %s"), *LocalId));
                }
                IdToNode.Add(LocalId, Created);

                TSharedPtr<FJsonObject> NodeResult = MakeShared<FJsonObject>();
                NodeResult->SetStringField(TEXT("id"), LocalId);
                NodeResult->SetStringField(TEXT("node_id"), Created->NodeGuid.ToString());
                FString TypeStr;
                (*NodeObj)->TryGetStringField(TEXT("type"), TypeStr);
                NodeResult->SetStringField(TEXT("type"), TypeStr);
                CreatedNodes.Add(MakeShared<FJsonValueObject>(NodeResult));
            }
        }
    }

    TArray<TSharedPtr<FJsonValue>> ConnectionResults;
    int32 ConnectionFailures = 0;
    if (GraphSpec->HasTypedField<EJson::Array>(TEXT("connections")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
        if (GraphSpec->TryGetArrayField(TEXT("connections"), Connections) && Connections)
        {
            for (const TSharedPtr<FJsonValue>& ConnValue : *Connections)
            {
                const TSharedPtr<FJsonObject>* ConnObj = nullptr;
                if (!ConnValue->TryGetObject(ConnObj) || !ConnObj)
                {
                    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Each connections[] entry must be an object"));
                }

                FString ConnError;
                TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
                if (ConnectByLocalIds(EventGraph, IdToNode, *ConnObj, ConnError))
                {
                    One->SetBoolField(TEXT("success"), true);
                }
                else
                {
                    One->SetBoolField(TEXT("success"), false);
                    One->SetStringField(TEXT("error"), ConnError);
                    ConnectionFailures++;
                }
                ConnectionResults.Add(MakeShared<FJsonValueObject>(One));
            }
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    bool bCompileOk = true;
    FString CompileMessage;
    if (bCompile)
    {
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        bCompileOk = (Blueprint->Status != BS_Error);
        if (!bCompileOk)
        {
            CompileMessage = TEXT("Blueprint compiled with errors (BS_Error)");
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), ConnectionFailures == 0 && bCompileOk);
    ResultObj->SetStringField(TEXT("blueprint_name"), BlueprintName);
    ResultObj->SetNumberField(TEXT("nodes_created"), CreatedNodes.Num());
    ResultObj->SetNumberField(TEXT("connection_failures"), ConnectionFailures);
    ResultObj->SetBoolField(TEXT("cleared_event_graph"), bClear);
    ResultObj->SetBoolField(TEXT("compiled"), bCompile);
    ResultObj->SetBoolField(TEXT("compile_ok"), bCompileOk);
    if (!CompileMessage.IsEmpty())
    {
        ResultObj->SetStringField(TEXT("compile_message"), CompileMessage);
    }
    ResultObj->SetArrayField(TEXT("nodes"), CreatedNodes);
    ResultObj->SetArrayField(TEXT("connections"), ConnectionResults);

    // Also expose id -> guid map
    TSharedPtr<FJsonObject> IdMap = MakeShared<FJsonObject>();
    for (const TPair<FString, UEdGraphNode*>& Pair : IdToNode)
    {
        if (Pair.Value)
        {
            IdMap->SetStringField(Pair.Key, Pair.Value->NodeGuid.ToString());
        }
    }
    ResultObj->SetObjectField(TEXT("id_to_node_id"), IdMap);

    if (ConnectionFailures > 0 && bCompileOk)
    {
        ResultObj->SetStringField(TEXT("message"),
            FString::Printf(TEXT("Graph rebuilt with %d connection failure(s)"), ConnectionFailures));
    }
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintNodeCommands::HandleBatchConnectBlueprintNodes(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    if (!Params->HasTypedField<EJson::Array>(TEXT("connections")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'connections' array"));
    }

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    UEdGraph* EventGraph = FUnrealMCPCommonUtils::FindOrCreateEventGraph(Blueprint);
    if (!EventGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get event graph"));
    }

    // Optional id map: { "local_id": "guid", ... }
    TMap<FString, UEdGraphNode*> IdToNode;
    if (Params->HasTypedField<EJson::Object>(TEXT("id_to_node_id")))
    {
        TSharedPtr<FJsonObject> IdMap = Params->GetObjectField(TEXT("id_to_node_id"));
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : IdMap->Values)
        {
            if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String)
            {
                continue;
            }
            const FString GuidStr = Pair.Value->AsString();
            for (UEdGraphNode* Node : EventGraph->Nodes)
            {
                if (Node && Node->NodeGuid.ToString() == GuidStr)
                {
                    IdToNode.Add(Pair.Key, Node);
                    break;
                }
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
    Params->TryGetArrayField(TEXT("connections"), Connections);

    TArray<TSharedPtr<FJsonValue>> Results;
    int32 Failures = 0;
    if (Connections)
    {
        for (const TSharedPtr<FJsonValue>& ConnValue : *Connections)
        {
            const TSharedPtr<FJsonObject>* ConnObj = nullptr;
            if (!ConnValue->TryGetObject(ConnObj) || !ConnObj)
            {
                Failures++;
                continue;
            }
            FString Err;
            TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
            if (ConnectByLocalIds(EventGraph, IdToNode, *ConnObj, Err))
            {
                One->SetBoolField(TEXT("success"), true);
            }
            else
            {
                One->SetBoolField(TEXT("success"), false);
                One->SetStringField(TEXT("error"), Err);
                Failures++;
            }
            Results.Add(MakeShared<FJsonValueObject>(One));
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), Failures == 0);
    ResultObj->SetNumberField(TEXT("connection_failures"), Failures);
    ResultObj->SetArrayField(TEXT("connections"), Results);
    return ResultObj;
}
