#include "Commands/UnrealMCPProjectCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "GameFramework/InputSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "EditorAssetLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/TopLevelAssetPath.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"

namespace
{
	FString NormalizeContentPath(const FString& InPath)
	{
		FString Path = InPath.TrimStartAndEnd();
		if (Path.IsEmpty())
		{
			return TEXT("/Game");
		}
		if (!Path.StartsWith(TEXT("/")))
		{
			Path = TEXT("/") + Path;
		}
		// Asset registry package paths usually omit trailing asset name and .uasset
		if (Path.EndsWith(TEXT("/")))
		{
			Path.LeftChopInline(1);
		}
		return Path;
	}

	bool ResolveClassPath(const FString& ClassNameOrPath, FTopLevelAssetPath& OutClassPath, FString& OutError)
	{
		FString Name = ClassNameOrPath.TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			OutError = TEXT("Empty class filter");
			return false;
		}

		// Already a full path like /Script/Engine.StaticMesh
		if (Name.Contains(TEXT("/")) || Name.Contains(TEXT(".")))
		{
			OutClassPath = FTopLevelAssetPath(Name);
			if (!OutClassPath.IsValid())
			{
				OutError = FString::Printf(TEXT("Invalid class path: %s"), *Name);
				return false;
			}
			return true;
		}

		// Friendly short names
		static const TMap<FString, FString> Aliases = {
			{TEXT("Material"), TEXT("/Script/Engine.Material")},
			{TEXT("MaterialInstance"), TEXT("/Script/Engine.MaterialInstance")},
			{TEXT("MaterialInstanceConstant"), TEXT("/Script/Engine.MaterialInstanceConstant")},
			{TEXT("StaticMesh"), TEXT("/Script/Engine.StaticMesh")},
			{TEXT("SkeletalMesh"), TEXT("/Script/Engine.SkeletalMesh")},
			{TEXT("Texture"), TEXT("/Script/Engine.Texture")},
			{TEXT("Texture2D"), TEXT("/Script/Engine.Texture2D")},
			{TEXT("Blueprint"), TEXT("/Script/Engine.Blueprint")},
			{TEXT("WidgetBlueprint"), TEXT("/Script/UMGEditor.WidgetBlueprint")},
			{TEXT("World"), TEXT("/Script/Engine.World")},
			{TEXT("SoundWave"), TEXT("/Script/Engine.SoundWave")},
			{TEXT("NiagaraSystem"), TEXT("/Script/Niagara.NiagaraSystem")},
		};

		if (const FString* Mapped = Aliases.Find(Name))
		{
			OutClassPath = FTopLevelAssetPath(*Mapped);
			return OutClassPath.IsValid();
		}

		// Try Engine module default
		const FString EnginePath = FString::Printf(TEXT("/Script/Engine.%s"), *Name);
		OutClassPath = FTopLevelAssetPath(EnginePath);
		if (OutClassPath.IsValid())
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Unknown class filter '%s'. Use a short alias or full /Script/Module.Class path."), *Name);
		return false;
	}

	TSharedPtr<FJsonObject> AssetDataToJson(const FAssetData& AssetData)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("object_path"), AssetData.GetObjectPathString());
		Obj->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());
		Obj->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
		Obj->SetStringField(TEXT("asset_class"), AssetData.AssetClassPath.ToString());
		Obj->SetStringField(TEXT("package_path"), FPackageName::GetLongPackagePath(AssetData.PackageName.ToString()));
		return Obj;
	}
}

FUnrealMCPProjectCommands::FUnrealMCPProjectCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("create_input_mapping"))
	{
		return HandleCreateInputMapping(Params);
	}
	if (CommandType == TEXT("find_assets"))
	{
		return HandleFindAssets(Params);
	}
	if (CommandType == TEXT("get_asset_info"))
	{
		return HandleGetAssetInfo(Params);
	}
	if (CommandType == TEXT("delete_asset"))
	{
		return HandleDeleteAsset(Params);
	}

	return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown project command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleCreateInputMapping(const TSharedPtr<FJsonObject>& Params)
{
	FString ActionName;
	if (!Params->TryGetStringField(TEXT("action_name"), ActionName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'action_name' parameter"));
	}

	FString Key;
	if (!Params->TryGetStringField(TEXT("key"), Key))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'key' parameter"));
	}

	UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
	if (!InputSettings)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get input settings"));
	}

	FInputActionKeyMapping ActionMapping;
	ActionMapping.ActionName = FName(*ActionName);
	ActionMapping.Key = FKey(*Key);

	if (Params->HasField(TEXT("shift")))
	{
		ActionMapping.bShift = Params->GetBoolField(TEXT("shift"));
	}
	if (Params->HasField(TEXT("ctrl")))
	{
		ActionMapping.bCtrl = Params->GetBoolField(TEXT("ctrl"));
	}
	if (Params->HasField(TEXT("alt")))
	{
		ActionMapping.bAlt = Params->GetBoolField(TEXT("alt"));
	}
	if (Params->HasField(TEXT("cmd")))
	{
		ActionMapping.bCmd = Params->GetBoolField(TEXT("cmd"));
	}

	InputSettings->AddActionMapping(ActionMapping);
	InputSettings->SaveConfig();

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("action_name"), ActionName);
	ResultObj->SetStringField(TEXT("key"), Key);
	return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleFindAssets(const TSharedPtr<FJsonObject>& Params)
{
	FString Path = TEXT("/Game");
	Params->TryGetStringField(TEXT("path"), Path);
	Path = NormalizeContentPath(Path);

	FString Query;
	Params->TryGetStringField(TEXT("query"), Query);

	FString ClassFilter;
	Params->TryGetStringField(TEXT("class_name"), ClassFilter);
	if (ClassFilter.IsEmpty())
	{
		Params->TryGetStringField(TEXT("class"), ClassFilter);
	}

	int32 MaxResults = 100;
	if (Params->HasField(TEXT("max_results")))
	{
		MaxResults = FMath::Clamp(static_cast<int32>(Params->GetNumberField(TEXT("max_results"))), 1, 2000);
	}

	bool bRecursive = true;
	if (Params->HasField(TEXT("recursive")))
	{
		bRecursive = Params->GetBoolField(TEXT("recursive"));
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.SearchAllAssets(true);

	FARFilter Filter;
	Filter.bRecursivePaths = bRecursive;
	Filter.PackagePaths.Add(FName(*Path));

	if (!ClassFilter.IsEmpty())
	{
		FTopLevelAssetPath ClassPath;
		FString ClassError;
		if (!ResolveClassPath(ClassFilter, ClassPath, ClassError))
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(ClassError);
		}
		Filter.ClassPaths.Add(ClassPath);
		Filter.bRecursiveClasses = true;
	}

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	const FString QueryLower = Query.ToLower();
	TArray<TSharedPtr<FJsonValue>> AssetArray;
	int32 Matched = 0;
	int32 TotalConsidered = Assets.Num();

	for (const FAssetData& AssetData : Assets)
	{
		if (!QueryLower.IsEmpty())
		{
			const FString NameLower = AssetData.AssetName.ToString().ToLower();
			const FString PathLower = AssetData.GetObjectPathString().ToLower();
			if (!NameLower.Contains(QueryLower) && !PathLower.Contains(QueryLower))
			{
				continue;
			}
		}

		AssetArray.Add(MakeShared<FJsonValueObject>(AssetDataToJson(AssetData)));
		Matched++;
		if (Matched >= MaxResults)
		{
			break;
		}
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("path"), Path);
	ResultObj->SetStringField(TEXT("query"), Query);
	ResultObj->SetStringField(TEXT("class_name"), ClassFilter);
	ResultObj->SetNumberField(TEXT("total_considered"), TotalConsidered);
	ResultObj->SetNumberField(TEXT("count"), AssetArray.Num());
	ResultObj->SetBoolField(TEXT("truncated"), Matched >= MaxResults && Assets.Num() > Matched);
	ResultObj->SetArrayField(TEXT("assets"), AssetArray);
	return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleGetAssetInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		if (!Params->TryGetStringField(TEXT("path"), AssetPath))
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
		}
	}

	AssetPath = AssetPath.TrimStartAndEnd();
	if (AssetPath.IsEmpty())
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Empty asset_path"));
	}

	// Accept /Game/Foo.Foo or /Game/Foo
	FString ObjectPath = AssetPath;
	if (!ObjectPath.Contains(TEXT(".")))
	{
		const FString AssetName = FPackageName::GetShortName(ObjectPath);
		ObjectPath = ObjectPath + TEXT(".") + AssetName;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
	if (!AssetData.IsValid())
	{
		// Fallback: try as package path
		const FAssetData ByPackage = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));
		if (!ByPackage.IsValid())
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		}
		TSharedPtr<FJsonObject> ResultObj = AssetDataToJson(ByPackage);
		ResultObj->SetBoolField(TEXT("success"), true);
		ResultObj->SetBoolField(TEXT("exists"), UEditorAssetLibrary::DoesAssetExist(ByPackage.PackageName.ToString()));
		return ResultObj;
	}

	TSharedPtr<FJsonObject> ResultObj = AssetDataToJson(AssetData);
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetBoolField(TEXT("exists"), UEditorAssetLibrary::DoesAssetExist(AssetData.PackageName.ToString()));
	return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPProjectCommands::HandleDeleteAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	AssetPath = AssetPath.TrimStartAndEnd();
	// EditorAssetLibrary expects package path without the .AssetName suffix ideally
	FString PackagePath = AssetPath;
	if (PackagePath.Contains(TEXT(".")))
	{
		PackagePath = FPackageName::ObjectPathToPackageName(PackagePath);
	}

	if (!UEditorAssetLibrary::DoesAssetExist(PackagePath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset does not exist: %s"), *PackagePath));
	}

	const bool bDeleted = UEditorAssetLibrary::DeleteAsset(PackagePath);
	if (!bDeleted)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to delete asset: %s"), *PackagePath));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetStringField(TEXT("deleted_path"), PackagePath);
	return ResultObj;
}
