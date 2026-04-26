#include "Commands/UnrealMCPMaterialCommands.h"

#include "Commands/UnrealMCPCommonUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "RHIShaderPlatform.h"
#include "SceneTypes.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

FUnrealMCPMaterialCommands::FUnrealMCPMaterialCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_material"))
    {
        return HandleCreateMaterial(Params);
    }
    if (CommandType == TEXT("set_material_properties"))
    {
        return HandleSetMaterialProperties(Params);
    }
    if (CommandType == TEXT("add_material_expression"))
    {
        return HandleAddMaterialExpression(Params);
    }
    if (CommandType == TEXT("set_material_expression_property"))
    {
        return HandleSetMaterialExpressionProperty(Params);
    }
    if (CommandType == TEXT("connect_material_expressions"))
    {
        return HandleConnectMaterialExpressions(Params);
    }
    if (CommandType == TEXT("connect_material_property"))
    {
        return HandleConnectMaterialProperty(Params);
    }
    if (CommandType == TEXT("recompile_material"))
    {
        return HandleRecompileMaterial(Params);
    }
    if (CommandType == TEXT("configure_glass_material"))
    {
        return HandleConfigureGlassMaterial(Params);
    }
    if (CommandType == TEXT("rebuild_material_graph"))
    {
        return HandleRebuildMaterialGraph(Params);
    }
    if (CommandType == TEXT("get_material_compile_status"))
    {
        return HandleGetMaterialCompileStatus(Params);
    }
    if (CommandType == TEXT("validate_material_graph"))
    {
        return HandleValidateMaterialGraph(Params);
    }
    if (CommandType == TEXT("reload_asset_from_disk"))
    {
        return HandleReloadAssetFromDisk(Params);
    }
    if (CommandType == TEXT("close_asset_editor"))
    {
        return HandleCloseAssetEditor(Params);
    }
    if (CommandType == TEXT("is_asset_loaded_dirty"))
    {
        return HandleIsAssetLoadedDirty(Params);
    }
    if (CommandType == TEXT("create_material_function"))
    {
        return HandleCreateMaterialFunction(Params);
    }
    if (CommandType == TEXT("rebuild_material_function_graph"))
    {
        return HandleRebuildMaterialFunctionGraph(Params);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown material command: %s"), *CommandType));
}

namespace
{
    static FString NormalizeAssetPath(const FString& InPath)
    {
        FString AssetPath = InPath;
        AssetPath.TrimStartAndEndInline();

        const int32 DotIndex = AssetPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        const int32 SlashIndex = AssetPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        if (DotIndex != INDEX_NONE && DotIndex > SlashIndex)
        {
            AssetPath = AssetPath.Left(DotIndex);
        }
        return AssetPath;
    }

    static FString MakeObjectPath(const FString& AssetPath)
    {
        const int32 SlashIndex = AssetPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        const FString LeafName = SlashIndex != INDEX_NONE ? AssetPath.Mid(SlashIndex + 1) : AssetPath;
        return FString::Printf(TEXT("%s.%s"), *AssetPath, *LeafName);
    }

    static TSharedPtr<FJsonValue> MakeJsonString(const FString& Value)
    {
        return MakeShared<FJsonValueString>(Value);
    }

    static TSharedPtr<FJsonValue> MakeJsonNumber(double Value)
    {
        return MakeShared<FJsonValueNumber>(Value);
    }

    static TSharedPtr<FJsonValue> MakeJsonBool(bool Value)
    {
        return MakeShared<FJsonValueBoolean>(Value);
    }

    static FString NormalizeEnumToken(const FString& InValue)
    {
        FString Result = InValue;
        Result.TrimStartAndEndInline();
        Result.ReplaceInline(TEXT(" "), TEXT(""));
        Result.ReplaceInline(TEXT("-"), TEXT(""));
        Result.ReplaceInline(TEXT("_"), TEXT(""));
        return Result.ToLower();
    }

    static bool TryGetNumericValue(const TSharedPtr<FJsonValue>& Value, double& OutValue)
    {
        if (!Value.IsValid())
        {
            return false;
        }

        if (Value->Type == EJson::Number)
        {
            OutValue = Value->AsNumber();
            return true;
        }

        if (Value->Type == EJson::Boolean)
        {
            OutValue = Value->AsBool() ? 1.0 : 0.0;
            return true;
        }

        if (Value->Type == EJson::String)
        {
            const FString RawValue = Value->AsString();
            if (RawValue.IsNumeric())
            {
                OutValue = FCString::Atod(*RawValue);
                return true;
            }
        }

        return false;
    }

    static bool TryGetFloatArrayValue(const TSharedPtr<FJsonValue>& Value, TArray<double>& OutValues)
    {
        OutValues.Reset();

        if (!Value.IsValid() || Value->Type != EJson::Array)
        {
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>& JsonArray = Value->AsArray();
        for (const TSharedPtr<FJsonValue>& ArrayValue : JsonArray)
        {
            double NumericValue = 0.0;
            if (!TryGetNumericValue(ArrayValue, NumericValue))
            {
                return false;
            }

            OutValues.Add(NumericValue);
        }

        return OutValues.Num() > 0;
    }

    static bool TryGetNumberFromObjectField(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, double& OutValue)
    {
        if (!JsonObject.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonValue>* JsonValue = JsonObject->Values.Find(FieldName);
        return JsonValue && TryGetNumericValue(*JsonValue, OutValue);
    }

    static bool TryParseBlendModeValue(const TSharedPtr<FJsonValue>& Value, EBlendMode& OutBlendMode)
    {
        if (!Value.IsValid())
        {
            return false;
        }

        if (Value->Type == EJson::Number)
        {
            OutBlendMode = static_cast<EBlendMode>(static_cast<int32>(Value->AsNumber()));
            return true;
        }

        if (Value->Type != EJson::String)
        {
            return false;
        }

        const FString RawToken = Value->AsString();
        const FString NormalizedToken = NormalizeEnumToken(RawToken);

        struct FBlendModeEntry
        {
            const TCHAR* Token;
            EBlendMode BlendMode;
        };

        static const FBlendModeEntry Entries[] = {
            { TEXT("opaque"), BLEND_Opaque },
            { TEXT("blendopaque"), BLEND_Opaque },
            { TEXT("masked"), BLEND_Masked },
            { TEXT("blendmasked"), BLEND_Masked },
            { TEXT("translucent"), BLEND_Translucent },
            { TEXT("blendtranslucent"), BLEND_Translucent },
            { TEXT("additive"), BLEND_Additive },
            { TEXT("blendadditive"), BLEND_Additive },
            { TEXT("modulate"), BLEND_Modulate },
            { TEXT("blendmodulate"), BLEND_Modulate },
            { TEXT("alphacomposite"), BLEND_AlphaComposite },
            { TEXT("blendalphacomposite"), BLEND_AlphaComposite },
            { TEXT("alphaholdout"), BLEND_AlphaHoldout },
            { TEXT("blendalphaholdout"), BLEND_AlphaHoldout }
        };

        for (const FBlendModeEntry& Entry : Entries)
        {
            if (NormalizedToken == Entry.Token)
            {
                OutBlendMode = Entry.BlendMode;
                return true;
            }
        }

        return false;
    }

    static FString GetBlendModeLabel(EBlendMode BlendMode)
    {
        switch (BlendMode)
        {
        case BLEND_Opaque:
            return TEXT("BLEND_Opaque");
        case BLEND_Masked:
            return TEXT("BLEND_Masked");
        case BLEND_Translucent:
            return TEXT("BLEND_Translucent");
        case BLEND_Additive:
            return TEXT("BLEND_Additive");
        case BLEND_Modulate:
            return TEXT("BLEND_Modulate");
        case BLEND_AlphaComposite:
            return TEXT("BLEND_AlphaComposite");
        case BLEND_AlphaHoldout:
            return TEXT("BLEND_AlphaHoldout");
        default:
            return TEXT("Unknown");
        }
    }

    static bool TryParseTranslucencyLightingModeValue(const TSharedPtr<FJsonValue>& Value, ETranslucencyLightingMode& OutMode)
    {
        if (!Value.IsValid())
        {
            return false;
        }

        if (Value->Type == EJson::Number)
        {
            OutMode = static_cast<ETranslucencyLightingMode>(static_cast<int32>(Value->AsNumber()));
            return true;
        }

        if (Value->Type != EJson::String)
        {
            return false;
        }

        const FString NormalizedToken = NormalizeEnumToken(Value->AsString());

        struct FLightingModeEntry
        {
            const TCHAR* Token;
            ETranslucencyLightingMode Mode;
        };

        static const FLightingModeEntry Entries[] = {
            { TEXT("tlmvolumetricnondirectional"), TLM_VolumetricNonDirectional },
            { TEXT("volumetricnondirectional"), TLM_VolumetricNonDirectional },
            { TEXT("tlmvolumetricdirectional"), TLM_VolumetricDirectional },
            { TEXT("volumetricdirectional"), TLM_VolumetricDirectional },
            { TEXT("tlmvolumetricpervertexnondirectional"), TLM_VolumetricPerVertexNonDirectional },
            { TEXT("volumetricpervertexnondirectional"), TLM_VolumetricPerVertexNonDirectional },
            { TEXT("tlmvolumetricpervertexdirectional"), TLM_VolumetricPerVertexDirectional },
            { TEXT("volumetricpervertexdirectional"), TLM_VolumetricPerVertexDirectional },
            { TEXT("tlmsurface"), TLM_Surface },
            { TEXT("surface"), TLM_Surface },
            { TEXT("surfacetranslucencyvolume"), TLM_Surface },
            { TEXT("tlmsurfaceperpixellighting"), TLM_SurfacePerPixelLighting },
            { TEXT("surfaceperpixellighting"), TLM_SurfacePerPixelLighting },
            { TEXT("surfaceforwardshading"), TLM_SurfacePerPixelLighting }
        };

        for (const FLightingModeEntry& Entry : Entries)
        {
            if (NormalizedToken == Entry.Token)
            {
                OutMode = Entry.Mode;
                return true;
            }
        }

        return false;
    }

    static bool TryParseRefractionModeValue(const TSharedPtr<FJsonValue>& Value, ERefractionMode& OutMode)
    {
        if (!Value.IsValid())
        {
            return false;
        }

        if (Value->Type == EJson::Number)
        {
            OutMode = static_cast<ERefractionMode>(static_cast<int32>(Value->AsNumber()));
            return true;
        }

        if (Value->Type != EJson::String)
        {
            return false;
        }

        const FString NormalizedToken = NormalizeEnumToken(Value->AsString());

        struct FRefractionModeEntry
        {
            const TCHAR* Token;
            ERefractionMode Mode;
        };

        static const FRefractionModeEntry Entries[] = {
            { TEXT("rmindexofrefraction"), RM_IndexOfRefraction },
            { TEXT("indexofrefraction"), RM_IndexOfRefraction },
            { TEXT("rm2doffset"), RM_2DOffset },
            { TEXT("2doffset"), RM_2DOffset },
            { TEXT("rmpixelnormaloffset"), RM_PixelNormalOffset },
            { TEXT("pixelnormaloffset"), RM_PixelNormalOffset },
            { TEXT("rmindexofrefractionfromf0"), RM_IndexOfRefractionFromF0 },
            { TEXT("indexofrefractionfromf0"), RM_IndexOfRefractionFromF0 }
        };

        for (const FRefractionModeEntry& Entry : Entries)
        {
            if (NormalizedToken == Entry.Token)
            {
                OutMode = Entry.Mode;
                return true;
            }
        }

        return false;
    }

    static bool GetBoolFieldDefault(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, bool DefaultValue)
    {
        return JsonObject.IsValid() && JsonObject->HasField(FieldName) ? JsonObject->GetBoolField(FieldName) : DefaultValue;
    }

    static FString GuidToStableString(const FGuid& Guid)
    {
        return Guid.IsValid() ? Guid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
    }

    static bool TryReadExpressionReferenceValue(const TSharedPtr<FJsonValue>& JsonValue, FString& OutReference)
    {
        if (!JsonValue.IsValid())
        {
            return false;
        }

        if (JsonValue->Type == EJson::String)
        {
            OutReference = JsonValue->AsString();
            return !OutReference.IsEmpty();
        }

        if (JsonValue->Type != EJson::Object)
        {
            return false;
        }

        const TSharedPtr<FJsonObject> RefObject = JsonValue->AsObject();
        static const TCHAR* RefFields[] = {
            TEXT("object_path"),
            TEXT("expression_guid"),
            TEXT("guid"),
            TEXT("stable_id"),
            TEXT("id"),
            TEXT("name"),
            TEXT("label")
        };

        for (const TCHAR* FieldName : RefFields)
        {
            FString Candidate;
            if (RefObject->TryGetStringField(FieldName, Candidate) && !Candidate.IsEmpty())
            {
                OutReference = Candidate;
                return true;
            }
        }

        return false;
    }

    static bool TryReadExpressionReferenceField(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, FString& OutReference)
    {
        if (!JsonObject.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonValue>* JsonValue = JsonObject->Values.Find(FieldName);
        return JsonValue && TryReadExpressionReferenceValue(*JsonValue, OutReference);
    }

    static FVector2D GetPositionFromJsonObject(const TSharedPtr<FJsonObject>& JsonObject, const FVector2D& DefaultValue)
    {
        if (!JsonObject.IsValid() || !JsonObject->HasTypedField<EJson::Array>(TEXT("position")))
        {
            return DefaultValue;
        }

        const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
        if (JsonObject->TryGetArrayField(TEXT("position"), JsonArray) && JsonArray && JsonArray->Num() >= 2)
        {
            return FVector2D(
                static_cast<float>((*JsonArray)[0]->AsNumber()),
                static_cast<float>((*JsonArray)[1]->AsNumber()));
        }

        return DefaultValue;
    }

    static void AddVector2DJson(TSharedPtr<FJsonObject> JsonObject, const TCHAR* FieldName, const FVector2D& Value)
    {
        TArray<TSharedPtr<FJsonValue>> Array;
        Array.Add(MakeJsonNumber(Value.X));
        Array.Add(MakeJsonNumber(Value.Y));
        JsonObject->SetArrayField(FieldName, Array);
    }

    static TSharedPtr<FJsonObject> MakeGraphIssue(const FString& Severity, const FString& Message, const FString& NodePath = FString(), const FString& NodeGuid = FString())
    {
        TSharedPtr<FJsonObject> IssueObject = MakeShared<FJsonObject>();
        IssueObject->SetStringField(TEXT("severity"), Severity);
        IssueObject->SetStringField(TEXT("message"), Message);
        if (!NodePath.IsEmpty())
        {
            IssueObject->SetStringField(TEXT("node_object_path"), NodePath);
        }
        if (!NodeGuid.IsEmpty())
        {
            IssueObject->SetStringField(TEXT("node_guid"), NodeGuid);
        }
        return IssueObject;
    }

    static void AppendStringArray(TSharedPtr<FJsonObject> JsonObject, const TCHAR* FieldName, const TArray<FString>& Strings)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        for (const FString& StringValue : Strings)
        {
            Values.Add(MakeJsonString(StringValue));
        }
        JsonObject->SetArrayField(FieldName, Values);
    }

    static void CollectMaterialExpressions(UMaterial* Material, TArray<UMaterialExpression*>& OutExpressions, TArray<UMaterialExpressionComment*>& OutComments)
    {
        OutExpressions.Reset();
        OutComments.Reset();
        if (!Material)
        {
            return;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (Expression)
            {
                OutExpressions.Add(Expression);
            }
        }

        for (UMaterialExpressionComment* Comment : Material->GetEditorComments())
        {
            if (Comment)
            {
                OutComments.Add(Comment);
            }
        }
    }

    static void CollectFunctionExpressions(UMaterialFunction* MaterialFunction, TArray<UMaterialExpression*>& OutExpressions, TArray<UMaterialExpressionComment*>& OutComments)
    {
        OutExpressions.Reset();
        OutComments.Reset();
        if (!MaterialFunction)
        {
            return;
        }

        for (UMaterialExpression* Expression : MaterialFunction->GetExpressions())
        {
            if (Expression)
            {
                OutExpressions.Add(Expression);
            }
        }

        for (UMaterialExpressionComment* Comment : MaterialFunction->GetEditorComments())
        {
            if (Comment)
            {
                OutComments.Add(Comment);
            }
        }
    }

    static void ClearMaterialComments(UMaterial* Material)
    {
        if (!Material)
        {
            return;
        }
        Material->GetExpressionCollection().EditorComments.Empty();
    }

    static void ClearFunctionComments(UMaterialFunction* MaterialFunction)
    {
        if (!MaterialFunction)
        {
            return;
        }
        MaterialFunction->GetExpressionCollection().EditorComments.Empty();
    }

    static UMaterialExpressionComment* CreateMaterialComment(UObject* Owner, const FVector2D& Position, const FVector2D& Size, const FString& Text)
    {
        UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Owner, NAME_None, RF_Transactional);
        if (!Comment)
        {
            return nullptr;
        }

        Comment->MaterialExpressionEditorX = static_cast<int32>(Position.X);
        Comment->MaterialExpressionEditorY = static_cast<int32>(Position.Y);
        Comment->SizeX = static_cast<int32>(Size.X);
        Comment->SizeY = static_cast<int32>(Size.Y);
        Comment->Text = Text;
        Comment->UpdateMaterialExpressionGuid(true, false);

        if (UMaterial* Material = Cast<UMaterial>(Owner))
        {
            Comment->Material = Material;
            Material->GetExpressionCollection().AddComment(Comment);
        }
        else if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Owner))
        {
            Comment->Function = MaterialFunction;
            MaterialFunction->GetExpressionCollection().AddComment(Comment);
        }

        return Comment;
    }

    static FString GetMaterialPropertyLabel(EMaterialProperty Property)
    {
        switch (Property)
        {
        case MP_BaseColor:
            return TEXT("BaseColor");
        case MP_Metallic:
            return TEXT("Metallic");
        case MP_Specular:
            return TEXT("Specular");
        case MP_Roughness:
            return TEXT("Roughness");
        case MP_EmissiveColor:
            return TEXT("EmissiveColor");
        case MP_Opacity:
            return TEXT("Opacity");
        case MP_OpacityMask:
            return TEXT("OpacityMask");
        case MP_Normal:
            return TEXT("Normal");
        case MP_WorldPositionOffset:
            return TEXT("WorldPositionOffset");
        case MP_AmbientOcclusion:
            return TEXT("AmbientOcclusion");
        case MP_Refraction:
            return TEXT("Refraction");
        case MP_PixelDepthOffset:
            return TEXT("PixelDepthOffset");
        default:
            return FString::Printf(TEXT("MaterialProperty_%d"), static_cast<int32>(Property));
        }
    }
}

UMaterial* FUnrealMCPMaterialCommands::LoadMaterialByPath(const FString& MaterialPath, FString& OutAssetPath, FString& OutObjectPath) const
{
    OutAssetPath = NormalizeAssetPath(MaterialPath);
    OutObjectPath = MakeObjectPath(OutAssetPath);

    if (UMaterial* Material = LoadObject<UMaterial>(nullptr, *OutObjectPath))
    {
        return Material;
    }

    return Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(OutAssetPath));
}

UMaterialFunction* FUnrealMCPMaterialCommands::LoadMaterialFunctionByPath(const FString& FunctionPath, FString& OutAssetPath, FString& OutObjectPath) const
{
    OutAssetPath = NormalizeAssetPath(FunctionPath);
    OutObjectPath = MakeObjectPath(OutAssetPath);

    if (UMaterialFunction* Function = LoadObject<UMaterialFunction>(nullptr, *OutObjectPath))
    {
        return Function;
    }

    return Cast<UMaterialFunction>(UEditorAssetLibrary::LoadAsset(OutAssetPath));
}

UObject* FUnrealMCPMaterialCommands::LoadAssetByPath(const FString& AssetPath, FString& OutNormalizedAssetPath, FString& OutObjectPath) const
{
    OutNormalizedAssetPath = NormalizeAssetPath(AssetPath);
    OutObjectPath = MakeObjectPath(OutNormalizedAssetPath);

    if (UObject* Object = LoadObject<UObject>(nullptr, *OutObjectPath))
    {
        return Object;
    }

    return UEditorAssetLibrary::LoadAsset(OutNormalizedAssetPath);
}

UClass* FUnrealMCPMaterialCommands::ResolveExpressionClass(const FString& ExpressionType) const
{
    TArray<FString> CandidateNames;
    CandidateNames.Add(ExpressionType);

    if (!ExpressionType.StartsWith(TEXT("MaterialExpression")))
    {
        CandidateNames.Add(TEXT("MaterialExpression") + ExpressionType);
    }

    for (const FString& CandidateName : CandidateNames)
    {
        if (UClass* FoundClass = FindFirstObjectSafe<UClass>(*CandidateName))
        {
            if (FoundClass->IsChildOf(UMaterialExpression::StaticClass()))
            {
                return FoundClass;
            }
        }

        const FString ScriptPath = FString::Printf(TEXT("/Script/Engine.%s"), *CandidateName);
        if (UClass* LoadedClass = LoadClass<UMaterialExpression>(nullptr, *ScriptPath))
        {
            return LoadedClass;
        }
    }

    return nullptr;
}

bool FUnrealMCPMaterialCommands::TrySetExpressionLabel(UMaterialExpression* Expression, const FString& ExpressionName) const
{
    if (!Expression || ExpressionName.IsEmpty())
    {
        return false;
    }

    Expression->Desc = ExpressionName;

    if (FProperty* Property = Expression->GetClass()->FindPropertyByName(TEXT("ParameterName")))
    {
        if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
        {
            NameProperty->SetPropertyValue_InContainer(Expression, FName(*ExpressionName));
        }
    }

    return true;
}

bool FUnrealMCPMaterialCommands::TryGetExpressionLabel(const UMaterialExpression* Expression, FString& OutLabel) const
{
    if (!Expression)
    {
        return false;
    }

    if (!Expression->Desc.IsEmpty())
    {
        OutLabel = Expression->Desc;
        return true;
    }

    if (const FProperty* Property = Expression->GetClass()->FindPropertyByName(TEXT("ParameterName")))
    {
        if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
        {
            const FName NameValue = NameProperty->GetPropertyValue_InContainer(Expression);
            if (!NameValue.IsNone())
            {
                OutLabel = NameValue.ToString();
                return true;
            }
        }
    }

    OutLabel = Expression->GetName();
    return true;
}

UMaterialExpression* FUnrealMCPMaterialCommands::FindExpressionByReference(UMaterial* Material, const FString& ExpressionReference, FString* OutErrorMessage) const
{
    if (!Material || ExpressionReference.IsEmpty())
    {
        return nullptr;
    }

    FGuid ReferenceGuid;
    const bool bReferenceIsGuid = FGuid::Parse(ExpressionReference, ReferenceGuid);
    TArray<UMaterialExpression*> LegacyMatches;

    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (!Expression)
        {
            continue;
        }

        if (Expression->GetPathName().Equals(ExpressionReference, ESearchCase::CaseSensitive))
        {
            return Expression;
        }

        if (bReferenceIsGuid && Expression->GetMaterialExpressionId() == ReferenceGuid)
        {
            return Expression;
        }

        if (Expression->GetName().Equals(ExpressionReference, ESearchCase::CaseSensitive))
        {
            return Expression;
        }

        FString Label;
        TryGetExpressionLabel(Expression, Label);
        if (Label.Equals(ExpressionReference, ESearchCase::CaseSensitive))
        {
            LegacyMatches.Add(Expression);
        }
    }

    if (LegacyMatches.Num() == 1)
    {
        return LegacyMatches[0];
    }

    if (LegacyMatches.Num() > 1 && OutErrorMessage)
    {
        *OutErrorMessage = FString::Printf(TEXT("Ambiguous material expression reference '%s'; use object_path or expression_guid"), *ExpressionReference);
    }

    return nullptr;
}

UMaterialExpression* FUnrealMCPMaterialCommands::FindExpressionByReference(UMaterialFunction* MaterialFunction, const FString& ExpressionReference, FString* OutErrorMessage) const
{
    if (!MaterialFunction || ExpressionReference.IsEmpty())
    {
        return nullptr;
    }

    FGuid ReferenceGuid;
    const bool bReferenceIsGuid = FGuid::Parse(ExpressionReference, ReferenceGuid);
    TArray<UMaterialExpression*> LegacyMatches;

    for (UMaterialExpression* Expression : MaterialFunction->GetExpressions())
    {
        if (!Expression)
        {
            continue;
        }

        if (Expression->GetPathName().Equals(ExpressionReference, ESearchCase::CaseSensitive))
        {
            return Expression;
        }

        if (bReferenceIsGuid && Expression->GetMaterialExpressionId() == ReferenceGuid)
        {
            return Expression;
        }

        if (Expression->GetName().Equals(ExpressionReference, ESearchCase::CaseSensitive))
        {
            return Expression;
        }

        FString Label;
        TryGetExpressionLabel(Expression, Label);
        if (Label.Equals(ExpressionReference, ESearchCase::CaseSensitive))
        {
            LegacyMatches.Add(Expression);
        }
    }

    if (LegacyMatches.Num() == 1)
    {
        return LegacyMatches[0];
    }

    if (LegacyMatches.Num() > 1 && OutErrorMessage)
    {
        *OutErrorMessage = FString::Printf(TEXT("Ambiguous material function expression reference '%s'; use object_path or expression_guid"), *ExpressionReference);
    }

    return nullptr;
}

bool FUnrealMCPMaterialCommands::TrySetMaterialShadingModel(UMaterial* Material, const FString& ShadingModelName, FString& OutErrorMessage) const
{
    if (!Material)
    {
        OutErrorMessage = TEXT("Invalid material");
        return false;
    }

    const FString NormalizedToken = NormalizeEnumToken(ShadingModelName);
    EMaterialShadingModel ShadingModel = MSM_MAX;

    if (NormalizedToken == TEXT("defaultlit") || NormalizedToken == TEXT("msmdefaultlit"))
    {
        ShadingModel = MSM_DefaultLit;
    }
    else if (NormalizedToken == TEXT("unlit") || NormalizedToken == TEXT("msmunlit"))
    {
        ShadingModel = MSM_Unlit;
    }
    else if (NormalizedToken == TEXT("subsurface") || NormalizedToken == TEXT("msmsubsurface"))
    {
        ShadingModel = MSM_Subsurface;
    }
    else if (NormalizedToken == TEXT("clearcoat") || NormalizedToken == TEXT("msmclearcoat"))
    {
        ShadingModel = MSM_ClearCoat;
    }
    else if (NormalizedToken == TEXT("singleslayerwater") || NormalizedToken == TEXT("msmsingleslayerwater"))
    {
        ShadingModel = MSM_SingleLayerWater;
    }

    if (ShadingModel == MSM_MAX)
    {
        OutErrorMessage = FString::Printf(TEXT("Unsupported shading model: %s"), *ShadingModelName);
        return false;
    }

    Material->SetShadingModel(ShadingModel);
    return true;
}

bool FUnrealMCPMaterialCommands::TryParseMaterialProperty(const FString& PropertyName, EMaterialProperty& OutProperty) const
{
    const FString NormalizedToken = NormalizeEnumToken(PropertyName);

    struct FMaterialPropertyEntry
    {
        const TCHAR* Token;
        EMaterialProperty Property;
    };

    static const FMaterialPropertyEntry Entries[] = {
        { TEXT("mpbasecolor"), MP_BaseColor },
        { TEXT("basecolor"), MP_BaseColor },
        { TEXT("diffuse"), MP_BaseColor },
        { TEXT("mpmetallic"), MP_Metallic },
        { TEXT("metallic"), MP_Metallic },
        { TEXT("mpspecular"), MP_Specular },
        { TEXT("specular"), MP_Specular },
        { TEXT("mproughness"), MP_Roughness },
        { TEXT("roughness"), MP_Roughness },
        { TEXT("mpnormal"), MP_Normal },
        { TEXT("normal"), MP_Normal },
        { TEXT("mpemissivecolor"), MP_EmissiveColor },
        { TEXT("emissive"), MP_EmissiveColor },
        { TEXT("emissivecolor"), MP_EmissiveColor },
        { TEXT("mpopacity"), MP_Opacity },
        { TEXT("opacity"), MP_Opacity },
        { TEXT("mpopacitymask"), MP_OpacityMask },
        { TEXT("opacitymask"), MP_OpacityMask },
        { TEXT("mprefraction"), MP_Refraction },
        { TEXT("refraction"), MP_Refraction },
        { TEXT("mpambientocclusion"), MP_AmbientOcclusion },
        { TEXT("ambientocclusion"), MP_AmbientOcclusion },
        { TEXT("ao"), MP_AmbientOcclusion },
        { TEXT("mpworldpositionoffset"), MP_WorldPositionOffset },
        { TEXT("worldpositionoffset"), MP_WorldPositionOffset },
        { TEXT("wpo"), MP_WorldPositionOffset },
        { TEXT("mppixeldepthoffset"), MP_PixelDepthOffset },
        { TEXT("pixeldepthoffset"), MP_PixelDepthOffset }
    };

    for (const FMaterialPropertyEntry& Entry : Entries)
    {
        if (NormalizedToken == Entry.Token)
        {
            OutProperty = Entry.Property;
            return true;
        }
    }

    return false;
}

bool FUnrealMCPMaterialCommands::ApplyMaterialProperties(UMaterial* Material, const TSharedPtr<FJsonObject>& PropertiesObject, FString& OutErrorMessage) const
{
    if (!Material)
    {
        OutErrorMessage = TEXT("Invalid material");
        return false;
    }

    if (!PropertiesObject.IsValid())
    {
        return true;
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObject->Values)
    {
        const FString& PropertyName = Pair.Key;
        const TSharedPtr<FJsonValue>& PropertyValue = Pair.Value;

        if (PropertyName == TEXT("blend_mode"))
        {
            EBlendMode BlendMode = BLEND_Opaque;
            if (!TryParseBlendModeValue(PropertyValue, BlendMode))
            {
                OutErrorMessage = TEXT("Invalid 'blend_mode' value");
                return false;
            }
            Material->BlendMode = BlendMode;
            continue;
        }

        if (PropertyName == TEXT("two_sided"))
        {
            Material->TwoSided = PropertyValue.IsValid() && PropertyValue->AsBool();
            continue;
        }

        if (PropertyName == TEXT("translucency_lighting_mode"))
        {
            ETranslucencyLightingMode LightingMode = TLM_SurfacePerPixelLighting;
            if (!TryParseTranslucencyLightingModeValue(PropertyValue, LightingMode))
            {
                OutErrorMessage = TEXT("Invalid 'translucency_lighting_mode' value");
                return false;
            }
            Material->TranslucencyLightingMode = LightingMode;
            continue;
        }

        if (PropertyName == TEXT("refraction_method"))
        {
            ERefractionMode RefractionMode = RM_IndexOfRefraction;
            if (!TryParseRefractionModeValue(PropertyValue, RefractionMode))
            {
                OutErrorMessage = TEXT("Invalid 'refraction_method' value");
                return false;
            }
            Material->RefractionMethod = RefractionMode;
            continue;
        }

        if (PropertyName == TEXT("shading_model"))
        {
            if (!PropertyValue.IsValid())
            {
                OutErrorMessage = TEXT("Invalid 'shading_model' value");
                return false;
            }

            if (!TrySetMaterialShadingModel(Material, PropertyValue->AsString(), OutErrorMessage))
            {
                return false;
            }
            continue;
        }

        if (!SetMaterialPropertyValue(Material, PropertyName, PropertyValue, OutErrorMessage))
        {
            return false;
        }
    }

    return true;
}

bool FUnrealMCPMaterialCommands::SetMaterialPropertyValue(UObject* TargetObject, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutErrorMessage) const
{
    if (!TargetObject)
    {
        OutErrorMessage = TEXT("Invalid target object");
        return false;
    }

    FProperty* Property = TargetObject->GetClass()->FindPropertyByName(*PropertyName);
    if (!Property)
    {
        OutErrorMessage = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
        return false;
    }

    void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(TargetObject);

    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        const UScriptStruct* StructType = StructProperty->Struct;
        TArray<double> ArrayValues;

        if (StructType == TBaseStructure<FLinearColor>::Get())
        {
            FLinearColor ParsedColor = FLinearColor::White;
            bool bParsed = false;

            if (TryGetFloatArrayValue(Value, ArrayValues) && ArrayValues.Num() >= 3)
            {
                ParsedColor = FLinearColor(
                    static_cast<float>(ArrayValues[0]),
                    static_cast<float>(ArrayValues[1]),
                    static_cast<float>(ArrayValues[2]),
                    static_cast<float>(ArrayValues.Num() >= 4 ? ArrayValues[3] : 1.0));
                bParsed = true;
            }
            else if (Value.IsValid() && Value->Type == EJson::Object)
            {
                const TSharedPtr<FJsonObject> JsonObject = Value->AsObject();
                double R = 1.0;
                double G = 1.0;
                double B = 1.0;
                double A = 1.0;
                bParsed =
                    (TryGetNumberFromObjectField(JsonObject, TEXT("r"), R) || TryGetNumberFromObjectField(JsonObject, TEXT("R"), R)) &&
                    (TryGetNumberFromObjectField(JsonObject, TEXT("g"), G) || TryGetNumberFromObjectField(JsonObject, TEXT("G"), G)) &&
                    (TryGetNumberFromObjectField(JsonObject, TEXT("b"), B) || TryGetNumberFromObjectField(JsonObject, TEXT("B"), B));
                TryGetNumberFromObjectField(JsonObject, TEXT("a"), A);
                TryGetNumberFromObjectField(JsonObject, TEXT("A"), A);

                if (bParsed)
                {
                    ParsedColor = FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
                }
            }

            if (!bParsed)
            {
                OutErrorMessage = FString::Printf(TEXT("Failed to parse FLinearColor for property %s"), *PropertyName);
                return false;
            }

            *StructProperty->ContainerPtrToValuePtr<FLinearColor>(TargetObject) = ParsedColor;
            return true;
        }

        if (StructType == TBaseStructure<FVector>::Get())
        {
            FVector ParsedVector = FVector::ZeroVector;
            bool bParsed = false;

            if (TryGetFloatArrayValue(Value, ArrayValues) && ArrayValues.Num() >= 3)
            {
                ParsedVector = FVector(
                    static_cast<float>(ArrayValues[0]),
                    static_cast<float>(ArrayValues[1]),
                    static_cast<float>(ArrayValues[2]));
                bParsed = true;
            }
            else if (Value.IsValid() && Value->Type == EJson::Object)
            {
                const TSharedPtr<FJsonObject> JsonObject = Value->AsObject();
                double X = 0.0;
                double Y = 0.0;
                double Z = 0.0;
                bParsed =
                    (TryGetNumberFromObjectField(JsonObject, TEXT("x"), X) || TryGetNumberFromObjectField(JsonObject, TEXT("X"), X)) &&
                    (TryGetNumberFromObjectField(JsonObject, TEXT("y"), Y) || TryGetNumberFromObjectField(JsonObject, TEXT("Y"), Y)) &&
                    (TryGetNumberFromObjectField(JsonObject, TEXT("z"), Z) || TryGetNumberFromObjectField(JsonObject, TEXT("Z"), Z));

                if (bParsed)
                {
                    ParsedVector = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
                }
            }

            if (!bParsed)
            {
                OutErrorMessage = FString::Printf(TEXT("Failed to parse FVector for property %s"), *PropertyName);
                return false;
            }

            *StructProperty->ContainerPtrToValuePtr<FVector>(TargetObject) = ParsedVector;
            return true;
        }

        if (StructType == TBaseStructure<FVector2D>::Get())
        {
            FVector2D ParsedVector2D = FVector2D::ZeroVector;
            bool bParsed = false;

            if (TryGetFloatArrayValue(Value, ArrayValues) && ArrayValues.Num() >= 2)
            {
                ParsedVector2D = FVector2D(
                    static_cast<float>(ArrayValues[0]),
                    static_cast<float>(ArrayValues[1]));
                bParsed = true;
            }
            else if (Value.IsValid() && Value->Type == EJson::Object)
            {
                const TSharedPtr<FJsonObject> JsonObject = Value->AsObject();
                double X = 0.0;
                double Y = 0.0;
                bParsed =
                    (TryGetNumberFromObjectField(JsonObject, TEXT("x"), X) || TryGetNumberFromObjectField(JsonObject, TEXT("X"), X)) &&
                    (TryGetNumberFromObjectField(JsonObject, TEXT("y"), Y) || TryGetNumberFromObjectField(JsonObject, TEXT("Y"), Y));

                if (bParsed)
                {
                    ParsedVector2D = FVector2D(static_cast<float>(X), static_cast<float>(Y));
                }
            }

            if (!bParsed)
            {
                OutErrorMessage = FString::Printf(TEXT("Failed to parse FVector2D for property %s"), *PropertyName);
                return false;
            }

            *StructProperty->ContainerPtrToValuePtr<FVector2D>(TargetObject) = ParsedVector2D;
            return true;
        }

        if (StructType == TBaseStructure<FRotator>::Get())
        {
            FRotator ParsedRotator = FRotator::ZeroRotator;
            bool bParsed = false;

            if (TryGetFloatArrayValue(Value, ArrayValues) && ArrayValues.Num() >= 3)
            {
                ParsedRotator = FRotator(
                    static_cast<float>(ArrayValues[0]),
                    static_cast<float>(ArrayValues[1]),
                    static_cast<float>(ArrayValues[2]));
                bParsed = true;
            }
            else if (Value.IsValid() && Value->Type == EJson::Object)
            {
                const TSharedPtr<FJsonObject> JsonObject = Value->AsObject();
                double Pitch = 0.0;
                double Yaw = 0.0;
                double Roll = 0.0;
                bParsed =
                    (TryGetNumberFromObjectField(JsonObject, TEXT("pitch"), Pitch) || TryGetNumberFromObjectField(JsonObject, TEXT("Pitch"), Pitch)) &&
                    (TryGetNumberFromObjectField(JsonObject, TEXT("yaw"), Yaw) || TryGetNumberFromObjectField(JsonObject, TEXT("Yaw"), Yaw)) &&
                    (TryGetNumberFromObjectField(JsonObject, TEXT("roll"), Roll) || TryGetNumberFromObjectField(JsonObject, TEXT("Roll"), Roll));

                if (bParsed)
                {
                    ParsedRotator = FRotator(static_cast<float>(Pitch), static_cast<float>(Yaw), static_cast<float>(Roll));
                }
            }

            if (!bParsed)
            {
                OutErrorMessage = FString::Printf(TEXT("Failed to parse FRotator for property %s"), *PropertyName);
                return false;
            }

            *StructProperty->ContainerPtrToValuePtr<FRotator>(TargetObject) = ParsedRotator;
            return true;
        }

        OutErrorMessage = FString::Printf(TEXT("Unsupported struct property type: %s for property %s"), *StructType->GetName(), *PropertyName);
        return false;
    }

    if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
    {
        NameProperty->SetPropertyValue_InContainer(TargetObject, FName(*Value->AsString()));
        return true;
    }

    if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
    {
        DoubleProperty->SetPropertyValue(PropertyAddr, Value->AsNumber());
        return true;
    }

    if (FInt64Property* Int64Property = CastField<FInt64Property>(Property))
    {
        Int64Property->SetPropertyValue(PropertyAddr, static_cast<int64>(Value->AsNumber()));
        return true;
    }

    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        if (!Value.IsValid() || Value->Type != EJson::String)
        {
            OutErrorMessage = FString::Printf(TEXT("Object property %s expects an asset path string"), *PropertyName);
            return false;
        }

        UObject* LoadedObject = LoadObject<UObject>(nullptr, *Value->AsString());
        if (!LoadedObject)
        {
            LoadedObject = UEditorAssetLibrary::LoadAsset(Value->AsString());
        }

        if (!LoadedObject || !LoadedObject->IsA(ObjectProperty->PropertyClass))
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to load compatible object for property %s: %s"), *PropertyName, *Value->AsString());
            return false;
        }

        ObjectProperty->SetObjectPropertyValue(PropertyAddr, LoadedObject);
        return true;
    }

    return FUnrealMCPCommonUtils::SetObjectProperty(TargetObject, PropertyName, Value, OutErrorMessage);
}

bool FUnrealMCPMaterialCommands::RecompileMaterialAsset(UMaterial* Material) const
{
    if (!Material)
    {
        return false;
    }

    UMaterialEditingLibrary::RecompileMaterial(Material);
    Material->PostEditChange();
    Material->MarkPackageDirty();
    return true;
}

bool FUnrealMCPMaterialCommands::SaveMaterialAsset(UMaterial* Material, const FString& AssetPath, FString& OutErrorMessage) const
{
    return SaveLoadedAsset(Material, AssetPath, OutErrorMessage);
}

bool FUnrealMCPMaterialCommands::SaveLoadedAsset(UObject* Asset, const FString& AssetPath, FString& OutErrorMessage) const
{
    if (!Asset)
    {
        OutErrorMessage = TEXT("Invalid asset");
        return false;
    }

    Asset->MarkPackageDirty();

    if (UEditorAssetLibrary::SaveLoadedAsset(Asset, true))
    {
        return true;
    }

    if (!AssetPath.IsEmpty() && UEditorAssetLibrary::SaveAsset(AssetPath, true))
    {
        return true;
    }

    OutErrorMessage = FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath);
    return false;
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::ExpressionToJson(UMaterialExpression* Expression) const
{
    TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
    if (!Expression)
    {
        return JsonObject;
    }

    FString Label;
    TryGetExpressionLabel(Expression, Label);

    JsonObject->SetStringField(TEXT("id"), Expression->GetName());
    JsonObject->SetStringField(TEXT("name"), Expression->GetName());
    JsonObject->SetStringField(TEXT("label"), Label);
    JsonObject->SetStringField(TEXT("type"), Expression->GetClass()->GetName());
    JsonObject->SetStringField(TEXT("object_path"), Expression->GetPathName());
    JsonObject->SetStringField(TEXT("expression_guid"), GuidToStableString(Expression->GetMaterialExpressionId()));
    JsonObject->SetStringField(TEXT("stable_id"), GuidToStableString(Expression->GetMaterialExpressionId()));

    TArray<TSharedPtr<FJsonValue>> PositionArray;
    PositionArray.Add(MakeJsonNumber(Expression->MaterialExpressionEditorX));
    PositionArray.Add(MakeJsonNumber(Expression->MaterialExpressionEditorY));
    JsonObject->SetArrayField(TEXT("position"), PositionArray);

    return JsonObject;
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::GetMaterialCompileStatus(UMaterial* Material) const
{
    TSharedPtr<FJsonObject> StatusObject = MakeShared<FJsonObject>();
    if (!Material)
    {
        StatusObject->SetBoolField(TEXT("success"), false);
        StatusObject->SetStringField(TEXT("error"), TEXT("Invalid material"));
        return StatusObject;
    }

    FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
    TArray<FString> CompileErrors;
    TArray<TSharedPtr<FJsonValue>> ErrorNodes;

    if (Resource)
    {
        CompileErrors = Resource->GetCompileErrors();
        for (UMaterialExpression* ErrorExpression : Resource->GetErrorExpressions())
        {
            if (ErrorExpression)
            {
                ErrorNodes.Add(MakeShared<FJsonValueObject>(ExpressionToJson(ErrorExpression)));
            }
        }
    }

    FMaterialStatistics Statistics = UMaterialEditingLibrary::GetStatistics(Material);
    TSharedPtr<FJsonObject> StatisticsObject = MakeShared<FJsonObject>();
    StatisticsObject->SetNumberField(TEXT("vertex_shader_instructions"), Statistics.NumVertexShaderInstructions);
    StatisticsObject->SetNumberField(TEXT("pixel_shader_instructions"), Statistics.NumPixelShaderInstructions);
    StatisticsObject->SetNumberField(TEXT("samplers"), Statistics.NumSamplers);
    StatisticsObject->SetNumberField(TEXT("vertex_texture_samples"), Statistics.NumVertexTextureSamples);
    StatisticsObject->SetNumberField(TEXT("pixel_texture_samples"), Statistics.NumPixelTextureSamples);
    StatisticsObject->SetNumberField(TEXT("virtual_texture_samples"), Statistics.NumVirtualTextureSamples);
    StatisticsObject->SetNumberField(TEXT("uv_scalars"), Statistics.NumUVScalars);
    StatisticsObject->SetNumberField(TEXT("interpolator_scalars"), Statistics.NumInterpolatorScalars);

    StatusObject->SetBoolField(TEXT("success"), CompileErrors.Num() == 0);
    AppendStringArray(StatusObject, TEXT("compile_errors"), CompileErrors);
    StatusObject->SetArrayField(TEXT("error_nodes"), ErrorNodes);
    StatusObject->SetNumberField(TEXT("shader_platform"), static_cast<int32>(GMaxRHIShaderPlatform));
    StatusObject->SetObjectField(TEXT("statistics"), StatisticsObject);
    return StatusObject;
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::ValidateMaterialGraph(UMaterial* Material) const
{
    TSharedPtr<FJsonObject> ValidationObject = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;
    TArray<TSharedPtr<FJsonValue>> NodeErrors;
    TArray<TSharedPtr<FJsonValue>> MaterialOutputs;

    if (!Material)
    {
        Errors.Add(MakeShared<FJsonValueObject>(MakeGraphIssue(TEXT("error"), TEXT("Invalid material"))));
        ValidationObject->SetBoolField(TEXT("valid"), false);
        ValidationObject->SetArrayField(TEXT("errors"), Errors);
        ValidationObject->SetArrayField(TEXT("warnings"), Warnings);
        ValidationObject->SetArrayField(TEXT("node_errors"), NodeErrors);
        ValidationObject->SetArrayField(TEXT("material_outputs"), MaterialOutputs);
        return ValidationObject;
    }

    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (!Expression)
        {
            continue;
        }

        const FString NodePath = Expression->GetPathName();
        const FString NodeGuid = GuidToStableString(Expression->GetMaterialExpressionId());

        for (int32 InputIndex = 0; InputIndex < Expression->CountInputs(); ++InputIndex)
        {
            FExpressionInput* Input = Expression->GetInput(InputIndex);
            if (Input && Expression->IsInputConnectionRequired(InputIndex) && !Input->Expression)
            {
                const FString InputName = Expression->GetInputName(InputIndex).ToString();
                TSharedPtr<FJsonObject> Issue = MakeGraphIssue(
                    TEXT("error"),
                    FString::Printf(TEXT("Required input '%s' is not connected"), *InputName),
                    NodePath,
                    NodeGuid);
                Issue->SetStringField(TEXT("input_name"), InputName);
                Errors.Add(MakeShared<FJsonValueObject>(Issue));
                NodeErrors.Add(MakeShared<FJsonValueObject>(Issue));
            }
        }

        if (UMaterialExpressionComponentMask* ComponentMask = Cast<UMaterialExpressionComponentMask>(Expression))
        {
            if (!ComponentMask->R && !ComponentMask->G && !ComponentMask->B && !ComponentMask->A)
            {
                TSharedPtr<FJsonObject> Issue = MakeGraphIssue(
                    TEXT("error"),
                    TEXT("ComponentMask has no selected channels"),
                    NodePath,
                    NodeGuid);
                Errors.Add(MakeShared<FJsonValueObject>(Issue));
                NodeErrors.Add(MakeShared<FJsonValueObject>(Issue));
            }
        }
    }

    static const EMaterialProperty PropertiesToCheck[] = {
        MP_BaseColor,
        MP_Metallic,
        MP_Specular,
        MP_Roughness,
        MP_EmissiveColor,
        MP_Opacity,
        MP_OpacityMask,
        MP_Normal,
        MP_WorldPositionOffset,
        MP_AmbientOcclusion,
        MP_Refraction,
        MP_PixelDepthOffset
    };

    bool bAnyOutputConnected = false;
    for (EMaterialProperty Property : PropertiesToCheck)
    {
        FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
        const bool bConnected = Input && Input->Expression;
        bAnyOutputConnected |= bConnected;

        TSharedPtr<FJsonObject> OutputObject = MakeShared<FJsonObject>();
        OutputObject->SetStringField(TEXT("property"), GetMaterialPropertyLabel(Property));
        OutputObject->SetBoolField(TEXT("connected"), bConnected);
        if (bConnected)
        {
            OutputObject->SetObjectField(TEXT("expression"), ExpressionToJson(Input->Expression));
        }
        MaterialOutputs.Add(MakeShared<FJsonValueObject>(OutputObject));
    }

    if (!bAnyOutputConnected)
    {
        Warnings.Add(MakeShared<FJsonValueObject>(MakeGraphIssue(TEXT("warning"), TEXT("No root material outputs are connected"))));
    }

    TSharedPtr<FJsonObject> CompileStatus = GetMaterialCompileStatus(Material);
    const TArray<TSharedPtr<FJsonValue>>* CompileErrors = nullptr;
    if (CompileStatus->TryGetArrayField(TEXT("compile_errors"), CompileErrors) && CompileErrors)
    {
        for (const TSharedPtr<FJsonValue>& CompileError : *CompileErrors)
        {
            Errors.Add(MakeShared<FJsonValueObject>(MakeGraphIssue(TEXT("error"), CompileError->AsString())));
        }
    }

    ValidationObject->SetBoolField(TEXT("valid"), Errors.Num() == 0);
    ValidationObject->SetArrayField(TEXT("errors"), Errors);
    ValidationObject->SetArrayField(TEXT("warnings"), Warnings);
    ValidationObject->SetArrayField(TEXT("node_errors"), NodeErrors);
    ValidationObject->SetArrayField(TEXT("material_outputs"), MaterialOutputs);
    ValidationObject->SetObjectField(TEXT("compile_status"), CompileStatus);
    return ValidationObject;
}

bool FUnrealMCPMaterialCommands::RollbackMaterialFromBackup(UMaterial* Material, UMaterial* BackupMaterial, bool bWasDirty, FString& OutErrorMessage) const
{
    if (!Material || !BackupMaterial)
    {
        OutErrorMessage = TEXT("Cannot rollback material because the backup is unavailable");
        return false;
    }

    Material->Modify();

    UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
    ClearMaterialComments(Material);

    TArray<UMaterialExpression*> BackupExpressions;
    TArray<UMaterialExpressionComment*> BackupComments;
    CollectMaterialExpressions(BackupMaterial, BackupExpressions, BackupComments);

    TArray<UMaterialExpression*> RestoredExpressions;
    TArray<UMaterialExpression*> RestoredComments;
    UMaterialExpression::CopyMaterialExpressions(BackupExpressions, BackupComments, Material, nullptr, RestoredExpressions, RestoredComments);

    for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
    {
        FExpressionInput* SourceInput = BackupMaterial->GetExpressionInputForProperty(static_cast<EMaterialProperty>(PropertyIndex));
        FExpressionInput* TargetInput = Material->GetExpressionInputForProperty(static_cast<EMaterialProperty>(PropertyIndex));
        if (!SourceInput || !TargetInput)
        {
            continue;
        }

        TargetInput->Expression = nullptr;
        TargetInput->OutputIndex = SourceInput->OutputIndex;
        TargetInput->InputName = SourceInput->InputName;
        TargetInput->Mask = SourceInput->Mask;
        TargetInput->MaskR = SourceInput->MaskR;
        TargetInput->MaskG = SourceInput->MaskG;
        TargetInput->MaskB = SourceInput->MaskB;
        TargetInput->MaskA = SourceInput->MaskA;

        if (SourceInput->Expression)
        {
            const int32 SourceExpressionIndex = BackupExpressions.IndexOfByKey(SourceInput->Expression);
            if (RestoredExpressions.IsValidIndex(SourceExpressionIndex))
            {
                TargetInput->Expression = RestoredExpressions[SourceExpressionIndex];
            }
        }
    }

    Material->BlendMode = BackupMaterial->BlendMode;
    Material->TwoSided = BackupMaterial->TwoSided;
    Material->TranslucencyLightingMode = BackupMaterial->TranslucencyLightingMode;
    Material->RefractionMethod = BackupMaterial->RefractionMethod;
    Material->SetShadingModel(BackupMaterial->GetShadingModels().GetFirstShadingModel());

    if (!bWasDirty && Material->GetPackage())
    {
        Material->GetPackage()->SetDirtyFlag(false);
    }

    return true;
}

bool FUnrealMCPMaterialCommands::ReloadAssetPackageFromDisk(UObject* Asset, bool bCloseEditors, bool bFailIfDirty, FString& OutErrorMessage) const
{
    if (!Asset)
    {
        OutErrorMessage = TEXT("Invalid asset");
        return false;
    }

    UPackage* Package = Asset->GetPackage();
    if (!Package)
    {
        OutErrorMessage = FString::Printf(TEXT("Asset has no package: %s"), *Asset->GetPathName());
        return false;
    }

    if (bFailIfDirty && Package->IsDirty())
    {
        OutErrorMessage = FString::Printf(TEXT("Refusing to reload dirty package: %s"), *Package->GetName());
        return false;
    }

    if (bCloseEditors)
    {
        if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
        {
            AssetEditorSubsystem->CloseAllEditorsForAsset(Asset);
        }
    }

    TArray<UPackage*> PackagesToReload;
    PackagesToReload.Add(Package);
    FText ErrorText;
    const bool bReloaded = UPackageTools::ReloadPackages(PackagesToReload, ErrorText, EReloadPackagesInteractionMode::AssumePositive);
    if (!bReloaded && !ErrorText.IsEmpty())
    {
        OutErrorMessage = ErrorText.ToString();
        return false;
    }

    return bReloaded;
}

bool FUnrealMCPMaterialCommands::RebuildExpressionGraph(
    UObject* Owner,
    UMaterial* Material,
    UMaterialFunction* MaterialFunction,
    const TSharedPtr<FJsonObject>& GraphSpec,
    TArray<TSharedPtr<FJsonValue>>& OutNodes,
    TArray<TSharedPtr<FJsonValue>>& OutComments,
    FString& OutErrorMessage) const
{
    OutNodes.Reset();
    OutComments.Reset();

    if (!Owner || (!Material && !MaterialFunction) || !GraphSpec.IsValid())
    {
        OutErrorMessage = TEXT("Invalid graph rebuild target or graph_spec");
        return false;
    }

    if (GraphSpec->HasField(TEXT("version")) && static_cast<int32>(GraphSpec->GetNumberField(TEXT("version"))) != 1)
    {
        OutErrorMessage = TEXT("Unsupported graph_spec version; expected version 1");
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* NodeArray = nullptr;
    GraphSpec->TryGetArrayField(TEXT("nodes"), NodeArray);
    const TArray<TSharedPtr<FJsonValue>>* RerouteArray = nullptr;
    GraphSpec->TryGetArrayField(TEXT("reroutes"), RerouteArray);
    const TArray<TSharedPtr<FJsonValue>>* CommentArray = nullptr;
    GraphSpec->TryGetArrayField(TEXT("comments"), CommentArray);
    const TArray<TSharedPtr<FJsonValue>>* ConnectionArray = nullptr;
    GraphSpec->TryGetArrayField(TEXT("connections"), ConnectionArray);
    const TArray<TSharedPtr<FJsonValue>>* GroupArray = nullptr;
    GraphSpec->TryGetArrayField(TEXT("groups"), GroupArray);

    TMap<FString, int32> GroupOrder;
    if (GroupArray)
    {
        for (int32 Index = 0; Index < GroupArray->Num(); ++Index)
        {
            FString GroupName;
            const TSharedPtr<FJsonValue>& GroupValue = (*GroupArray)[Index];
            if (GroupValue->Type == EJson::String)
            {
                GroupName = GroupValue->AsString();
            }
            else if (GroupValue->Type == EJson::Object)
            {
                TSharedPtr<FJsonObject> GroupObject = GroupValue->AsObject();
                if (!GroupObject->TryGetStringField(TEXT("id"), GroupName))
                {
                    GroupObject->TryGetStringField(TEXT("name"), GroupName);
                }
            }

            if (!GroupName.IsEmpty())
            {
                GroupOrder.Add(GroupName, Index);
            }
        }
    }

    auto GetGroupIndex = [&GroupOrder](const FString& GroupName) -> int32
    {
        if (GroupName.IsEmpty())
        {
            return 0;
        }
        if (const int32* ExistingIndex = GroupOrder.Find(GroupName))
        {
            return *ExistingIndex;
        }
        const int32 NewIndex = GroupOrder.Num();
        GroupOrder.Add(GroupName, NewIndex);
        return NewIndex;
    };

    auto GetDefaultPosition = [&GetGroupIndex](const FString& GroupName, int32 NodeIndex) -> FVector2D
    {
        const int32 GroupIndex = GetGroupIndex(GroupName);
        return FVector2D(-1200.0f + GroupIndex * 520.0f, -360.0f + NodeIndex * 180.0f);
    };

    TMap<FString, UMaterialExpression*> CreatedByLocalId;
    TSet<FString> SeenIds;

    auto CreateExpressionFromSpec = [&](const TSharedPtr<FJsonObject>& NodeObject, int32 NodeIndex, const FString& DefaultType) -> UMaterialExpression*
    {
        if (!NodeObject.IsValid())
        {
            OutErrorMessage = TEXT("Graph node entry must be an object");
            return nullptr;
        }

        FString LocalId;
        if (!NodeObject->TryGetStringField(TEXT("id"), LocalId) || LocalId.IsEmpty())
        {
            OutErrorMessage = TEXT("Every graph node must have a non-empty id");
            return nullptr;
        }

        if (SeenIds.Contains(LocalId))
        {
            OutErrorMessage = FString::Printf(TEXT("Duplicate graph node id: %s"), *LocalId);
            return nullptr;
        }
        SeenIds.Add(LocalId);

        FString ExpressionType = DefaultType;
        NodeObject->TryGetStringField(TEXT("type"), ExpressionType);
        if (ExpressionType.IsEmpty())
        {
            OutErrorMessage = FString::Printf(TEXT("Graph node %s is missing type"), *LocalId);
            return nullptr;
        }

        UClass* ExpressionClass = ResolveExpressionClass(ExpressionType);
        if (!ExpressionClass)
        {
            OutErrorMessage = FString::Printf(TEXT("Unknown material expression type for node %s: %s"), *LocalId, *ExpressionType);
            return nullptr;
        }

        FString GroupName;
        NodeObject->TryGetStringField(TEXT("group"), GroupName);
        const FVector2D Position = GetPositionFromJsonObject(NodeObject, GetDefaultPosition(GroupName, NodeIndex));

        UObject* SelectedAsset = nullptr;
        FString SelectedAssetPath;
        if (NodeObject->TryGetStringField(TEXT("selected_asset"), SelectedAssetPath) ||
            NodeObject->TryGetStringField(TEXT("function_path"), SelectedAssetPath))
        {
            FString NormalizedAssetPath;
            FString ObjectPath;
            SelectedAsset = LoadAssetByPath(SelectedAssetPath, NormalizedAssetPath, ObjectPath);
            if (!SelectedAsset)
            {
                OutErrorMessage = FString::Printf(TEXT("Failed to load selected asset for node %s: %s"), *LocalId, *SelectedAssetPath);
                return nullptr;
            }
        }

        UMaterialExpression* Expression = UMaterialEditingLibrary::CreateMaterialExpressionEx(
            Material,
            MaterialFunction,
            ExpressionClass,
            SelectedAsset,
            static_cast<int32>(Position.X),
            static_cast<int32>(Position.Y),
            false);

        if (!Expression)
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to create material expression for node %s"), *LocalId);
            return nullptr;
        }

        Expression->UpdateMaterialExpressionGuid(true, false);

        if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
        {
            if (SelectedAsset)
            {
                if (UMaterialFunctionInterface* FunctionAsset = Cast<UMaterialFunctionInterface>(SelectedAsset))
                {
                    FunctionCall->SetMaterialFunction(FunctionAsset);
                    FunctionCall->UpdateFromFunctionResource();
                }
                else
                {
                    OutErrorMessage = FString::Printf(TEXT("Selected asset for node %s is not a material function: %s"), *LocalId, *SelectedAssetPath);
                    return nullptr;
                }
            }
        }

        FString Label;
        if (NodeObject->TryGetStringField(TEXT("label"), Label) ||
            NodeObject->TryGetStringField(TEXT("expression_name"), Label) ||
            NodeObject->TryGetStringField(TEXT("name"), Label))
        {
            TrySetExpressionLabel(Expression, Label);
        }

        if (NodeObject->HasTypedField<EJson::Object>(TEXT("properties")))
        {
            TSharedPtr<FJsonObject> PropertiesObject = NodeObject->GetObjectField(TEXT("properties"));
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObject->Values)
            {
                if (!SetMaterialPropertyValue(Expression, Pair.Key, Pair.Value, OutErrorMessage))
                {
                    return nullptr;
                }
            }
        }

        CreatedByLocalId.Add(LocalId, Expression);

        TSharedPtr<FJsonObject> ExpressionObject = ExpressionToJson(Expression);
        ExpressionObject->SetStringField(TEXT("local_id"), LocalId);
        if (!GroupName.IsEmpty())
        {
            ExpressionObject->SetStringField(TEXT("group"), GroupName);
        }
        OutNodes.Add(MakeShared<FJsonValueObject>(ExpressionObject));
        return Expression;
    };

    if (Material)
    {
        UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
        ClearMaterialComments(Material);
    }
    else
    {
        UMaterialEditingLibrary::DeleteAllMaterialExpressionsInFunction(MaterialFunction);
        ClearFunctionComments(MaterialFunction);
    }

    int32 NodeIndex = 0;
    if (NodeArray)
    {
        for (const TSharedPtr<FJsonValue>& NodeValue : *NodeArray)
        {
            if (!CreateExpressionFromSpec(NodeValue->AsObject(), NodeIndex++, TEXT("")))
            {
                return false;
            }
        }
    }

    if (RerouteArray)
    {
        for (const TSharedPtr<FJsonValue>& RerouteValue : *RerouteArray)
        {
            if (!CreateExpressionFromSpec(RerouteValue->AsObject(), NodeIndex++, TEXT("MaterialExpressionReroute")))
            {
                return false;
            }
        }
    }

    if (CommentArray)
    {
        for (const TSharedPtr<FJsonValue>& CommentValue : *CommentArray)
        {
            if (!CommentValue.IsValid() || CommentValue->Type != EJson::Object)
            {
                OutErrorMessage = TEXT("Comment entries must be objects");
                return false;
            }

            TSharedPtr<FJsonObject> CommentObject = CommentValue->AsObject();
            FString CommentId;
            CommentObject->TryGetStringField(TEXT("id"), CommentId);
            FString CommentText;
            if (!CommentObject->TryGetStringField(TEXT("text"), CommentText))
            {
                CommentObject->TryGetStringField(TEXT("label"), CommentText);
            }
            FString GroupName;
            CommentObject->TryGetStringField(TEXT("group"), GroupName);

            FVector2D Position = GetPositionFromJsonObject(CommentObject, FVector2D(-1280.0f + GetGroupIndex(GroupName) * 520.0f, -460.0f));
            FVector2D Size(460.0f, 720.0f);
            if (CommentObject->HasTypedField<EJson::Array>(TEXT("bounds")))
            {
                const TArray<TSharedPtr<FJsonValue>>* BoundsArray = nullptr;
                if (CommentObject->TryGetArrayField(TEXT("bounds"), BoundsArray) && BoundsArray && BoundsArray->Num() >= 4)
                {
                    Position = FVector2D(static_cast<float>((*BoundsArray)[0]->AsNumber()), static_cast<float>((*BoundsArray)[1]->AsNumber()));
                    Size = FVector2D(static_cast<float>((*BoundsArray)[2]->AsNumber()), static_cast<float>((*BoundsArray)[3]->AsNumber()));
                }
            }
            else if (CommentObject->HasTypedField<EJson::Array>(TEXT("size")))
            {
                const TArray<TSharedPtr<FJsonValue>>* SizeArray = nullptr;
                if (CommentObject->TryGetArrayField(TEXT("size"), SizeArray) && SizeArray && SizeArray->Num() >= 2)
                {
                    Size = FVector2D(static_cast<float>((*SizeArray)[0]->AsNumber()), static_cast<float>((*SizeArray)[1]->AsNumber()));
                }
            }

            UMaterialExpressionComment* Comment = CreateMaterialComment(Owner, Position, Size, CommentText);
            if (!Comment)
            {
                OutErrorMessage = TEXT("Failed to create comment box");
                return false;
            }

            TSharedPtr<FJsonObject> CommentJson = ExpressionToJson(Comment);
            if (!CommentId.IsEmpty())
            {
                CommentJson->SetStringField(TEXT("local_id"), CommentId);
            }
            CommentJson->SetStringField(TEXT("text"), CommentText);
            AddVector2DJson(CommentJson, TEXT("size"), Size);
            OutComments.Add(MakeShared<FJsonValueObject>(CommentJson));
        }
    }

    auto ResolveEndpointExpression = [&](const TSharedPtr<FJsonObject>& Endpoint, const FString& Direction) -> UMaterialExpression*
    {
        if (!Endpoint.IsValid())
        {
            OutErrorMessage = FString::Printf(TEXT("Connection %s endpoint must be an object"), *Direction);
            return nullptr;
        }

        FString LocalNodeId;
        if (Endpoint->TryGetStringField(TEXT("node"), LocalNodeId) ||
            Endpoint->TryGetStringField(TEXT("id"), LocalNodeId))
        {
            if (UMaterialExpression** FoundExpression = CreatedByLocalId.Find(LocalNodeId))
            {
                return *FoundExpression;
            }
            OutErrorMessage = FString::Printf(TEXT("Connection %s endpoint references unknown node id: %s"), *Direction, *LocalNodeId);
            return nullptr;
        }

        FString StableReference;
        if (TryReadExpressionReferenceField(Endpoint, TEXT("ref"), StableReference) ||
            TryReadExpressionReferenceField(Endpoint, TEXT("expression"), StableReference))
        {
            FString ResolveError;
            UMaterialExpression* Expression = Material
                ? FindExpressionByReference(Material, StableReference, &ResolveError)
                : FindExpressionByReference(MaterialFunction, StableReference, &ResolveError);
            if (!Expression)
            {
                OutErrorMessage = !ResolveError.IsEmpty()
                    ? ResolveError
                    : FString::Printf(TEXT("Connection %s endpoint expression not found: %s"), *Direction, *StableReference);
            }
            return Expression;
        }

        OutErrorMessage = FString::Printf(TEXT("Connection %s endpoint must specify node or ref"), *Direction);
        return nullptr;
    };

    if (ConnectionArray)
    {
        for (const TSharedPtr<FJsonValue>& ConnectionValue : *ConnectionArray)
        {
            if (!ConnectionValue.IsValid() || ConnectionValue->Type != EJson::Object)
            {
                OutErrorMessage = TEXT("Connection entries must be objects");
                return false;
            }

            TSharedPtr<FJsonObject> ConnectionObject = ConnectionValue->AsObject();
            if (!ConnectionObject->HasTypedField<EJson::Object>(TEXT("from")) || !ConnectionObject->HasTypedField<EJson::Object>(TEXT("to")))
            {
                OutErrorMessage = TEXT("Connection entries require object fields 'from' and 'to'");
                return false;
            }

            TSharedPtr<FJsonObject> FromObject = ConnectionObject->GetObjectField(TEXT("from"));
            TSharedPtr<FJsonObject> ToObject = ConnectionObject->GetObjectField(TEXT("to"));

            UMaterialExpression* FromExpression = ResolveEndpointExpression(FromObject, TEXT("from"));
            if (!FromExpression)
            {
                return false;
            }

            FString FromOutputName;
            if (!FromObject->TryGetStringField(TEXT("output"), FromOutputName))
            {
                FromObject->TryGetStringField(TEXT("output_name"), FromOutputName);
            }

            FString MaterialPropertyName;
            if (ToObject->TryGetStringField(TEXT("material_property"), MaterialPropertyName) ||
                ToObject->TryGetStringField(TEXT("property"), MaterialPropertyName))
            {
                if (!Material)
                {
                    OutErrorMessage = TEXT("material_property connections are only valid when rebuilding a material graph");
                    return false;
                }

                EMaterialProperty MaterialProperty = MP_BaseColor;
                if (!TryParseMaterialProperty(MaterialPropertyName, MaterialProperty))
                {
                    OutErrorMessage = FString::Printf(TEXT("Unsupported material property: %s"), *MaterialPropertyName);
                    return false;
                }

                if (!UMaterialEditingLibrary::ConnectMaterialProperty(FromExpression, FromOutputName, MaterialProperty))
                {
                    OutErrorMessage = FString::Printf(TEXT("Failed to connect node to material property: %s"), *MaterialPropertyName);
                    return false;
                }
                continue;
            }

            UMaterialExpression* ToExpression = ResolveEndpointExpression(ToObject, TEXT("to"));
            if (!ToExpression)
            {
                return false;
            }

            FString ToInputName;
            if (!ToObject->TryGetStringField(TEXT("input"), ToInputName))
            {
                ToObject->TryGetStringField(TEXT("input_name"), ToInputName);
            }

            if (!UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpression, FromOutputName, ToExpression, ToInputName))
            {
                OutErrorMessage = FString::Printf(TEXT("Failed to connect expression %s to %s"), *FromExpression->GetName(), *ToExpression->GetName());
                return false;
            }
        }
    }

    if (Material && GraphSpec->HasTypedField<EJson::Object>(TEXT("layout")))
    {
        TSharedPtr<FJsonObject> LayoutObject = GraphSpec->GetObjectField(TEXT("layout"));
        FString LayoutMode;
        if (LayoutObject->TryGetStringField(TEXT("mode"), LayoutMode) && NormalizeEnumToken(LayoutMode) == TEXT("ueauto"))
        {
            UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
        }
    }

    return true;
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleCreateMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath) &&
        !Params->TryGetStringField(TEXT("asset_path"), MaterialPath))
    {
        FString PackagePath;
        FString AssetName;
        if (!Params->TryGetStringField(TEXT("package_path"), PackagePath) ||
            !Params->TryGetStringField(TEXT("asset_name"), AssetName))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' or ('package_path' and 'asset_name') parameter"));
        }

        MaterialPath = PackagePath / AssetName;
    }

    MaterialPath = NormalizeAssetPath(MaterialPath);
    const FString PackagePath = FPackageName::GetLongPackagePath(MaterialPath);
    const FString AssetName = FPackageName::GetLongPackageAssetName(MaterialPath);

    if (PackagePath.IsEmpty() || AssetName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid material path: %s"), *MaterialPath));
    }

    if (UEditorAssetLibrary::DoesAssetExist(MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material already exists: %s"), *MaterialPath));
    }

    UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
    UObject* CreatedObject = FAssetToolsModule::GetModule().Get().CreateAsset(
        AssetName,
        PackagePath,
        UMaterial::StaticClass(),
        MaterialFactory,
        TEXT("UnrealMCP"));

    UMaterial* Material = Cast<UMaterial>(CreatedObject);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create material: %s"), *MaterialPath));
    }

    Material->Modify();
    FAssetRegistryModule::AssetCreated(Material);

    FString SaveErrorMessage;
    if (!SaveMaterialAsset(Material, MaterialPath, SaveErrorMessage))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), MaterialPath);
    ResponseObject->SetStringField(TEXT("object_path"), MakeObjectPath(MaterialPath));
    ResponseObject->SetStringField(TEXT("name"), AssetName);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleSetMaterialProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    FString AssetPath;
    FString ObjectPath;
    UMaterial* Material = LoadMaterialByPath(MaterialPath, AssetPath, ObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    Material->Modify();

    if (Params->HasField(TEXT("blend_mode")))
    {
        EBlendMode BlendMode = BLEND_Opaque;
        if (!TryParseBlendModeValue(Params->Values.FindRef(TEXT("blend_mode")), BlendMode))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid 'blend_mode' value"));
        }

        Material->BlendMode = BlendMode;
    }

    if (Params->HasField(TEXT("two_sided")))
    {
        Material->TwoSided = Params->GetBoolField(TEXT("two_sided"));
    }

    if (Params->HasField(TEXT("translucency_lighting_mode")))
    {
        ETranslucencyLightingMode LightingMode = TLM_SurfacePerPixelLighting;
        if (!TryParseTranslucencyLightingModeValue(Params->Values.FindRef(TEXT("translucency_lighting_mode")), LightingMode))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid 'translucency_lighting_mode' value"));
        }

        Material->TranslucencyLightingMode = LightingMode;
    }

    if (Params->HasField(TEXT("refraction_method")))
    {
        ERefractionMode RefractionMode = RM_IndexOfRefraction;
        if (!TryParseRefractionModeValue(Params->Values.FindRef(TEXT("refraction_method")), RefractionMode))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid 'refraction_method' value"));
        }

        Material->RefractionMethod = RefractionMode;
    }

    if (Params->HasField(TEXT("shading_model")))
    {
        FString ErrorMessage;
        if (!TrySetMaterialShadingModel(Material, Params->GetStringField(TEXT("shading_model")), ErrorMessage))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
        }
    }

    if (Params->HasTypedField<EJson::Object>(TEXT("properties")))
    {
        TSharedPtr<FJsonObject> PropertiesObject = Params->GetObjectField(TEXT("properties"));
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObject->Values)
        {
            const FString& PropertyName = Pair.Key;
            const TSharedPtr<FJsonValue>& PropertyValue = Pair.Value;

            if (PropertyName == TEXT("blend_mode"))
            {
                EBlendMode BlendMode = BLEND_Opaque;
                if (!TryParseBlendModeValue(PropertyValue, BlendMode))
                {
                    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid 'blend_mode' value"));
                }
                Material->BlendMode = BlendMode;
                continue;
            }

            if (PropertyName == TEXT("translucency_lighting_mode"))
            {
                ETranslucencyLightingMode LightingMode = TLM_SurfacePerPixelLighting;
                if (!TryParseTranslucencyLightingModeValue(PropertyValue, LightingMode))
                {
                    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid 'translucency_lighting_mode' value"));
                }
                Material->TranslucencyLightingMode = LightingMode;
                continue;
            }

            if (PropertyName == TEXT("refraction_method"))
            {
                ERefractionMode RefractionMode = RM_IndexOfRefraction;
                if (!TryParseRefractionModeValue(PropertyValue, RefractionMode))
                {
                    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid 'refraction_method' value"));
                }
                Material->RefractionMethod = RefractionMode;
                continue;
            }

            if (PropertyName == TEXT("shading_model"))
            {
                FString ErrorMessage;
                if (!TrySetMaterialShadingModel(Material, PropertyValue->AsString(), ErrorMessage))
                {
                    return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
                }
                continue;
            }

            FString ErrorMessage;
            if (!SetMaterialPropertyValue(Material, PropertyName, PropertyValue, ErrorMessage))
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
            }
        }
    }

    RecompileMaterialAsset(Material);

    FString SaveErrorMessage;
    if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetStringField(TEXT("object_path"), ObjectPath);
    ResponseObject->SetStringField(TEXT("blend_mode"), GetBlendModeLabel(Material->BlendMode));
    ResponseObject->SetBoolField(TEXT("two_sided"), Material->TwoSided != 0);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleAddMaterialExpression(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    FString ExpressionType;
    if (!Params->TryGetStringField(TEXT("expression_type"), ExpressionType))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'expression_type' parameter"));
    }

    FString AssetPath;
    FString ObjectPath;
    UMaterial* Material = LoadMaterialByPath(MaterialPath, AssetPath, ObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    UClass* ExpressionClass = ResolveExpressionClass(ExpressionType);
    if (!ExpressionClass)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown material expression type: %s"), *ExpressionType));
    }

    FVector2D Position = FUnrealMCPCommonUtils::GetVector2DFromJson(Params, TEXT("position"));

    UObject* SelectedAsset = nullptr;
    if (Params->HasField(TEXT("selected_asset")))
    {
        const FString SelectedAssetPath = Params->GetStringField(TEXT("selected_asset"));
        SelectedAsset = LoadObject<UObject>(nullptr, *SelectedAssetPath);
        if (!SelectedAsset)
        {
            SelectedAsset = UEditorAssetLibrary::LoadAsset(SelectedAssetPath);
        }
    }
    else if (Params->HasField(TEXT("function_path")))
    {
        const FString FunctionPath = Params->GetStringField(TEXT("function_path"));
        FString NormalizedAssetPath;
        FString FunctionObjectPath;
        SelectedAsset = LoadAssetByPath(FunctionPath, NormalizedAssetPath, FunctionObjectPath);
    }

    Material->Modify();

    UMaterialExpression* Expression = SelectedAsset
        ? UMaterialEditingLibrary::CreateMaterialExpressionEx(Material, nullptr, ExpressionClass, SelectedAsset, Position.X, Position.Y, true)
        : UMaterialEditingLibrary::CreateMaterialExpression(Material, ExpressionClass, Position.X, Position.Y);

    if (!Expression)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create material expression"));
    }

    Expression->UpdateMaterialExpressionGuid(true, false);

    if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
    {
        if (SelectedAsset)
        {
            if (UMaterialFunctionInterface* FunctionAsset = Cast<UMaterialFunctionInterface>(SelectedAsset))
            {
                FunctionCall->SetMaterialFunction(FunctionAsset);
                FunctionCall->UpdateFromFunctionResource();
            }
        }
    }

    if (Params->HasField(TEXT("expression_name")))
    {
        TrySetExpressionLabel(Expression, Params->GetStringField(TEXT("expression_name")));
    }

    if (Params->HasTypedField<EJson::Object>(TEXT("properties")))
    {
        TSharedPtr<FJsonObject> PropertiesObject = Params->GetObjectField(TEXT("properties"));
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObject->Values)
        {
            FString ErrorMessage;
            if (!SetMaterialPropertyValue(Expression, Pair.Key, Pair.Value, ErrorMessage))
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
            }
        }
    }

    const bool bDeferCompile = Params->HasField(TEXT("defer_compile")) ? Params->GetBoolField(TEXT("defer_compile")) : true;
    const bool bDeferSave = Params->HasField(TEXT("defer_save")) ? Params->GetBoolField(TEXT("defer_save")) : true;

    if (!bDeferCompile)
    {
        RecompileMaterialAsset(Material);
    }

    if (!bDeferSave)
    {
        FString SaveErrorMessage;
        if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
        }
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetObjectField(TEXT("expression"), ExpressionToJson(Expression));
    ResponseObject->SetBoolField(TEXT("defer_compile"), bDeferCompile);
    ResponseObject->SetBoolField(TEXT("defer_save"), bDeferSave);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleSetMaterialExpressionProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    FString ExpressionReference;
    if (!TryReadExpressionReferenceField(Params, TEXT("expression_ref"), ExpressionReference) &&
        !TryReadExpressionReferenceField(Params, TEXT("ref"), ExpressionReference) &&
        !Params->TryGetStringField(TEXT("expression"), ExpressionReference) &&
        !Params->TryGetStringField(TEXT("expression_name"), ExpressionReference) &&
        !Params->TryGetStringField(TEXT("expression_id"), ExpressionReference))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'expression' parameter"));
    }

    FString AssetPath;
    FString ObjectPath;
    UMaterial* Material = LoadMaterialByPath(MaterialPath, AssetPath, ObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    FString ResolveErrorMessage;
    UMaterialExpression* Expression = FindExpressionByReference(Material, ExpressionReference, &ResolveErrorMessage);
    if (!Expression)
    {
        if (!ResolveErrorMessage.IsEmpty())
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ResolveErrorMessage);
        }
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material expression not found: %s"), *ExpressionReference));
    }

    Material->Modify();

    if (Params->HasTypedField<EJson::Object>(TEXT("properties")))
    {
        TSharedPtr<FJsonObject> PropertiesObject = Params->GetObjectField(TEXT("properties"));
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PropertiesObject->Values)
        {
            FString ErrorMessage;
            if (!SetMaterialPropertyValue(Expression, Pair.Key, Pair.Value, ErrorMessage))
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
            }
        }
    }
    else
    {
        FString PropertyName;
        if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
        }

        if (!Params->HasField(TEXT("property_value")))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
        }

        FString ErrorMessage;
        if (!SetMaterialPropertyValue(Expression, PropertyName, Params->Values.FindRef(TEXT("property_value")), ErrorMessage))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
        }
    }

    const bool bDeferCompile = Params->HasField(TEXT("defer_compile")) ? Params->GetBoolField(TEXT("defer_compile")) : true;
    const bool bDeferSave = Params->HasField(TEXT("defer_save")) ? Params->GetBoolField(TEXT("defer_save")) : true;

    if (!bDeferCompile)
    {
        RecompileMaterialAsset(Material);
    }

    if (!bDeferSave)
    {
        FString SaveErrorMessage;
        if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
        }
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetObjectField(TEXT("expression"), ExpressionToJson(Expression));
    ResponseObject->SetBoolField(TEXT("defer_compile"), bDeferCompile);
    ResponseObject->SetBoolField(TEXT("defer_save"), bDeferSave);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleConnectMaterialExpressions(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    FString FromExpressionReference;
    if (!TryReadExpressionReferenceField(Params, TEXT("source_ref"), FromExpressionReference) &&
        !TryReadExpressionReferenceField(Params, TEXT("from_ref"), FromExpressionReference) &&
        !Params->TryGetStringField(TEXT("from_expression"), FromExpressionReference))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'from_expression' or 'source_ref' parameter"));
    }

    FString ToExpressionReference;
    if (!TryReadExpressionReferenceField(Params, TEXT("target_ref"), ToExpressionReference) &&
        !TryReadExpressionReferenceField(Params, TEXT("to_ref"), ToExpressionReference) &&
        !Params->TryGetStringField(TEXT("to_expression"), ToExpressionReference))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'to_expression' or 'target_ref' parameter"));
    }

    FString AssetPath;
    FString ObjectPath;
    UMaterial* Material = LoadMaterialByPath(MaterialPath, AssetPath, ObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    FString ResolveErrorMessage;
    UMaterialExpression* FromExpression = FindExpressionByReference(Material, FromExpressionReference, &ResolveErrorMessage);
    if (!FromExpression)
    {
        if (!ResolveErrorMessage.IsEmpty())
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ResolveErrorMessage);
        }
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Source material expression not found: %s"), *FromExpressionReference));
    }

    ResolveErrorMessage.Empty();
    UMaterialExpression* ToExpression = FindExpressionByReference(Material, ToExpressionReference, &ResolveErrorMessage);
    if (!ToExpression)
    {
        if (!ResolveErrorMessage.IsEmpty())
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ResolveErrorMessage);
        }
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Target material expression not found: %s"), *ToExpressionReference));
    }

    const FString FromOutputName = Params->HasField(TEXT("from_output_name")) ? Params->GetStringField(TEXT("from_output_name")) : TEXT("");
    const FString ToInputName = Params->HasField(TEXT("to_input_name")) ? Params->GetStringField(TEXT("to_input_name")) : TEXT("");

    Material->Modify();

    if (!UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpression, FromOutputName, ToExpression, ToInputName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to connect material expressions"));
    }

    const bool bDeferCompile = Params->HasField(TEXT("defer_compile")) ? Params->GetBoolField(TEXT("defer_compile")) : true;
    const bool bDeferSave = Params->HasField(TEXT("defer_save")) ? Params->GetBoolField(TEXT("defer_save")) : true;

    if (!bDeferCompile)
    {
        RecompileMaterialAsset(Material);
    }

    if (!bDeferSave)
    {
        FString SaveErrorMessage;
        if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
        }
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetObjectField(TEXT("from_expression"), ExpressionToJson(FromExpression));
    ResponseObject->SetObjectField(TEXT("to_expression"), ExpressionToJson(ToExpression));
    ResponseObject->SetBoolField(TEXT("defer_compile"), bDeferCompile);
    ResponseObject->SetBoolField(TEXT("defer_save"), bDeferSave);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleConnectMaterialProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    FString ExpressionReference;
    if (!TryReadExpressionReferenceField(Params, TEXT("expression_ref"), ExpressionReference) &&
        !TryReadExpressionReferenceField(Params, TEXT("source_ref"), ExpressionReference) &&
        !Params->TryGetStringField(TEXT("expression"), ExpressionReference))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'expression' or 'expression_ref' parameter"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName) &&
        !Params->TryGetStringField(TEXT("material_property"), PropertyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    FString AssetPath;
    FString ObjectPath;
    UMaterial* Material = LoadMaterialByPath(MaterialPath, AssetPath, ObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    FString ResolveErrorMessage;
    UMaterialExpression* Expression = FindExpressionByReference(Material, ExpressionReference, &ResolveErrorMessage);
    if (!Expression)
    {
        if (!ResolveErrorMessage.IsEmpty())
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ResolveErrorMessage);
        }
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material expression not found: %s"), *ExpressionReference));
    }

    EMaterialProperty MaterialProperty = MP_BaseColor;
    if (!TryParseMaterialProperty(PropertyName, MaterialProperty))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unsupported material property: %s"), *PropertyName));
    }

    const FString FromOutputName = Params->HasField(TEXT("from_output_name")) ? Params->GetStringField(TEXT("from_output_name")) : TEXT("");

    Material->Modify();

    if (!UMaterialEditingLibrary::ConnectMaterialProperty(Expression, FromOutputName, MaterialProperty))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to connect material expression to material property"));
    }

    const bool bDeferCompile = Params->HasField(TEXT("defer_compile")) ? Params->GetBoolField(TEXT("defer_compile")) : true;
    const bool bDeferSave = Params->HasField(TEXT("defer_save")) ? Params->GetBoolField(TEXT("defer_save")) : true;

    if (!bDeferCompile)
    {
        RecompileMaterialAsset(Material);
    }

    if (!bDeferSave)
    {
        FString SaveErrorMessage;
        if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
        }
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetStringField(TEXT("property_name"), PropertyName);
    ResponseObject->SetObjectField(TEXT("expression"), ExpressionToJson(Expression));
    ResponseObject->SetBoolField(TEXT("defer_compile"), bDeferCompile);
    ResponseObject->SetBoolField(TEXT("defer_save"), bDeferSave);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleRecompileMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    FString AssetPath;
    FString ObjectPath;
    UMaterial* Material = LoadMaterialByPath(MaterialPath, AssetPath, ObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    RecompileMaterialAsset(Material);

    FString SaveErrorMessage;
    if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetStringField(TEXT("object_path"), ObjectPath);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleRebuildMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    if (!Params->HasTypedField<EJson::Object>(TEXT("graph_spec")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'graph_spec' object parameter"));
    }

    TSharedPtr<FJsonObject> GraphSpec = Params->GetObjectField(TEXT("graph_spec"));
    TSharedPtr<FJsonObject> Options = GraphSpec->HasTypedField<EJson::Object>(TEXT("options"))
        ? GraphSpec->GetObjectField(TEXT("options"))
        : MakeShared<FJsonObject>();

    FString AssetPath;
    FString ObjectPath;
    UMaterial* Material = LoadMaterialByPath(MaterialPath, AssetPath, ObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    const bool bCloseEditorBeforeRebuild = GetBoolFieldDefault(Options, TEXT("close_editor_before_rebuild"), false);
    if (bCloseEditorBeforeRebuild)
    {
        if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
        {
            AssetEditorSubsystem->CloseAllEditorsForAsset(Material);
        }
    }

    UPackage* Package = Material->GetPackage();
    const bool bWasDirty = Package && Package->IsDirty();
    UMaterial* BackupMaterial = DuplicateObject<UMaterial>(Material, GetTransientPackage());

    TArray<TSharedPtr<FJsonValue>> CreatedNodes;
    TArray<TSharedPtr<FJsonValue>> CreatedComments;
    FString ErrorMessage;

    auto FailWithRollback = [&](const FString& FailureMessage) -> TSharedPtr<FJsonObject>
    {
        FString RollbackError;
        const bool bRollbackSucceeded = RollbackMaterialFromBackup(Material, BackupMaterial, bWasDirty, RollbackError);
        FString FullMessage = FailureMessage;
        if (!bRollbackSucceeded)
        {
            FullMessage += FString::Printf(TEXT(" Rollback failed: %s"), *RollbackError);
        }
        return FUnrealMCPCommonUtils::CreateErrorResponse(FullMessage);
    };

    Material->Modify();

    if (GraphSpec->HasTypedField<EJson::Object>(TEXT("material_properties")))
    {
        if (!ApplyMaterialProperties(Material, GraphSpec->GetObjectField(TEXT("material_properties")), ErrorMessage))
        {
            return FailWithRollback(ErrorMessage);
        }
    }

    if (!RebuildExpressionGraph(Material, Material, nullptr, GraphSpec, CreatedNodes, CreatedComments, ErrorMessage))
    {
        return FailWithRollback(ErrorMessage);
    }

    const bool bCompile = GetBoolFieldDefault(Options, TEXT("compile"), true);
    const bool bSave = GetBoolFieldDefault(Options, TEXT("save"), true);
    const bool bValidateBeforeSave = GetBoolFieldDefault(Options, TEXT("validate_before_save"), true);

    if (bCompile)
    {
        RecompileMaterialAsset(Material);
    }

    TSharedPtr<FJsonObject> ValidationObject = ValidateMaterialGraph(Material);
    if (bValidateBeforeSave && !ValidationObject->GetBoolField(TEXT("valid")))
    {
        return FailWithRollback(TEXT("Material graph validation failed; asset was not saved"));
    }

    if (bSave)
    {
        FString SaveErrorMessage;
        if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
        {
            return FailWithRollback(SaveErrorMessage);
        }
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetStringField(TEXT("object_path"), ObjectPath);
    ResponseObject->SetArrayField(TEXT("nodes"), CreatedNodes);
    ResponseObject->SetArrayField(TEXT("comments"), CreatedComments);
    ResponseObject->SetObjectField(TEXT("validation"), ValidationObject);
    ResponseObject->SetObjectField(TEXT("compile_status"), GetMaterialCompileStatus(Material));
    ResponseObject->SetBoolField(TEXT("saved"), bSave);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleGetMaterialCompileStatus(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    FString AssetPath;
    FString ObjectPath;
    UMaterial* Material = LoadMaterialByPath(MaterialPath, AssetPath, ObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    TSharedPtr<FJsonObject> ResponseObject = GetMaterialCompileStatus(Material);
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetStringField(TEXT("object_path"), ObjectPath);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleValidateMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    FString AssetPath;
    FString ObjectPath;
    UMaterial* Material = LoadMaterialByPath(MaterialPath, AssetPath, ObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    TSharedPtr<FJsonObject> ResponseObject = ValidateMaterialGraph(Material);
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetStringField(TEXT("object_path"), ObjectPath);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleReloadAssetFromDisk(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) &&
        !Params->TryGetStringField(TEXT("material_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    FString NormalizedAssetPath;
    FString ObjectPath;
    UObject* Asset = LoadAssetByPath(AssetPath, NormalizedAssetPath, ObjectPath);
    if (!Asset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
    }

    FString ErrorMessage;
    const bool bCloseEditors = Params->HasField(TEXT("close_editors")) ? Params->GetBoolField(TEXT("close_editors")) : false;
    const bool bFailIfDirty = Params->HasField(TEXT("fail_if_dirty")) ? Params->GetBoolField(TEXT("fail_if_dirty")) : true;
    if (!ReloadAssetPackageFromDisk(Asset, bCloseEditors, bFailIfDirty, ErrorMessage))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("asset_path"), NormalizedAssetPath);
    ResponseObject->SetStringField(TEXT("object_path"), ObjectPath);
    ResponseObject->SetBoolField(TEXT("reloaded"), true);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleCloseAssetEditor(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) &&
        !Params->TryGetStringField(TEXT("material_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    FString NormalizedAssetPath;
    FString ObjectPath;
    UObject* Asset = LoadAssetByPath(AssetPath, NormalizedAssetPath, ObjectPath);
    if (!Asset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
    }

    int32 ClosedEditors = 0;
    if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
    {
        ClosedEditors = AssetEditorSubsystem->CloseAllEditorsForAsset(Asset);
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("asset_path"), NormalizedAssetPath);
    ResponseObject->SetStringField(TEXT("object_path"), ObjectPath);
    ResponseObject->SetNumberField(TEXT("closed_editors"), ClosedEditors);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleIsAssetLoadedDirty(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) &&
        !Params->TryGetStringField(TEXT("material_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    const FString NormalizedAssetPath = NormalizeAssetPath(AssetPath);
    const FString ObjectPath = MakeObjectPath(NormalizedAssetPath);
    UObject* Asset = FindObject<UObject>(nullptr, *ObjectPath);
    const bool bLoaded = Asset != nullptr;

    UPackage* Package = Asset ? Asset->GetPackage() : nullptr;
    int32 OpenEditors = 0;
    if (Asset)
    {
        if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
        {
            OpenEditors = AssetEditorSubsystem->FindEditorsForAsset(Asset).Num();
        }
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("asset_path"), NormalizedAssetPath);
    ResponseObject->SetStringField(TEXT("object_path"), ObjectPath);
    ResponseObject->SetBoolField(TEXT("loaded"), bLoaded);
    ResponseObject->SetBoolField(TEXT("dirty"), Package && Package->IsDirty());
    ResponseObject->SetStringField(TEXT("package_name"), Package ? Package->GetName() : FString());
    ResponseObject->SetNumberField(TEXT("open_editors"), OpenEditors);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleCreateMaterialFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString FunctionPath;
    if (!Params->TryGetStringField(TEXT("function_path"), FunctionPath) &&
        !Params->TryGetStringField(TEXT("asset_path"), FunctionPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_path' parameter"));
    }

    FunctionPath = NormalizeAssetPath(FunctionPath);
    const FString PackagePath = FPackageName::GetLongPackagePath(FunctionPath);
    const FString AssetName = FPackageName::GetLongPackageAssetName(FunctionPath);
    if (PackagePath.IsEmpty() || AssetName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid material function path: %s"), *FunctionPath));
    }

    if (UEditorAssetLibrary::DoesAssetExist(FunctionPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material function already exists: %s"), *FunctionPath));
    }

    UMaterialFunctionFactoryNew* FunctionFactory = NewObject<UMaterialFunctionFactoryNew>();
    UObject* CreatedObject = FAssetToolsModule::GetModule().Get().CreateAsset(
        AssetName,
        PackagePath,
        UMaterialFunction::StaticClass(),
        FunctionFactory,
        TEXT("UnrealMCP"));

    UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(CreatedObject);
    if (!MaterialFunction)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create material function: %s"), *FunctionPath));
    }

    FString SaveErrorMessage;
    if (!SaveLoadedAsset(MaterialFunction, FunctionPath, SaveErrorMessage))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("function_path"), FunctionPath);
    ResponseObject->SetStringField(TEXT("object_path"), MakeObjectPath(FunctionPath));
    ResponseObject->SetStringField(TEXT("name"), AssetName);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleRebuildMaterialFunctionGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString FunctionPath;
    if (!Params->TryGetStringField(TEXT("function_path"), FunctionPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_path' parameter"));
    }

    if (!Params->HasTypedField<EJson::Object>(TEXT("graph_spec")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'graph_spec' object parameter"));
    }

    FString AssetPath;
    FString ObjectPath;
    UMaterialFunction* MaterialFunction = LoadMaterialFunctionByPath(FunctionPath, AssetPath, ObjectPath);
    if (!MaterialFunction)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material function not found: %s"), *FunctionPath));
    }

    TSharedPtr<FJsonObject> GraphSpec = Params->GetObjectField(TEXT("graph_spec"));
    TSharedPtr<FJsonObject> Options = GraphSpec->HasTypedField<EJson::Object>(TEXT("options"))
        ? GraphSpec->GetObjectField(TEXT("options"))
        : MakeShared<FJsonObject>();

    const bool bWasDirty = MaterialFunction->GetPackage() && MaterialFunction->GetPackage()->IsDirty();
    UMaterialFunction* BackupFunction = DuplicateObject<UMaterialFunction>(MaterialFunction, GetTransientPackage());

    TArray<TSharedPtr<FJsonValue>> CreatedNodes;
    TArray<TSharedPtr<FJsonValue>> CreatedComments;
    FString ErrorMessage;

    auto FailWithRollback = [&](const FString& FailureMessage) -> TSharedPtr<FJsonObject>
    {
        UMaterialEditingLibrary::DeleteAllMaterialExpressionsInFunction(MaterialFunction);
        ClearFunctionComments(MaterialFunction);
        TArray<UMaterialExpression*> BackupExpressions;
        TArray<UMaterialExpressionComment*> BackupComments;
        CollectFunctionExpressions(BackupFunction, BackupExpressions, BackupComments);
        TArray<UMaterialExpression*> RestoredExpressions;
        TArray<UMaterialExpression*> RestoredComments;
        UMaterialExpression::CopyMaterialExpressions(BackupExpressions, BackupComments, nullptr, MaterialFunction, RestoredExpressions, RestoredComments);
        if (!bWasDirty && MaterialFunction->GetPackage())
        {
            MaterialFunction->GetPackage()->SetDirtyFlag(false);
        }
        return FUnrealMCPCommonUtils::CreateErrorResponse(FailureMessage);
    };

    MaterialFunction->Modify();
    if (!RebuildExpressionGraph(MaterialFunction, nullptr, MaterialFunction, GraphSpec, CreatedNodes, CreatedComments, ErrorMessage))
    {
        return FailWithRollback(ErrorMessage);
    }

    const bool bCompile = GetBoolFieldDefault(Options, TEXT("compile"), true);
    const bool bSave = GetBoolFieldDefault(Options, TEXT("save"), true);
    if (bCompile)
    {
        UMaterialEditingLibrary::UpdateMaterialFunction(MaterialFunction);
    }

    if (bSave)
    {
        FString SaveErrorMessage;
        if (!SaveLoadedAsset(MaterialFunction, AssetPath, SaveErrorMessage))
        {
            return FailWithRollback(SaveErrorMessage);
        }
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("function_path"), AssetPath);
    ResponseObject->SetStringField(TEXT("object_path"), ObjectPath);
    ResponseObject->SetArrayField(TEXT("nodes"), CreatedNodes);
    ResponseObject->SetArrayField(TEXT("comments"), CreatedComments);
    ResponseObject->SetBoolField(TEXT("saved"), bSave);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleConfigureGlassMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString MaterialPath;
    if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
    }

    FString AssetPath;
    FString ObjectPath;
    UMaterial* Material = LoadMaterialByPath(MaterialPath, AssetPath, ObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    FLinearColor Tint = FLinearColor(0.92f, 0.96f, 1.0f, 1.0f);
    if (Params->HasField(TEXT("tint")))
    {
        TArray<double> TintValues;
        if (!TryGetFloatArrayValue(Params->Values.FindRef(TEXT("tint")), TintValues) || TintValues.Num() < 3)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid 'tint' value"));
        }

        Tint = FLinearColor(
            static_cast<float>(TintValues[0]),
            static_cast<float>(TintValues[1]),
            static_cast<float>(TintValues[2]),
            static_cast<float>(TintValues.Num() >= 4 ? TintValues[3] : 1.0));
    }

    const float Roughness = Params->HasField(TEXT("roughness")) ? static_cast<float>(Params->GetNumberField(TEXT("roughness"))) : 0.02f;
    const float Specular = Params->HasField(TEXT("specular")) ? static_cast<float>(Params->GetNumberField(TEXT("specular"))) : 0.5f;
    const float BaseOpacity = Params->HasField(TEXT("base_opacity")) ? static_cast<float>(Params->GetNumberField(TEXT("base_opacity"))) : 0.08f;
    const float EdgeOpacity = Params->HasField(TEXT("edge_opacity")) ? static_cast<float>(Params->GetNumberField(TEXT("edge_opacity"))) : 0.24f;
    const float IOR = Params->HasField(TEXT("ior")) ? static_cast<float>(Params->GetNumberField(TEXT("ior"))) : 1.52f;
    const float FresnelExponent = Params->HasField(TEXT("fresnel_exponent")) ? static_cast<float>(Params->GetNumberField(TEXT("fresnel_exponent"))) : 5.0f;
    const float FresnelBaseReflectFraction = Params->HasField(TEXT("fresnel_base_reflect_fraction")) ? static_cast<float>(Params->GetNumberField(TEXT("fresnel_base_reflect_fraction"))) : 0.04f;
    const bool bTwoSided = Params->HasField(TEXT("two_sided")) ? Params->GetBoolField(TEXT("two_sided")) : true;

    Material->Modify();
    UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);

    Material->BlendMode = BLEND_Translucent;
    Material->TwoSided = bTwoSided;
    Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
    Material->RefractionMethod = RM_IndexOfRefraction;

    FString ErrorMessage;
    if (!TrySetMaterialShadingModel(Material, TEXT("DefaultLit"), ErrorMessage))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
    }

    UMaterialExpressionConstant3Vector* TintExpression = Cast<UMaterialExpressionConstant3Vector>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -1200, -280));
    UMaterialExpressionConstant* RoughnessExpression = Cast<UMaterialExpressionConstant>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -1200, -80));
    UMaterialExpressionConstant* SpecularExpression = Cast<UMaterialExpressionConstant>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -1200, 120));
    UMaterialExpressionConstant* BaseOpacityExpression = Cast<UMaterialExpressionConstant>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -1200, 320));
    UMaterialExpressionConstant* EdgeOpacityExpression = Cast<UMaterialExpressionConstant>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -1200, 500));
    UMaterialExpressionConstant* IORExpression = Cast<UMaterialExpressionConstant>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -1200, 700));
    UMaterialExpressionFresnel* FresnelExpression = Cast<UMaterialExpressionFresnel>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionFresnel::StaticClass(), -700, 460));
    UMaterialExpressionLinearInterpolate* OpacityLerpExpression = Cast<UMaterialExpressionLinearInterpolate>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLinearInterpolate::StaticClass(), -320, 400));

    if (!TintExpression || !RoughnessExpression || !SpecularExpression || !BaseOpacityExpression ||
        !EdgeOpacityExpression || !IORExpression || !FresnelExpression || !OpacityLerpExpression)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create one or more glass material expressions"));
    }

    TrySetExpressionLabel(TintExpression, TEXT("Tint"));
    TrySetExpressionLabel(RoughnessExpression, TEXT("Roughness"));
    TrySetExpressionLabel(SpecularExpression, TEXT("Specular"));
    TrySetExpressionLabel(BaseOpacityExpression, TEXT("BaseOpacity"));
    TrySetExpressionLabel(EdgeOpacityExpression, TEXT("EdgeOpacity"));
    TrySetExpressionLabel(IORExpression, TEXT("IOR"));
    TrySetExpressionLabel(FresnelExpression, TEXT("EdgeFresnel"));
    TrySetExpressionLabel(OpacityLerpExpression, TEXT("OpacityLerp"));

    TintExpression->Constant = Tint;
    RoughnessExpression->R = Roughness;
    SpecularExpression->R = Specular;
    BaseOpacityExpression->R = BaseOpacity;
    EdgeOpacityExpression->R = EdgeOpacity;
    IORExpression->R = IOR;
    FresnelExpression->Exponent = FresnelExponent;
    FresnelExpression->BaseReflectFraction = FresnelBaseReflectFraction;

    const bool bConnectionsSucceeded =
        UMaterialEditingLibrary::ConnectMaterialProperty(TintExpression, TEXT(""), MP_BaseColor) &&
        UMaterialEditingLibrary::ConnectMaterialProperty(RoughnessExpression, TEXT(""), MP_Roughness) &&
        UMaterialEditingLibrary::ConnectMaterialProperty(SpecularExpression, TEXT(""), MP_Specular) &&
        UMaterialEditingLibrary::ConnectMaterialExpressions(BaseOpacityExpression, TEXT(""), OpacityLerpExpression, TEXT("A")) &&
        UMaterialEditingLibrary::ConnectMaterialExpressions(EdgeOpacityExpression, TEXT(""), OpacityLerpExpression, TEXT("B")) &&
        UMaterialEditingLibrary::ConnectMaterialExpressions(FresnelExpression, TEXT(""), OpacityLerpExpression, TEXT("Alpha")) &&
        UMaterialEditingLibrary::ConnectMaterialProperty(OpacityLerpExpression, TEXT(""), MP_Opacity) &&
        UMaterialEditingLibrary::ConnectMaterialProperty(IORExpression, TEXT(""), MP_Refraction);

    if (!bConnectionsSucceeded)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to connect one or more glass material expressions"));
    }

    UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
    RecompileMaterialAsset(Material);

    FString SaveErrorMessage;
    if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetStringField(TEXT("object_path"), ObjectPath);
    ResponseObject->SetStringField(TEXT("preset"), TEXT("glass"));

    TArray<TSharedPtr<FJsonValue>> ExpressionArray;
    ExpressionArray.Add(MakeShared<FJsonValueObject>(ExpressionToJson(TintExpression)));
    ExpressionArray.Add(MakeShared<FJsonValueObject>(ExpressionToJson(RoughnessExpression)));
    ExpressionArray.Add(MakeShared<FJsonValueObject>(ExpressionToJson(SpecularExpression)));
    ExpressionArray.Add(MakeShared<FJsonValueObject>(ExpressionToJson(BaseOpacityExpression)));
    ExpressionArray.Add(MakeShared<FJsonValueObject>(ExpressionToJson(EdgeOpacityExpression)));
    ExpressionArray.Add(MakeShared<FJsonValueObject>(ExpressionToJson(IORExpression)));
    ExpressionArray.Add(MakeShared<FJsonValueObject>(ExpressionToJson(FresnelExpression)));
    ExpressionArray.Add(MakeShared<FJsonValueObject>(ExpressionToJson(OpacityLerpExpression)));
    ResponseObject->SetArrayField(TEXT("expressions"), ExpressionArray);

    TArray<TSharedPtr<FJsonValue>> TintArray;
    TintArray.Add(MakeJsonNumber(Tint.R));
    TintArray.Add(MakeJsonNumber(Tint.G));
    TintArray.Add(MakeJsonNumber(Tint.B));
    TintArray.Add(MakeJsonNumber(Tint.A));

    TSharedPtr<FJsonObject> SettingsObject = MakeShared<FJsonObject>();
    SettingsObject->SetArrayField(TEXT("tint"), TintArray);
    SettingsObject->SetNumberField(TEXT("roughness"), Roughness);
    SettingsObject->SetNumberField(TEXT("specular"), Specular);
    SettingsObject->SetNumberField(TEXT("base_opacity"), BaseOpacity);
    SettingsObject->SetNumberField(TEXT("edge_opacity"), EdgeOpacity);
    SettingsObject->SetNumberField(TEXT("ior"), IOR);
    SettingsObject->SetNumberField(TEXT("fresnel_exponent"), FresnelExponent);
    SettingsObject->SetNumberField(TEXT("fresnel_base_reflect_fraction"), FresnelBaseReflectFraction);
    SettingsObject->SetBoolField(TEXT("two_sided"), bTwoSided);
    ResponseObject->SetObjectField(TEXT("settings"), SettingsObject);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResponseObject);
}
