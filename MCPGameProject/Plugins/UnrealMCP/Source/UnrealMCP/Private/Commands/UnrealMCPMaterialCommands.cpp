#include "Commands/UnrealMCPMaterialCommands.h"

#include "Commands/UnrealMCPCommonUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Misc/PackageName.h"
#include "SceneTypes.h"
#include "UObject/EnumProperty.h"
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

UMaterialExpression* FUnrealMCPMaterialCommands::FindExpressionByReference(UMaterial* Material, const FString& ExpressionReference) const
{
    if (!Material || ExpressionReference.IsEmpty())
    {
        return nullptr;
    }

    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (!Expression)
        {
            continue;
        }

        if (Expression->GetName().Equals(ExpressionReference, ESearchCase::CaseSensitive))
        {
            return Expression;
        }

        FString Label;
        TryGetExpressionLabel(Expression, Label);
        if (Label.Equals(ExpressionReference, ESearchCase::CaseSensitive))
        {
            return Expression;
        }
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
    if (!Material)
    {
        OutErrorMessage = TEXT("Invalid material");
        return false;
    }

    Material->MarkPackageDirty();

    if (UEditorAssetLibrary::SaveLoadedAsset(Material, true))
    {
        return true;
    }

    if (!AssetPath.IsEmpty() && UEditorAssetLibrary::SaveAsset(AssetPath, true))
    {
        return true;
    }

    OutErrorMessage = FString::Printf(TEXT("Failed to save material asset: %s"), *AssetPath);
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

    TArray<TSharedPtr<FJsonValue>> PositionArray;
    PositionArray.Add(MakeJsonNumber(Expression->MaterialExpressionEditorX));
    PositionArray.Add(MakeJsonNumber(Expression->MaterialExpressionEditorY));
    JsonObject->SetArrayField(TEXT("position"), PositionArray);

    return JsonObject;
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

    Material->Modify();

    UMaterialExpression* Expression = SelectedAsset
        ? UMaterialEditingLibrary::CreateMaterialExpressionEx(Material, nullptr, ExpressionClass, SelectedAsset, Position.X, Position.Y, true)
        : UMaterialEditingLibrary::CreateMaterialExpression(Material, ExpressionClass, Position.X, Position.Y);

    if (!Expression)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create material expression"));
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

    RecompileMaterialAsset(Material);

    FString SaveErrorMessage;
    if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetObjectField(TEXT("expression"), ExpressionToJson(Expression));

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
    if (!Params->TryGetStringField(TEXT("expression"), ExpressionReference) &&
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

    UMaterialExpression* Expression = FindExpressionByReference(Material, ExpressionReference);
    if (!Expression)
    {
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

    RecompileMaterialAsset(Material);

    FString SaveErrorMessage;
    if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetObjectField(TEXT("expression"), ExpressionToJson(Expression));

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
    if (!Params->TryGetStringField(TEXT("from_expression"), FromExpressionReference))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'from_expression' parameter"));
    }

    FString ToExpressionReference;
    if (!Params->TryGetStringField(TEXT("to_expression"), ToExpressionReference))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'to_expression' parameter"));
    }

    FString AssetPath;
    FString ObjectPath;
    UMaterial* Material = LoadMaterialByPath(MaterialPath, AssetPath, ObjectPath);
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
    }

    UMaterialExpression* FromExpression = FindExpressionByReference(Material, FromExpressionReference);
    if (!FromExpression)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Source material expression not found: %s"), *FromExpressionReference));
    }

    UMaterialExpression* ToExpression = FindExpressionByReference(Material, ToExpressionReference);
    if (!ToExpression)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Target material expression not found: %s"), *ToExpressionReference));
    }

    const FString FromOutputName = Params->HasField(TEXT("from_output_name")) ? Params->GetStringField(TEXT("from_output_name")) : TEXT("");
    const FString ToInputName = Params->HasField(TEXT("to_input_name")) ? Params->GetStringField(TEXT("to_input_name")) : TEXT("");

    Material->Modify();

    if (!UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpression, FromOutputName, ToExpression, ToInputName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to connect material expressions"));
    }

    RecompileMaterialAsset(Material);

    FString SaveErrorMessage;
    if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetObjectField(TEXT("from_expression"), ExpressionToJson(FromExpression));
    ResponseObject->SetObjectField(TEXT("to_expression"), ExpressionToJson(ToExpression));

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
    if (!Params->TryGetStringField(TEXT("expression"), ExpressionReference))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'expression' parameter"));
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

    UMaterialExpression* Expression = FindExpressionByReference(Material, ExpressionReference);
    if (!Expression)
    {
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

    RecompileMaterialAsset(Material);

    FString SaveErrorMessage;
    if (!SaveMaterialAsset(Material, AssetPath, SaveErrorMessage))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(SaveErrorMessage);
    }

    TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
    ResponseObject->SetStringField(TEXT("material_path"), AssetPath);
    ResponseObject->SetStringField(TEXT("property_name"), PropertyName);
    ResponseObject->SetObjectField(TEXT("expression"), ExpressionToJson(Expression));

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
