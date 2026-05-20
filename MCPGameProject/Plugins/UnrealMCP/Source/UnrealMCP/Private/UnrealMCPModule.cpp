#include "UnrealMCPModule.h"
#include "UnrealMCPBridge.h"
#include "Modules/ModuleManager.h"
#include "EditorSubsystem.h"
#include "Editor.h"
#include "HAL/PlatformTime.h"
#include "Misc/OutputDevice.h"

namespace
{
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

}

class FUnrealMCPLogOutputDevice final : public FOutputDevice
{
public:
	explicit FUnrealMCPLogOutputDevice(FUnrealMCPModule& InOwner)
		: Owner(InOwner)
	{
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		Serialize(V, Verbosity, Category, FPlatformTime::Seconds());
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, const double Time) override
	{
		const ELogVerbosity::Type VerbosityMask = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
		if (VerbosityMask == ELogVerbosity::SetColor || V == nullptr)
		{
			return;
		}

		FString Message(V);
		Message.TrimEndInline();
		if (Message.IsEmpty())
		{
			return;
		}

		FUnrealMCPLogEntry Entry;
		Entry.Timestamp = FDateTime::UtcNow().ToIso8601();
		Entry.Category = Category.ToString();
		Entry.Verbosity = LogVerbosityToString(VerbosityMask);
		Entry.Message = MoveTemp(Message);
		Owner.AddBufferedLogEntry(Entry);
	}

	virtual bool CanBeUsedOnAnyThread() const override
	{
		return true;
	}

private:
	FUnrealMCPModule& Owner;
};

#define LOCTEXT_NAMESPACE "FUnrealMCPModule"

void FUnrealMCPModule::StartupModule()
{
	LogOutputDevice = MakeUnique<FUnrealMCPLogOutputDevice>(*this);
	if (GLog && LogOutputDevice.IsValid())
	{
		GLog->AddOutputDevice(LogOutputDevice.Get());
	}

	UE_LOG(LogTemp, Display, TEXT("Unreal MCP Module has started"));
}

void FUnrealMCPModule::ShutdownModule()
{
	UE_LOG(LogTemp, Display, TEXT("Unreal MCP Module has shut down"));

	if (GLog && LogOutputDevice.IsValid())
	{
		GLog->RemoveOutputDevice(LogOutputDevice.Get());
	}
	LogOutputDevice.Reset();
}

TArray<FUnrealMCPLogEntry> FUnrealMCPModule::GetBufferedLogEntries() const
{
	FScopeLock Lock(&BufferedLogEntriesCriticalSection);
	return BufferedLogEntries;
}

void FUnrealMCPModule::AddBufferedLogEntry(const FUnrealMCPLogEntry& Entry)
{
	FScopeLock Lock(&BufferedLogEntriesCriticalSection);

	if (BufferedLogEntries.Num() >= MaxBufferedLogEntries)
	{
		const int32 NumToRemove = (BufferedLogEntries.Num() - MaxBufferedLogEntries) + 1;
		BufferedLogEntries.RemoveAt(0, NumToRemove, EAllowShrinking::No);
	}

	BufferedLogEntries.Add(Entry);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUnrealMCPModule, UnrealMCP)
