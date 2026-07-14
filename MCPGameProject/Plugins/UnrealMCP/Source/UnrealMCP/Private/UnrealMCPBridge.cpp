#include "UnrealMCPBridge.h"
#include "MCPServerRunnable.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
// Add Blueprint related includes
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
// UE5.5 correct includes
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
// Blueprint Graph specific includes
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "GameFramework/InputSettings.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Interfaces/IPluginManager.h"
// Include our new command handler classes
#include "Commands/UnrealMCPEditorCommands.h"
#include "Commands/UnrealMCPBlueprintCommands.h"
#include "Commands/UnrealMCPBlueprintNodeCommands.h"
#include "Commands/UnrealMCPMaterialCommands.h"
#include "Commands/UnrealMCPProjectCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Commands/UnrealMCPUMGCommands.h"

// Default settings
#define MCP_SERVER_HOST "127.0.0.1"
#define MCP_SERVER_PORT 55557

namespace
{
    // Must match Python bridge_protocol.PROTOCOL_VERSION (length-prefixed frames).
    const FString UnrealMCPProtocolVersion = TEXT("2.0");

    const TArray<FString>& GetEditorCommandTypes()
    {
        static const TArray<FString> CommandTypes = {
            TEXT("get_level_status"),
            TEXT("open_level"),
            TEXT("save_current_level"),
            TEXT("save_dirty_packages"),
            TEXT("get_play_state"),
            TEXT("start_pie"),
            TEXT("stop_pie"),
            TEXT("get_output_log"),
            TEXT("get_message_log"),
            TEXT("get_actors_in_level"),
            TEXT("find_actors_by_name"),
            TEXT("spawn_actor"),
            TEXT("create_actor"),
            TEXT("spawn_actor_by_class"),
            TEXT("delete_actor"),
            TEXT("set_actor_transform"),
            TEXT("get_actor_properties"),
            TEXT("set_actor_property"),
            TEXT("assign_material_to_actor"),
            TEXT("get_viewport_status"),
            TEXT("spawn_blueprint_actor"),
            TEXT("focus_viewport"),
            TEXT("take_screenshot")
        };
        return CommandTypes;
    }

    const TArray<FString>& GetBlueprintCommandTypes()
    {
        static const TArray<FString> CommandTypes = {
            TEXT("create_blueprint"),
            TEXT("add_component_to_blueprint"),
            TEXT("set_component_property"),
            TEXT("set_physics_properties"),
            TEXT("compile_blueprint"),
            TEXT("set_blueprint_property"),
            TEXT("set_static_mesh_properties"),
            TEXT("set_pawn_properties")
        };
        return CommandTypes;
    }

    const TArray<FString>& GetBlueprintNodeCommandTypes()
    {
        static const TArray<FString> CommandTypes = {
            TEXT("connect_blueprint_nodes"),
            TEXT("add_blueprint_get_self_component_reference"),
            TEXT("add_blueprint_self_reference"),
            TEXT("find_blueprint_nodes"),
            TEXT("add_blueprint_event_node"),
            TEXT("add_blueprint_input_action_node"),
            TEXT("add_blueprint_function_node"),
            TEXT("add_blueprint_get_component_node"),
            TEXT("add_blueprint_variable")
        };
        return CommandTypes;
    }

    const TArray<FString>& GetProjectCommandTypes()
    {
        static const TArray<FString> CommandTypes = {
            TEXT("create_input_mapping"),
            TEXT("find_assets"),
            TEXT("get_asset_info"),
            TEXT("delete_asset")
        };
        return CommandTypes;
    }

    const TArray<FString>& GetMaterialCommandTypes()
    {
        static const TArray<FString> CommandTypes = {
            TEXT("create_material"),
            TEXT("set_material_properties"),
            TEXT("add_material_expression"),
            TEXT("set_material_expression_property"),
            TEXT("connect_material_expressions"),
            TEXT("connect_material_property"),
            TEXT("recompile_material"),
            TEXT("configure_glass_material"),
            TEXT("rebuild_material_graph"),
            TEXT("get_material_compile_status"),
            TEXT("validate_material_graph"),
            TEXT("reload_asset_from_disk"),
            TEXT("close_asset_editor"),
            TEXT("is_asset_loaded_dirty"),
            TEXT("create_material_function"),
            TEXT("rebuild_material_function_graph")
        };
        return CommandTypes;
    }

    const TArray<FString>& GetUMGCommandTypes()
    {
        static const TArray<FString> CommandTypes = {
            TEXT("create_umg_widget_blueprint"),
            TEXT("add_text_block_to_widget"),
            TEXT("add_button_to_widget"),
            TEXT("bind_widget_event"),
            TEXT("set_text_block_binding"),
            TEXT("add_widget_to_viewport")
        };
        return CommandTypes;
    }

    bool CommandTypeInGroup(const FString& CommandType, const TArray<FString>& CommandTypes)
    {
        return CommandTypes.Contains(CommandType);
    }

    TArray<TSharedPtr<FJsonValue>> MakeStringArray(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> JsonArray;
        JsonArray.Reserve(Values.Num());
        for (const FString& Value : Values)
        {
            JsonArray.Add(MakeShared<FJsonValueString>(Value));
        }
        return JsonArray;
    }

    TSharedPtr<FJsonObject> BuildCommandGroupsObject()
    {
        TSharedPtr<FJsonObject> CommandsObject = MakeShared<FJsonObject>();
        CommandsObject->SetArrayField(TEXT("editor"), MakeStringArray(GetEditorCommandTypes()));
        CommandsObject->SetArrayField(TEXT("blueprint"), MakeStringArray(GetBlueprintCommandTypes()));
        CommandsObject->SetArrayField(TEXT("blueprint_nodes"), MakeStringArray(GetBlueprintNodeCommandTypes()));
        CommandsObject->SetArrayField(TEXT("material"), MakeStringArray(GetMaterialCommandTypes()));
        CommandsObject->SetArrayField(TEXT("project"), MakeStringArray(GetProjectCommandTypes()));
        CommandsObject->SetArrayField(TEXT("umg"), MakeStringArray(GetUMGCommandTypes()));
        return CommandsObject;
    }

    TSharedPtr<FJsonObject> BuildBridgeStatusObject()
    {
        TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
        ResultObject->SetBoolField(TEXT("success"), true);
        ResultObject->SetStringField(TEXT("protocol_version"), UnrealMCPProtocolVersion);

        TSharedPtr<FJsonObject> PluginObject = MakeShared<FJsonObject>();
        PluginObject->SetStringField(TEXT("name"), TEXT("UnrealMCP"));

        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealMCP"));
        if (Plugin.IsValid())
        {
            PluginObject->SetNumberField(TEXT("version"), Plugin->GetDescriptor().Version);
            PluginObject->SetStringField(TEXT("version_name"), Plugin->GetDescriptor().VersionName);
        }
        else
        {
            PluginObject->SetNumberField(TEXT("version"), 0);
            PluginObject->SetStringField(TEXT("version_name"), TEXT(""));
        }
        ResultObject->SetObjectField(TEXT("plugin"), PluginObject);

        TSharedPtr<FJsonObject> EditorObject = MakeShared<FJsonObject>();
        const bool bConnected = GEditor != nullptr;
        const bool bHasActiveViewport = bConnected && GEditor->GetActiveViewport() != nullptr;
        EditorObject->SetBoolField(TEXT("connected"), bConnected);
        EditorObject->SetBoolField(TEXT("has_active_viewport"), bHasActiveViewport);
        ResultObject->SetObjectField(TEXT("editor"), EditorObject);

        TSharedPtr<FJsonObject> ListenObject = MakeShared<FJsonObject>();
        ListenObject->SetStringField(TEXT("host"), MCP_SERVER_HOST);
        ListenObject->SetNumberField(TEXT("port"), MCP_SERVER_PORT);
        if (GEditor)
        {
            if (UUnrealMCPBridge* Bridge = GEditor->GetEditorSubsystem<UUnrealMCPBridge>())
            {
                ListenObject->SetStringField(TEXT("host"), Bridge->GetListenHost());
                ListenObject->SetNumberField(TEXT("port"), Bridge->GetPort());
            }
        }
        ResultObject->SetObjectField(TEXT("listen"), ListenObject);

        ResultObject->SetObjectField(TEXT("commands"), BuildCommandGroupsObject());
        return ResultObject;
    }
}

UUnrealMCPBridge::UUnrealMCPBridge()
{
    EditorCommands = MakeShared<FUnrealMCPEditorCommands>();
    BlueprintCommands = MakeShared<FUnrealMCPBlueprintCommands>();
    BlueprintNodeCommands = MakeShared<FUnrealMCPBlueprintNodeCommands>();
    MaterialCommands = MakeShared<FUnrealMCPMaterialCommands>();
    ProjectCommands = MakeShared<FUnrealMCPProjectCommands>();
    UMGCommands = MakeShared<FUnrealMCPUMGCommands>();
}

UUnrealMCPBridge::~UUnrealMCPBridge()
{
    EditorCommands.Reset();
    BlueprintCommands.Reset();
    BlueprintNodeCommands.Reset();
    MaterialCommands.Reset();
    ProjectCommands.Reset();
    UMGCommands.Reset();
}

// Initialize subsystem
void UUnrealMCPBridge::Initialize(FSubsystemCollectionBase& Collection)
{
    UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Initializing"));
    
    bIsRunning = false;
    ListenerSocket = nullptr;
    ConnectionSocket = nullptr;
    ServerThread = nullptr;
    Port = MCP_SERVER_PORT;
    FIPv4Address::Parse(MCP_SERVER_HOST, ServerAddress);

    // Optional multi-instance override: UNREAL_MCP_PORT / UNREAL_MCP_HOST
    {
        const FString PortEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_MCP_PORT"));
        if (!PortEnv.IsEmpty())
        {
            const int32 ParsedPort = FCString::Atoi(*PortEnv);
            if (ParsedPort > 0 && ParsedPort < 65536)
            {
                Port = static_cast<uint16>(ParsedPort);
                UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Using UNREAL_MCP_PORT=%d"), Port);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("UnrealMCPBridge: Ignoring invalid UNREAL_MCP_PORT=%s"), *PortEnv);
            }
        }

        const FString HostEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_MCP_HOST"));
        if (!HostEnv.IsEmpty())
        {
            FIPv4Address ParsedHost;
            if (FIPv4Address::Parse(HostEnv, ParsedHost))
            {
                ServerAddress = ParsedHost;
                UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Using UNREAL_MCP_HOST=%s"), *HostEnv);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("UnrealMCPBridge: Ignoring invalid UNREAL_MCP_HOST=%s"), *HostEnv);
            }
        }
    }

    // Start the server automatically
    StartServer();
}

// Clean up resources when subsystem is destroyed
void UUnrealMCPBridge::Deinitialize()
{
    UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Shutting down"));
    StopServer();
}

// Start the MCP server
void UUnrealMCPBridge::StartServer()
{
    if (bIsRunning)
    {
        UE_LOG(LogTemp, Warning, TEXT("UnrealMCPBridge: Server is already running"));
        return;
    }

    // Create socket subsystem
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealMCPBridge: Failed to get socket subsystem"));
        return;
    }

    // Create listener socket
    TSharedPtr<FSocket> NewListenerSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UnrealMCPListener"), false));
    if (!NewListenerSocket.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealMCPBridge: Failed to create listener socket"));
        return;
    }

    // Allow address reuse for quick restarts
    NewListenerSocket->SetReuseAddr(true);
    NewListenerSocket->SetNonBlocking(true);

    // Bind to address
    FIPv4Endpoint Endpoint(ServerAddress, Port);
    if (!NewListenerSocket->Bind(*Endpoint.ToInternetAddr()))
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealMCPBridge: Failed to bind listener socket to %s:%d"), *ServerAddress.ToString(), Port);
        return;
    }

    // Start listening
    if (!NewListenerSocket->Listen(5))
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealMCPBridge: Failed to start listening"));
        return;
    }

    ListenerSocket = NewListenerSocket;
    bIsRunning = true;
    UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Server started on %s:%d"), *ServerAddress.ToString(), Port);

    // Start server thread
    ServerThread = FRunnableThread::Create(
        new FMCPServerRunnable(this, ListenerSocket),
        TEXT("UnrealMCPServerThread"),
        0, TPri_Normal
    );

    if (!ServerThread)
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealMCPBridge: Failed to create server thread"));
        StopServer();
        return;
    }
}

// Stop the MCP server
void UUnrealMCPBridge::StopServer()
{
    if (!bIsRunning)
    {
        return;
    }

    bIsRunning = false;

    // Clean up thread
    if (ServerThread)
    {
        ServerThread->Kill(true);
        delete ServerThread;
        ServerThread = nullptr;
    }

    // Close sockets
    if (ConnectionSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ConnectionSocket.Get());
        ConnectionSocket.Reset();
    }

    if (ListenerSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket.Get());
        ListenerSocket.Reset();
    }

    UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Server stopped"));
}

// Execute a command received from a client
FString UUnrealMCPBridge::ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Executing command: %s"), *CommandType);
    
    // Create a promise to wait for the result
    TPromise<FString> Promise;
    TFuture<FString> Future = Promise.GetFuture();
    
    // Queue execution on Game Thread
    AsyncTask(ENamedThreads::GameThread, [this, CommandType, Params, Promise = MoveTemp(Promise)]() mutable
    {
        TSharedPtr<FJsonObject> ResponseJson = MakeShareable(new FJsonObject);
        
        try
        {
            TSharedPtr<FJsonObject> ResultJson;
            
            if (CommandType == TEXT("ping"))
            {
                ResultJson = MakeShareable(new FJsonObject);
                ResultJson->SetStringField(TEXT("message"), TEXT("pong"));
                ResultJson->SetStringField(TEXT("protocol_version"), UnrealMCPProtocolVersion);
            }
            else if (CommandType == TEXT("get_bridge_status"))
            {
                ResultJson = BuildBridgeStatusObject();
            }
            // Editor Commands (including actor manipulation)
            else if (CommandTypeInGroup(CommandType, GetEditorCommandTypes()))
            {
                ResultJson = EditorCommands->HandleCommand(CommandType, Params);
            }
            // Blueprint Commands
            else if (CommandTypeInGroup(CommandType, GetBlueprintCommandTypes()))
            {
                ResultJson = BlueprintCommands->HandleCommand(CommandType, Params);
            }
            // Blueprint Node Commands
            else if (CommandTypeInGroup(CommandType, GetBlueprintNodeCommandTypes()))
            {
                ResultJson = BlueprintNodeCommands->HandleCommand(CommandType, Params);
            }
            // Project Commands
            else if (CommandTypeInGroup(CommandType, GetProjectCommandTypes()))
            {
                ResultJson = ProjectCommands->HandleCommand(CommandType, Params);
            }
            // Material Commands
            else if (CommandTypeInGroup(CommandType, GetMaterialCommandTypes()))
            {
                ResultJson = MaterialCommands->HandleCommand(CommandType, Params);
            }
            // UMG Commands
            else if (CommandTypeInGroup(CommandType, GetUMGCommandTypes()))
            {
                ResultJson = UMGCommands->HandleCommand(CommandType, Params);
            }
            else
            {
                ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
                ResponseJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown command: %s"), *CommandType));
                
                FString ResultString;
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
                FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
                Promise.SetValue(ResultString);
                return;
            }
            
            // Check if the result contains an error
            bool bSuccess = true;
            FString ErrorMessage;
            
            if (ResultJson->HasField(TEXT("success")))
            {
                bSuccess = ResultJson->GetBoolField(TEXT("success"));
                if (!bSuccess && ResultJson->HasField(TEXT("error")))
                {
                    ErrorMessage = ResultJson->GetStringField(TEXT("error"));
                }
            }
            
            if (bSuccess)
            {
                // Set success status and include the result
                ResponseJson->SetStringField(TEXT("status"), TEXT("success"));
                ResponseJson->SetObjectField(TEXT("result"), ResultJson);
            }
            else
            {
                // Set error status and include the error message
                ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
                ResponseJson->SetStringField(TEXT("error"), ErrorMessage);
            }
        }
        catch (const std::exception& e)
        {
            ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
            ResponseJson->SetStringField(TEXT("error"), UTF8_TO_TCHAR(e.what()));
        }
        
        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
        Promise.SetValue(ResultString);
    });
    
    return Future.Get();
}
