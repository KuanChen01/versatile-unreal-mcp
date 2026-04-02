#pragma once

#include "CoreMinimal.h"
#include "Json.h"
#include "SceneTypes.h"

class UMaterial;
class UMaterialExpression;

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

    UMaterial* LoadMaterialByPath(const FString& MaterialPath, FString& OutAssetPath, FString& OutObjectPath) const;
    UMaterialExpression* FindExpressionByReference(UMaterial* Material, const FString& ExpressionReference) const;
    UClass* ResolveExpressionClass(const FString& ExpressionType) const;
    bool SaveMaterialAsset(UMaterial* Material, const FString& AssetPath, FString& OutErrorMessage) const;
    bool RecompileMaterialAsset(UMaterial* Material) const;
    bool SetMaterialPropertyValue(UObject* TargetObject, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutErrorMessage) const;
    bool TrySetExpressionLabel(UMaterialExpression* Expression, const FString& ExpressionName) const;
    bool TryGetExpressionLabel(const UMaterialExpression* Expression, FString& OutLabel) const;
    bool TrySetMaterialShadingModel(UMaterial* Material, const FString& ShadingModelName, FString& OutErrorMessage) const;
    bool TryParseMaterialProperty(const FString& PropertyName, EMaterialProperty& OutProperty) const;
    TSharedPtr<FJsonObject> ExpressionToJson(UMaterialExpression* Expression) const;
};
