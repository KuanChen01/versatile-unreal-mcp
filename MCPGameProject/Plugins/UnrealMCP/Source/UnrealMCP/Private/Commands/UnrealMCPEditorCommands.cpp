#include "Commands/UnrealMCPEditorCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "UnrealMCPModule.h"
#include "Algo/Reverse.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "FileHelpers.h"
#include "LevelEditorViewport.h"
#include "LevelEditor.h"
#include "ImageUtils.h"
#include "HighResScreenshot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "PlayInEditorDataTypes.h"
#include "IMessageLogListing.h"
#include "MessageLogModule.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    FViewport* GetActiveEditorViewport(FString& OutErrorMessage)
    {
        if (!GEditor)
        {
            OutErrorMessage = TEXT("Unreal Editor is not available");
            return nullptr;
        }

        FViewport* ActiveViewport = GEditor->GetActiveViewport();
        if (!ActiveViewport)
        {
            OutErrorMessage = TEXT("No active editor viewport is available");
            return nullptr;
        }

        return ActiveViewport;
    }

    FLevelEditorViewportClient* GetFocusableViewportClient(FString& OutErrorMessage)
    {
        if (GCurrentLevelEditingViewportClient)
        {
            return GCurrentLevelEditingViewportClient;
        }

        FViewport* ActiveViewport = GetActiveEditorViewport(OutErrorMessage);
        if (!ActiveViewport)
        {
            return nullptr;
        }

        FViewportClient* ViewportClient = ActiveViewport->GetClient();
        if (!ViewportClient)
        {
            OutErrorMessage = TEXT("Active viewport does not have a client");
            return nullptr;
        }

        FEditorViewportClient* EditorViewportClient = static_cast<FEditorViewportClient*>(ViewportClient);
        if (!EditorViewportClient || !EditorViewportClient->IsLevelEditorClient())
        {
            OutErrorMessage = TEXT("Active viewport is not a level editor viewport");
            return nullptr;
        }

        return static_cast<FLevelEditorViewportClient*>(EditorViewportClient);
    }

    bool ValidateArrayFieldLength(
        const TSharedPtr<FJsonObject>& Params,
        const FString& FieldName,
        int32 ExpectedLength,
        FString& OutErrorMessage)
    {
        const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
        if (!Params->TryGetArrayField(FieldName, JsonArray) || !JsonArray)
        {
            OutErrorMessage = FString::Printf(TEXT("Missing '%s' parameter"), *FieldName);
            return false;
        }

        if (JsonArray->Num() != ExpectedLength)
        {
            OutErrorMessage = FString::Printf(
                TEXT("'%s' must contain exactly %d numeric values"),
                *FieldName,
                ExpectedLength);
            return false;
        }

        return true;
    }

    TArray<TSharedPtr<FJsonValue>> MakeVectorArray(const FVector& Vector)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Reserve(3);
        Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
        Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
        Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
        return Values;
    }

    TArray<TSharedPtr<FJsonValue>> MakeIntPointArray(const FIntPoint& Point)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Reserve(2);
        Values.Add(MakeShared<FJsonValueNumber>(Point.X));
        Values.Add(MakeShared<FJsonValueNumber>(Point.Y));
        return Values;
    }

    FString LogVerbosityToString(ELogVerbosity::Type Verbosity)
    {
        switch (Verbosity & ELogVerbosity::VerbosityMask)
        {
        case ELogVerbosity::Fatal:
            return TEXT("Fatal");
        case ELogVerbosity::Error:
            return TEXT("Error");
        case ELogVerbosity::Warning:
            return TEXT("Warning");
        case ELogVerbosity::Display:
            return TEXT("Display");
        case ELogVerbosity::Log:
            return TEXT("Log");
        case ELogVerbosity::Verbose:
            return TEXT("Verbose");
        case ELogVerbosity::VeryVerbose:
            return TEXT("VeryVerbose");
        default:
            return TEXT("Unknown");
        }
    }

    FString MessageSeverityToString(EMessageSeverity::Type Severity)
    {
        switch (Severity)
        {
        case EMessageSeverity::Error:
            return TEXT("Error");
        case EMessageSeverity::PerformanceWarning:
            return TEXT("PerformanceWarning");
        case EMessageSeverity::Warning:
            return TEXT("Warning");
        case EMessageSeverity::Info:
            return TEXT("Info");
        default:
            return TEXT("Unknown");
        }
    }

    FString PlaySessionWorldTypeToString(EPlaySessionWorldType WorldType)
    {
        switch (WorldType)
        {
        case EPlaySessionWorldType::PlayInEditor:
            return TEXT("play_in_editor");
        case EPlaySessionWorldType::SimulateInEditor:
            return TEXT("simulate_in_editor");
        default:
            return TEXT("inactive");
        }
    }

    UWorld* GetEditorWorld(FString& OutErrorMessage)
    {
        if (!GEditor)
        {
            OutErrorMessage = TEXT("Unreal Editor is not available");
            return nullptr;
        }

        UWorld* World = GEditor->GetEditorWorldContext().World();
        if (!World)
        {
            OutErrorMessage = TEXT("Failed to get the editor world");
            return nullptr;
        }

        return World;
    }

    bool MatchesFilter(const FString& Value, const FString& Filter)
    {
        return Filter.IsEmpty() || Value.Contains(Filter, ESearchCase::IgnoreCase);
    }

    bool MatchesExactFilter(const FString& Value, const FString& Filter)
    {
        return Filter.IsEmpty() || Value.Equals(Filter, ESearchCase::IgnoreCase);
    }

    int32 GetClampedMaxEntries(const TSharedPtr<FJsonObject>& Params, const FString& FieldName, int32 DefaultValue, int32 MinValue, int32 MaxValue)
    {
        int32 Value = DefaultValue;
        if (Params->HasField(FieldName))
        {
            Value = Params->GetIntegerField(FieldName);
        }

        return FMath::Clamp(Value, MinValue, MaxValue);
    }

    bool ResolveLevelFilename(const FString& InputPath, FString& OutFilename, FString& OutPackageName, FString& OutErrorMessage)
    {
        FString LevelPath = InputPath;
        LevelPath.TrimStartAndEndInline();
        if (LevelPath.IsEmpty())
        {
            OutErrorMessage = TEXT("Level path is empty");
            return false;
        }

        if (FPaths::FileExists(LevelPath))
        {
            OutFilename = FPaths::ConvertRelativePathToFull(LevelPath);
            OutPackageName = FPackageName::FilenameToLongPackageName(OutFilename);
            return true;
        }

        if (!LevelPath.StartsWith(TEXT("/")))
        {
            LevelPath = TEXT("/Game/") + LevelPath.TrimStartAndEnd();
        }
        else if (LevelPath.StartsWith(TEXT("/Game/")) == false)
        {
            OutErrorMessage = TEXT("Level path must be an absolute .umap file path or a /Game/... package path");
            return false;
        }

        const int32 DotIndex = LevelPath.Find(TEXT("."), ESearchCase::CaseSensitive);
        if (DotIndex != INDEX_NONE)
        {
            LevelPath = LevelPath.Left(DotIndex);
        }

        FString Filename;
        if (!FPackageName::TryConvertLongPackageNameToFilename(LevelPath, Filename, FPackageName::GetMapPackageExtension()))
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to convert level package path '%s' to a map filename"), *LevelPath);
            return false;
        }

        Filename = FPaths::ConvertRelativePathToFull(Filename);
        if (!FPaths::FileExists(Filename))
        {
            OutErrorMessage = FString::Printf(TEXT("Level file does not exist: %s"), *Filename);
            return false;
        }

        OutFilename = Filename;
        OutPackageName = LevelPath;
        return true;
    }

    TSharedPtr<FJsonObject> BuildLevelStatusObject(UWorld* World)
    {
        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetBoolField(TEXT("success"), true);

        UPackage* WorldPackage = World ? World->GetOutermost() : nullptr;
        const FString PackageName = WorldPackage ? WorldPackage->GetName() : FString();
        FString FilePath;
        if (!PackageName.IsEmpty())
        {
            FPackageName::TryConvertLongPackageNameToFilename(PackageName, FilePath, FPackageName::GetMapPackageExtension());
            if (!FilePath.IsEmpty())
            {
                FilePath = FPaths::ConvertRelativePathToFull(FilePath);
            }
        }

        TArray<AActor*> AllActors;
        if (World)
        {
            UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
        }

        ResultObj->SetStringField(TEXT("world_name"), World ? World->GetName() : FString());
        ResultObj->SetStringField(TEXT("level_name"), PackageName.IsEmpty() ? FString() : FPackageName::GetLongPackageAssetName(PackageName));
        ResultObj->SetStringField(TEXT("package_name"), PackageName);
        ResultObj->SetStringField(TEXT("file_path"), FilePath);
        ResultObj->SetBoolField(TEXT("is_dirty"), WorldPackage ? WorldPackage->IsDirty() : false);
        ResultObj->SetNumberField(TEXT("actor_count"), AllActors.Num());
        ResultObj->SetBoolField(TEXT("has_play_world"), GEditor && GEditor->PlayWorld != nullptr);

        return ResultObj;
    }

    TSharedPtr<FJsonObject> MakeOutputLogEntryObject(const FUnrealMCPLogEntry& Entry)
    {
        TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
        EntryObject->SetStringField(TEXT("timestamp"), Entry.Timestamp);
        EntryObject->SetStringField(TEXT("category"), Entry.Category);
        EntryObject->SetStringField(TEXT("verbosity"), Entry.Verbosity);
        EntryObject->SetStringField(TEXT("message"), Entry.Message);
        return EntryObject;
    }

    TSharedPtr<FJsonObject> MakeMessageLogEntryObject(const TSharedRef<FTokenizedMessage>& Message)
    {
        TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
        EntryObject->SetStringField(TEXT("severity"), MessageSeverityToString(Message->GetSeverity()));
        EntryObject->SetStringField(TEXT("message"), Message->ToText().ToString());
        return EntryObject;
    }
}

FUnrealMCPEditorCommands::FUnrealMCPEditorCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("get_level_status"))
    {
        return HandleGetLevelStatus(Params);
    }
    else if (CommandType == TEXT("open_level"))
    {
        return HandleOpenLevel(Params);
    }
    else if (CommandType == TEXT("save_current_level"))
    {
        return HandleSaveCurrentLevel(Params);
    }
    else if (CommandType == TEXT("save_dirty_packages"))
    {
        return HandleSaveDirtyPackages(Params);
    }
    else if (CommandType == TEXT("get_play_state"))
    {
        return HandleGetPlayState(Params);
    }
    else if (CommandType == TEXT("start_pie"))
    {
        return HandleStartPIE(Params);
    }
    else if (CommandType == TEXT("stop_pie"))
    {
        return HandleStopPIE(Params);
    }
    else if (CommandType == TEXT("get_output_log"))
    {
        return HandleGetOutputLog(Params);
    }
    else if (CommandType == TEXT("get_message_log"))
    {
        return HandleGetMessageLog(Params);
    }
    // Actor manipulation commands
    else if (CommandType == TEXT("get_actors_in_level"))
    {
        return HandleGetActorsInLevel(Params);
    }
    else if (CommandType == TEXT("find_actors_by_name"))
    {
        return HandleFindActorsByName(Params);
    }
    else if (CommandType == TEXT("spawn_actor") || CommandType == TEXT("create_actor"))
    {
        if (CommandType == TEXT("create_actor"))
        {
            UE_LOG(LogTemp, Warning, TEXT("'create_actor' command is deprecated and will be removed in a future version. Please use 'spawn_actor' instead."));
        }
        return HandleSpawnActor(Params);
    }
    else if (CommandType == TEXT("spawn_actor_by_class"))
    {
        return HandleSpawnActorByClass(Params);
    }
    else if (CommandType == TEXT("delete_actor"))
    {
        return HandleDeleteActor(Params);
    }
    else if (CommandType == TEXT("set_actor_transform"))
    {
        return HandleSetActorTransform(Params);
    }
    else if (CommandType == TEXT("get_actor_properties"))
    {
        return HandleGetActorProperties(Params);
    }
    else if (CommandType == TEXT("set_actor_property"))
    {
        return HandleSetActorProperty(Params);
    }
    else if (CommandType == TEXT("assign_material_to_actor"))
    {
        return HandleAssignMaterialToActor(Params);
    }
    else if (CommandType == TEXT("get_viewport_status"))
    {
        return HandleGetViewportStatus(Params);
    }
    // Blueprint actor spawning
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    // Editor viewport commands
    else if (CommandType == TEXT("focus_viewport"))
    {
        return HandleFocusViewport(Params);
    }
    else if (CommandType == TEXT("take_screenshot"))
    {
        return HandleTakeScreenshot(Params);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown editor command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetViewportStatus(const TSharedPtr<FJsonObject>& Params)
{
    FString ViewportErrorMessage;
    FViewport* ActiveViewport = GetActiveEditorViewport(ViewportErrorMessage);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);

    if (!ActiveViewport)
    {
        ResultObj->SetBoolField(TEXT("has_active_viewport"), false);
        ResultObj->SetField(TEXT("size"), MakeShared<FJsonValueNull>());
        ResultObj->SetBoolField(TEXT("can_focus"), false);
        ResultObj->SetBoolField(TEXT("can_screenshot"), false);
        ResultObj->SetStringField(TEXT("message"), ViewportErrorMessage);
        return ResultObj;
    }

    const FIntPoint ViewportSize = ActiveViewport->GetSizeXY();
    FString FocusErrorMessage;
    const bool bCanFocus = GetFocusableViewportClient(FocusErrorMessage) != nullptr;
    const bool bCanScreenshot = ViewportSize.X > 0 && ViewportSize.Y > 0;

    ResultObj->SetBoolField(TEXT("has_active_viewport"), true);
    ResultObj->SetArrayField(TEXT("size"), MakeIntPointArray(ViewportSize));
    ResultObj->SetBoolField(TEXT("can_focus"), bCanFocus);
    ResultObj->SetBoolField(TEXT("can_screenshot"), bCanScreenshot);
    if (!bCanFocus && !FocusErrorMessage.IsEmpty())
    {
        ResultObj->SetStringField(TEXT("message"), FocusErrorMessage);
    }

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetLevelStatus(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMessage;
    UWorld* EditorWorld = GetEditorWorld(ErrorMessage);
    if (!EditorWorld)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
    }

    return BuildLevelStatusObject(EditorWorld);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleOpenLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString LevelPath;
    if (!Params->TryGetStringField(TEXT("level_path"), LevelPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'level_path' parameter"));
    }

    if (GEditor && GEditor->IsPlaySessionInProgress())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot open a level while a play session is active"));
    }

    bool bSaveDirtyPackages = false;
    Params->TryGetBoolField(TEXT("save_dirty_packages"), bSaveDirtyPackages);

    FString EditorWorldError;
    UWorld* EditorWorld = GetEditorWorld(EditorWorldError);
    if (!EditorWorld)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(EditorWorldError);
    }

    if (UPackage* CurrentWorldPackage = EditorWorld->GetOutermost())
    {
        if (CurrentWorldPackage->IsDirty())
        {
            if (!bSaveDirtyPackages)
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    TEXT("The current level has unsaved changes. Save it first or pass save_dirty_packages=true."));
            }

            bool bPackagesNeededSaving = false;
            const bool bSaved = FEditorFileUtils::SaveDirtyPackages(
                false,
                true,
                true,
                true,
                false,
                false,
                &bPackagesNeededSaving);

            if (!bSaved)
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to save dirty packages before opening the requested level"));
            }
        }
    }

    FString LevelFilename;
    FString LevelPackageName;
    FString ResolveError;
    if (!ResolveLevelFilename(LevelPath, LevelFilename, LevelPackageName, ResolveError))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ResolveError);
    }

    UWorld* LoadedWorld = UEditorLoadingAndSavingUtils::LoadMap(LevelFilename);
    if (!LoadedWorld)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to load level: %s"), *LevelFilename));
    }

    return BuildLevelStatusObject(LoadedWorld);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSaveCurrentLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString ErrorMessage;
    UWorld* EditorWorld = GetEditorWorld(ErrorMessage);
    if (!EditorWorld)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
    }

    UPackage* WorldPackage = EditorWorld->GetOutermost();
    const bool bWasDirty = WorldPackage && WorldPackage->IsDirty();
    if (!UEditorLoadingAndSavingUtils::SaveCurrentLevel())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to save the current level"));
    }

    TSharedPtr<FJsonObject> ResultObj = BuildLevelStatusObject(EditorWorld);
    ResultObj->SetBoolField(TEXT("saved"), true);
    ResultObj->SetBoolField(TEXT("was_dirty"), bWasDirty);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSaveDirtyPackages(const TSharedPtr<FJsonObject>& Params)
{
    bool bSaveMapPackages = true;
    bool bSaveContentPackages = true;
    Params->TryGetBoolField(TEXT("save_map_packages"), bSaveMapPackages);
    Params->TryGetBoolField(TEXT("save_content_packages"), bSaveContentPackages);

    bool bPackagesNeededSaving = false;
    const bool bSaved = FEditorFileUtils::SaveDirtyPackages(
        false,
        bSaveMapPackages,
        bSaveContentPackages,
        true,
        false,
        false,
        &bPackagesNeededSaving);

    if (!bSaved)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to save dirty packages"));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("saved"), true);
    ResultObj->SetBoolField(TEXT("packages_needed_saving"), bPackagesNeededSaving);
    ResultObj->SetBoolField(TEXT("save_map_packages"), bSaveMapPackages);
    ResultObj->SetBoolField(TEXT("save_content_packages"), bSaveContentPackages);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetPlayState(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Unreal Editor is not available"));
    }

    const TOptional<FPlayInEditorSessionInfo> SessionInfo = GEditor->GetPlayInEditorSessionInfo();
    const TOptional<FRequestPlaySessionParams> SessionRequest = GEditor->GetPlaySessionRequest();

    FString SessionType = TEXT("inactive");
    if (SessionInfo.IsSet())
    {
        SessionType = PlaySessionWorldTypeToString(SessionInfo->OriginalRequestParams.WorldType);
    }
    else if (SessionRequest.IsSet())
    {
        SessionType = PlaySessionWorldTypeToString(SessionRequest->WorldType);
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetBoolField(TEXT("is_play_session_in_progress"), GEditor->IsPlaySessionInProgress());
    ResultObj->SetBoolField(TEXT("is_playing_session_in_editor"), GEditor->IsPlayingSessionInEditor());
    ResultObj->SetBoolField(TEXT("is_play_session_request_queued"), GEditor->IsPlaySessionRequestQueued());
    ResultObj->SetBoolField(TEXT("is_simulating_in_editor"), GEditor->IsSimulatingInEditor());
    ResultObj->SetStringField(TEXT("session_type"), SessionType);
    ResultObj->SetBoolField(TEXT("has_play_world"), GEditor->PlayWorld != nullptr);
    ResultObj->SetStringField(TEXT("play_world_name"), GEditor->PlayWorld ? GEditor->PlayWorld->GetName() : FString());
    ResultObj->SetNumberField(TEXT("pie_instance_count"), SessionInfo.IsSet() ? SessionInfo->PIEInstanceCount : 0);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleStartPIE(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Unreal Editor is not available"));
    }

    if (GEditor->IsPlaySessionInProgress())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("A play session is already active or queued"));
    }

    bool bSimulate = false;
    Params->TryGetBoolField(TEXT("simulate"), bSimulate);

    TOptional<FVector> StartLocation;
    if (Params->HasField(TEXT("location")))
    {
        FString ValidationError;
        if (!ValidateArrayFieldLength(Params, TEXT("location"), 3, ValidationError))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ValidationError);
        }
        StartLocation = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }

    TOptional<FRotator> StartRotation;
    if (Params->HasField(TEXT("rotation")))
    {
        FString ValidationError;
        if (!ValidateArrayFieldLength(Params, TEXT("rotation"), 3, ValidationError))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ValidationError);
        }
        StartRotation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }

    FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
    TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveViewport();
    if (!ActiveLevelViewport.IsValid())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No active level editor viewport is available for PIE"));
    }

    FRequestPlaySessionParams SessionParams;
    SessionParams.WorldType = bSimulate ? EPlaySessionWorldType::SimulateInEditor : EPlaySessionWorldType::PlayInEditor;
    SessionParams.DestinationSlateViewport = ActiveLevelViewport;
    if (StartLocation.IsSet())
    {
        SessionParams.StartLocation = StartLocation;
    }
    if (StartRotation.IsSet())
    {
        SessionParams.StartRotation = StartRotation;
    }

    GEditor->RequestPlaySession(SessionParams);

    TSharedPtr<FJsonObject> ResultObj = HandleGetPlayState(Params);
    ResultObj->SetBoolField(TEXT("requested"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleStopPIE(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Unreal Editor is not available"));
    }

    if (!GEditor->IsPlaySessionInProgress())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No play session is currently active or queued"));
    }

    if (GEditor->IsPlaySessionRequestQueued() && !GEditor->IsPlayingSessionInEditor())
    {
        GEditor->CancelRequestPlaySession();
    }
    else
    {
        GEditor->RequestEndPlayMap();
    }

    TSharedPtr<FJsonObject> ResultObj = HandleGetPlayState(Params);
    ResultObj->SetBoolField(TEXT("stop_requested"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetOutputLog(const TSharedPtr<FJsonObject>& Params)
{
    if (!FUnrealMCPModule::IsAvailable())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("UnrealMCP module is not available"));
    }

    const int32 MaxEntries = GetClampedMaxEntries(Params, TEXT("max_entries"), 200, 1, 1000);

    FString ContainsFilter;
    FString CategoryFilter;
    FString VerbosityFilter;
    Params->TryGetStringField(TEXT("contains"), ContainsFilter);
    Params->TryGetStringField(TEXT("category"), CategoryFilter);
    Params->TryGetStringField(TEXT("verbosity"), VerbosityFilter);

    const TArray<FUnrealMCPLogEntry> BufferedEntries = FUnrealMCPModule::Get().GetBufferedLogEntries();
    TArray<TSharedPtr<FJsonValue>> EntryArray;
    EntryArray.Reserve(MaxEntries);

    for (int32 Index = BufferedEntries.Num() - 1; Index >= 0 && EntryArray.Num() < MaxEntries; --Index)
    {
        const FUnrealMCPLogEntry& Entry = BufferedEntries[Index];
        if (!MatchesFilter(Entry.Message, ContainsFilter))
        {
            continue;
        }
        if (!MatchesExactFilter(Entry.Category, CategoryFilter))
        {
            continue;
        }
        if (!MatchesExactFilter(Entry.Verbosity, VerbosityFilter))
        {
            continue;
        }

        EntryArray.Add(MakeShared<FJsonValueObject>(MakeOutputLogEntryObject(Entry)));
    }

    Algo::Reverse(EntryArray);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetNumberField(TEXT("total_buffered"), BufferedEntries.Num());
    ResultObj->SetNumberField(TEXT("returned_entries"), EntryArray.Num());
    ResultObj->SetArrayField(TEXT("entries"), EntryArray);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetMessageLog(const TSharedPtr<FJsonObject>& Params)
{
    const int32 MaxEntries = GetClampedMaxEntries(Params, TEXT("max_entries"), 100, 1, 500);

    FString LogName = TEXT("PIE");
    FString ContainsFilter;
    FString SeverityFilter;
    Params->TryGetStringField(TEXT("log_name"), LogName);
    Params->TryGetStringField(TEXT("contains"), ContainsFilter);
    Params->TryGetStringField(TEXT("severity"), SeverityFilter);

    FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
    const bool bWasRegistered = MessageLogModule.IsRegisteredLogListing(*LogName);
    TSharedRef<IMessageLogListing> LogListing = MessageLogModule.GetLogListing(*LogName);
    const TArray<TSharedRef<FTokenizedMessage>>& Messages = LogListing->GetFilteredMessages();

    TArray<TSharedPtr<FJsonValue>> EntryArray;
    EntryArray.Reserve(MaxEntries);

    for (int32 Index = Messages.Num() - 1; Index >= 0 && EntryArray.Num() < MaxEntries; --Index)
    {
        const TSharedRef<FTokenizedMessage>& Message = Messages[Index];
        const FString MessageText = Message->ToText().ToString();
        const FString Severity = MessageSeverityToString(Message->GetSeverity());

        if (!MatchesFilter(MessageText, ContainsFilter))
        {
            continue;
        }
        if (!MatchesExactFilter(Severity, SeverityFilter))
        {
            continue;
        }

        EntryArray.Add(MakeShared<FJsonValueObject>(MakeMessageLogEntryObject(Message)));
    }

    Algo::Reverse(EntryArray);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("log_name"), LogName);
    ResultObj->SetBoolField(TEXT("registered"), bWasRegistered);
    ResultObj->SetNumberField(TEXT("total_messages"), Messages.Num());
    ResultObj->SetNumberField(TEXT("returned_entries"), EntryArray.Num());
    ResultObj->SetArrayField(TEXT("entries"), EntryArray);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params)
{
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> ActorArray;
    for (AActor* Actor : AllActors)
    {
        if (Actor)
        {
            ActorArray.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), ActorArray);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params)
{
    FString Pattern;
    if (!Params->TryGetStringField(TEXT("pattern"), Pattern))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pattern' parameter"));
    }
    
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    TArray<TSharedPtr<FJsonValue>> MatchingActors;
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName().Contains(Pattern))
        {
            MatchingActors.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }
    
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), MatchingActors);
    
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorType;
    if (!Params->TryGetStringField(TEXT("type"), ActorType))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    // Get actor name (required parameter)
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Get optional transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);
    FVector Scale(1.0f, 1.0f, 1.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }

    // Create the actor based on type
    AActor* NewActor = nullptr;
    UWorld* World = GEditor->GetEditorWorldContext().World();

    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    // Check if an actor with this name already exists
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor with name '%s' already exists"), *ActorName));
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *ActorName;

    if (ActorType == TEXT("StaticMeshActor"))
    {
        NewActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("PointLight"))
    {
        NewActor = World->SpawnActor<APointLight>(APointLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("SpotLight"))
    {
        NewActor = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("DirectionalLight"))
    {
        NewActor = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("CameraActor"))
    {
        NewActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Location, Rotation, SpawnParams);
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown actor type: %s"), *ActorType));
    }

    if (NewActor)
    {
        // Set scale (since SpawnActor only takes location and rotation)
        FTransform Transform = NewActor->GetTransform();
        Transform.SetScale3D(Scale);
        NewActor->SetActorTransform(Transform);

        // Return the created actor's details
        return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create actor"));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world available"));
    }

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && !Actor->IsActorBeingDestroyed() && Actor->GetName() == ActorName)
        {
            // Store actor info before deletion for the response
            TSharedPtr<FJsonObject> ActorInfo = FUnrealMCPCommonUtils::ActorToJsonObject(Actor);

            // Editor destroy frees the object name so a later spawn can reuse it.
            // Plain Destroy() leaves the name reserved until GC and can crash spawn with Required_Fatal.
            const bool bDestroyed = World->EditorDestroyActor(Actor, true);
            if (!bDestroyed)
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to destroy actor: %s"), *ActorName));
            }
            
            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetObjectField(TEXT("deleted_actor"), ActorInfo);
            ResultObj->SetBoolField(TEXT("success"), true);
            return ResultObj;
        }
    }
    
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Get transform parameters
    FTransform NewTransform = TargetActor->GetTransform();

    if (Params->HasField(TEXT("location")))
    {
        NewTransform.SetLocation(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        NewTransform.SetRotation(FQuat(FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"))));
    }
    if (Params->HasField(TEXT("scale")))
    {
        NewTransform.SetScale3D(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
    }

    // Set the new transform
    TargetActor->SetActorTransform(NewTransform);

    // Return updated actor info
    return FUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetActorProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Always return detailed properties for this command
    return FUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSetActorProperty(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Get property name
    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    // Get property value
    if (!Params->HasField(TEXT("property_value")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
    }
    
    TSharedPtr<FJsonValue> PropertyValue = Params->Values.FindRef(TEXT("property_value"));
    
    // Set the property using our utility function
    FString ErrorMessage;
    if (FUnrealMCPCommonUtils::SetObjectProperty(TargetActor, PropertyName, PropertyValue, ErrorMessage))
    {
        // Property set successfully
        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("actor"), ActorName);
        ResultObj->SetStringField(TEXT("property"), PropertyName);
        ResultObj->SetBoolField(TEXT("success"), true);
        
        // Also include the full actor details
        ResultObj->SetObjectField(TEXT("actor_details"), FUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true));
        return ResultObj;
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
    }
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSpawnActorByClass(const TSharedPtr<FJsonObject>& Params)
{
    FString ClassPath;
    if (!Params->TryGetStringField(TEXT("class_path"), ClassPath))
    {
        if (!Params->TryGetStringField(TEXT("class"), ClassPath))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'class_path' parameter (e.g. /Script/Engine.PointLight or /Game/BP.BP_C)"));
        }
    }

    FString ActorName;
    Params->TryGetStringField(TEXT("name"), ActorName);
    if (ActorName.IsEmpty())
    {
        Params->TryGetStringField(TEXT("actor_name"), ActorName);
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor world available"));
    }

    // Resolve UClass from soft path or short name
    UClass* ActorClass = nullptr;
    FString ResolvedPath = ClassPath.TrimStartAndEnd();

    if (!ResolvedPath.Contains(TEXT("/")) && !ResolvedPath.Contains(TEXT(".")))
    {
        // Short names: PointLight, StaticMeshActor, CameraActor
        static const TMap<FString, FString> ClassAliases = {
            {TEXT("StaticMeshActor"), TEXT("/Script/Engine.StaticMeshActor")},
            {TEXT("PointLight"), TEXT("/Script/Engine.PointLight")},
            {TEXT("SpotLight"), TEXT("/Script/Engine.SpotLight")},
            {TEXT("DirectionalLight"), TEXT("/Script/Engine.DirectionalLight")},
            {TEXT("CameraActor"), TEXT("/Script/Engine.CameraActor")},
            {TEXT("Actor"), TEXT("/Script/Engine.Actor")},
            {TEXT("Pawn"), TEXT("/Script/Engine.Pawn")},
            {TEXT("Character"), TEXT("/Script/Engine.Character")},
        };
        if (const FString* Mapped = ClassAliases.Find(ResolvedPath))
        {
            ResolvedPath = *Mapped;
        }
        else
        {
            ResolvedPath = FString::Printf(TEXT("/Script/Engine.%s"), *ResolvedPath);
        }
    }

    // Blueprint generated class: /Game/Foo.Foo_C or /Game/Foo.Foo
    if (ResolvedPath.StartsWith(TEXT("/Game/")) && !ResolvedPath.EndsWith(TEXT("_C")))
    {
        FString PackagePath = ResolvedPath;
        FString ObjectName;
        if (ResolvedPath.Contains(TEXT(".")))
        {
            const FSoftObjectPath SoftPath(ResolvedPath);
            PackagePath = SoftPath.GetLongPackageName();
            ObjectName = SoftPath.GetAssetName();
        }
        else
        {
            ObjectName = FPackageName::GetShortName(PackagePath);
        }

        // Try Blueprint asset first
        const FString BPObjectPath = FString::Printf(TEXT("%s.%s"), *PackagePath, *ObjectName);
        if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPObjectPath))
        {
            ActorClass = BP->GeneratedClass;
        }
        if (!ActorClass)
        {
            const FString GenClassPath = FString::Printf(TEXT("%s.%s_C"), *PackagePath, *ObjectName);
            ActorClass = LoadObject<UClass>(nullptr, *GenClassPath);
        }
    }

    if (!ActorClass)
    {
        ActorClass = LoadObject<UClass>(nullptr, *ResolvedPath);
    }
    if (!ActorClass)
    {
        ActorClass = FindObject<UClass>(nullptr, *ResolvedPath);
    }
    if (!ActorClass)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Could not resolve actor class from '%s'"), *ClassPath));
    }
    if (!ActorClass->IsChildOf(AActor::StaticClass()))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Class '%s' is not an Actor"), *ActorClass->GetPathName()));
    }

    if (!ActorName.IsEmpty())
    {
        TArray<AActor*> AllActors;
        UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
        for (AActor* Existing : AllActors)
        {
            if (Existing && Existing->GetName() == ActorName)
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Actor with name '%s' already exists"), *ActorName));
            }
        }
    }

    FVector Location(0.f, 0.f, 0.f);
    FRotator Rotation(0.f, 0.f, 0.f);
    FVector Scale(1.f, 1.f, 1.f);
    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    // Never use Required_Fatal for MCP-chosen names — name collisions must return JSON errors, not crash the Editor.
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
    if (!ActorName.IsEmpty())
    {
        SpawnParams.Name = FName(*ActorName);
    }

    AActor* NewActor = World->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
    if (!NewActor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            ActorName.IsEmpty()
                ? TEXT("Failed to spawn actor")
                : FString::Printf(TEXT("Failed to spawn actor (name '%s' may already be in use)"), *ActorName));
    }

    NewActor->SetActorScale3D(Scale);
    if (!ActorName.IsEmpty())
    {
        NewActor->SetActorLabel(*ActorName);
    }

    TSharedPtr<FJsonObject> ResultObj = FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("class_path"), ActorClass->GetPathName());
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleAssignMaterialToActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        if (!Params->TryGetStringField(TEXT("name"), ActorName))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
        }
    }

    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    FString ComponentName;
    Params->TryGetStringField(TEXT("component_name"), ComponentName);

    int32 SlotIndex = 0;
    if (Params->HasField(TEXT("slot_index")))
    {
        SlotIndex = static_cast<int32>(Params->GetNumberField(TEXT("slot_index")));
    }

    FString SlotName;
    Params->TryGetStringField(TEXT("slot_name"), SlotName);

    // Find actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }
    if (!TargetActor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Load material interface
    FString MatObjectPath = MaterialPath.TrimStartAndEnd();
    if (!MatObjectPath.Contains(TEXT(".")))
    {
        const FString Leaf = FPackageName::GetShortName(MatObjectPath);
        MatObjectPath = FString::Printf(TEXT("%s.%s"), *MatObjectPath, *Leaf);
    }
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MatObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    // Resolve mesh component
    UMeshComponent* MeshComp = nullptr;
    TArray<UMeshComponent*> MeshComponents;
    TargetActor->GetComponents<UMeshComponent>(MeshComponents);

    if (!ComponentName.IsEmpty())
    {
        for (UMeshComponent* Comp : MeshComponents)
        {
            if (Comp && (Comp->GetName() == ComponentName || Comp->GetFName().ToString() == ComponentName))
            {
                MeshComp = Comp;
                break;
            }
        }
        if (!MeshComp)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Mesh component '%s' not found on actor '%s'"), *ComponentName, *ActorName));
        }
    }
    else if (MeshComponents.Num() > 0)
    {
        MeshComp = MeshComponents[0];
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Actor '%s' has no mesh components"), *ActorName));
    }

    if (!SlotName.IsEmpty())
    {
        const TArray<FName> SlotNames = MeshComp->GetMaterialSlotNames();
        const int32 Found = SlotNames.IndexOfByKey(FName(*SlotName));
        if (Found == INDEX_NONE)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Material slot '%s' not found on component '%s'"), *SlotName, *MeshComp->GetName()));
        }
        SlotIndex = Found;
    }

    if (SlotIndex < 0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("slot_index must be >= 0"));
    }

    MeshComp->SetMaterial(SlotIndex, Material);
    MeshComp->MarkRenderStateDirty();
    TargetActor->MarkPackageDirty();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetStringField(TEXT("actor_name"), ActorName);
    ResultObj->SetStringField(TEXT("component_name"), MeshComp->GetName());
    ResultObj->SetStringField(TEXT("material_path"), Material->GetPathName());
    ResultObj->SetNumberField(TEXT("slot_index"), SlotIndex);
    if (!SlotName.IsEmpty())
    {
        ResultObj->SetStringField(TEXT("slot_name"), SlotName);
    }
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    // Find the blueprint
    if (BlueprintName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint name is empty"));
    }

    FString AssetPath = BlueprintName;
    if (!AssetPath.StartsWith(TEXT("/Game/")))
    {
        AssetPath = TEXT("/Game/Blueprints/") + BlueprintName;
    }

    FString ObjectPath = AssetPath;
    const int32 LastSlashIndex = AssetPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    const FString AssetLeafName = LastSlashIndex != INDEX_NONE ? AssetPath.Mid(LastSlashIndex + 1) : AssetPath;
    if (!AssetPath.Contains(TEXT(".")))
    {
        ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetLeafName);
    }

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
    if (!Blueprint)
    {
        Blueprint = FindObject<UBlueprint>(nullptr, *ObjectPath);
    }
    if (!Blueprint && !BlueprintName.StartsWith(TEXT("/Game/")))
    {
        Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    }
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint '%s' not found – it must reside under /Game/Blueprints"), *BlueprintName));
    }

    // Get transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);
    FVector Scale(1.0f, 1.0f, 1.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }

    // Spawn the actor
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    // Guard name collisions up front (delete+respawn races used to hit a fatal check in LevelActor.cpp).
    {
        TArray<AActor*> AllActors;
        UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
        for (AActor* Existing : AllActors)
        {
            if (Existing && !Existing->IsActorBeingDestroyed() && Existing->GetName() == ActorName)
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(
                        TEXT("Actor with name '%s' already exists. Delete it first or choose a unique actor_name."),
                        *ActorName));
            }
        }
    }

    if (!Blueprint->GeneratedClass)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint '%s' has no GeneratedClass (compile it first)"), *BlueprintName));
    }

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(Location);
    SpawnTransform.SetRotation(FQuat(Rotation));
    SpawnTransform.SetScale3D(Scale);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    // Critical: default NameMode is Required_Fatal and will crash the Editor on name collision.
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
    SpawnParams.Name = FName(*ActorName);

    AActor* NewActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, SpawnTransform, SpawnParams);
    if (NewActor)
    {
        NewActor->SetActorLabel(*ActorName);
        TSharedPtr<FJsonObject> ResultObj = FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
        ResultObj->SetBoolField(TEXT("success"), true);
        return ResultObj;
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(
        FString::Printf(
            TEXT("Failed to spawn blueprint actor '%s' (name '%s' may already be in use or class failed to spawn)"),
            *BlueprintName,
            *ActorName));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleFocusViewport(const TSharedPtr<FJsonObject>& Params)
{
    // Get target actor name if provided
    FString TargetActorName;
    bool HasTargetActor = Params->TryGetStringField(TEXT("target"), TargetActorName);

    // Get location if provided
    FVector Location(0.0f, 0.0f, 0.0f);
    bool HasLocation = false;
    if (Params->HasField(TEXT("location")))
    {
        FString ValidationError;
        if (!ValidateArrayFieldLength(Params, TEXT("location"), 3, ValidationError))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ValidationError);
        }
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
        HasLocation = true;
    }

    // Get distance
    float Distance = 1000.0f;
    if (Params->HasField(TEXT("distance")))
    {
        Distance = Params->GetNumberField(TEXT("distance"));
    }

    // Get orientation if provided
    FRotator Orientation(0.0f, 0.0f, 0.0f);
    bool HasOrientation = false;
    if (Params->HasField(TEXT("orientation")))
    {
        FString ValidationError;
        if (!ValidateArrayFieldLength(Params, TEXT("orientation"), 3, ValidationError))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ValidationError);
        }
        Orientation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("orientation"));
        HasOrientation = true;
    }

    if (Distance < 0.0f)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'distance' must be non-negative"));
    }

    FString ViewportErrorMessage;
    FLevelEditorViewportClient* ViewportClient = GetFocusableViewportClient(ViewportErrorMessage);
    if (!ViewportClient)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ViewportErrorMessage);
    }

    FVector FocusPoint = FVector::ZeroVector;

    // If we have a target actor, focus on it
    if (HasTargetActor)
    {
        // Find the actor
        AActor* TargetActor = nullptr;
        TArray<AActor*> AllActors;
        UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
        
        for (AActor* Actor : AllActors)
        {
            if (Actor && Actor->GetName() == TargetActorName)
            {
                TargetActor = Actor;
                break;
            }
        }

        if (!TargetActor)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *TargetActorName));
        }

        FocusPoint = TargetActor->GetActorLocation();
    }
    // Otherwise use the provided location
    else if (HasLocation)
    {
        FocusPoint = Location;
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Either 'target' or 'location' must be provided"));
    }

    const FRotator ViewRotation = HasOrientation ? Orientation : ViewportClient->GetViewRotation();
    ViewportClient->SetViewRotation(ViewRotation);
    ViewportClient->SetViewLocation(FocusPoint - (ViewRotation.Vector() * Distance));

    // Force viewport to redraw
    ViewportClient->Invalidate();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    ResultObj->SetArrayField(TEXT("focus_point"), MakeVectorArray(FocusPoint));
    ResultObj->SetNumberField(TEXT("distance"), Distance);
    if (HasTargetActor)
    {
        ResultObj->SetStringField(TEXT("target"), TargetActorName);
    }
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleTakeScreenshot(const TSharedPtr<FJsonObject>& Params)
{
    // Get file path parameter
    FString FilePath;
    if (!Params->TryGetStringField(TEXT("filepath"), FilePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'filepath' parameter"));
    }
    
    // Ensure the file path has a proper extension
    if (!FilePath.EndsWith(TEXT(".png")))
    {
        FilePath += TEXT(".png");
    }

    FString ViewportErrorMessage;
    FViewport* Viewport = GetActiveEditorViewport(ViewportErrorMessage);
    if (!Viewport)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ViewportErrorMessage);
    }

    const FIntPoint ViewportSize = Viewport->GetSizeXY();
    if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Active viewport does not report a valid size"));
    }

    TArray<FColor> Bitmap;
    FIntRect ViewportRect(0, 0, ViewportSize.X, ViewportSize.Y);
    if (!Viewport->ReadPixels(Bitmap, FReadSurfaceDataFlags(), ViewportRect))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to read pixels from the active viewport"));
    }

    TArray<uint8> CompressedBitmap;
    FImageUtils::CompressImageArray(ViewportSize.X, ViewportSize.Y, Bitmap, CompressedBitmap);
    if (!FFileHelper::SaveArrayToFile(CompressedBitmap, *FilePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to write screenshot file"));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("filepath"), FilePath);
    ResultObj->SetArrayField(TEXT("size"), MakeIntPointArray(ViewportSize));
    return ResultObj;
} 
