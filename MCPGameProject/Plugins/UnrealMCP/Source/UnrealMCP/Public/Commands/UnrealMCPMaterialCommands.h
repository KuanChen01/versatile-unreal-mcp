#pragma once

#include "CoreMinimal.h"
#include "Json.h"
#include "SceneTypes.h"

class UMaterial;
class UMaterialExpression;
class UMaterialFunction;
class UMaterialFunctionInterface;

/**
 * Handler class for Material-related MCP commands.
 */
class UNREALMCP_API FUnrealMCPMaterialCommands
{
public:
    FUnrealMCPMaterialCommands();

    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleCreateMaterial(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetMaterialProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddMaterialExpression(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetMaterialExpressionProperty(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectMaterialExpressions(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectMaterialProperty(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRecompileMaterial(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConfigureGlassMaterial(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRebuildMaterialGraph(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetMaterialCompileStatus(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleValidateMaterialGraph(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReloadAssetFromDisk(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCloseAssetEditor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleIsAssetLoadedDirty(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateMaterialFunction(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRebuildMaterialFunctionGraph(const TSharedPtr<FJsonObject>& Params);

    UMaterial* LoadMaterialByPath(const FString& MaterialPath, FString& OutAssetPath, FString& OutObjectPath) const;
    UMaterialFunction* LoadMaterialFunctionByPath(const FString& FunctionPath, FString& OutAssetPath, FString& OutObjectPath) const;
    UObject* LoadAssetByPath(const FString& AssetPath, FString& OutNormalizedAssetPath, FString& OutObjectPath) const;
    UMaterialExpression* FindExpressionByReference(UMaterial* Material, const FString& ExpressionReference, FString* OutErrorMessage = nullptr) const;
    UMaterialExpression* FindExpressionByReference(UMaterialFunction* MaterialFunction, const FString& ExpressionReference, FString* OutErrorMessage = nullptr) const;
    UClass* ResolveExpressionClass(const FString& ExpressionType) const;
    bool SaveMaterialAsset(UMaterial* Material, const FString& AssetPath, FString& OutErrorMessage) const;
    bool SaveLoadedAsset(UObject* Asset, const FString& AssetPath, FString& OutErrorMessage) const;
    bool RecompileMaterialAsset(UMaterial* Material) const;
    bool SetMaterialPropertyValue(UObject* TargetObject, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutErrorMessage) const;
    bool TrySetExpressionLabel(UMaterialExpression* Expression, const FString& ExpressionName) const;
    bool TryGetExpressionLabel(const UMaterialExpression* Expression, FString& OutLabel) const;
    bool TrySetMaterialShadingModel(UMaterial* Material, const FString& ShadingModelName, FString& OutErrorMessage) const;
    bool TryParseMaterialProperty(const FString& PropertyName, EMaterialProperty& OutProperty) const;
    TSharedPtr<FJsonObject> ExpressionToJson(UMaterialExpression* Expression) const;
    TSharedPtr<FJsonObject> ValidateMaterialGraph(UMaterial* Material) const;
    TSharedPtr<FJsonObject> GetMaterialCompileStatus(UMaterial* Material) const;
    bool ApplyMaterialProperties(UMaterial* Material, const TSharedPtr<FJsonObject>& PropertiesObject, FString& OutErrorMessage) const;
    bool RollbackMaterialFromBackup(UMaterial* Material, UMaterial* BackupMaterial, bool bWasDirty, FString& OutErrorMessage) const;
    bool ReloadAssetPackageFromDisk(UObject* Asset, bool bCloseEditors, bool bFailIfDirty, FString& OutErrorMessage) const;
    bool RebuildExpressionGraph(UObject* Owner, UMaterial* Material, UMaterialFunction* MaterialFunction, const TSharedPtr<FJsonObject>& GraphSpec, TArray<TSharedPtr<FJsonValue>>& OutNodes, TArray<TSharedPtr<FJsonValue>>& OutComments, FString& OutErrorMessage) const;
};
