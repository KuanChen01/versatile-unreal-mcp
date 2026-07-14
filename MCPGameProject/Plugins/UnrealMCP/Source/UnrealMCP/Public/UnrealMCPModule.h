#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Modules/ModuleManager.h"
#include "Templates/UniquePtr.h"

struct FUnrealMCPLogEntry
{
	FString Timestamp;
	FString Category;
	FString Verbosity;
	FString Message;
};

class FUnrealMCPModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TArray<FUnrealMCPLogEntry> GetBufferedLogEntries() const;

	static inline FUnrealMCPModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FUnrealMCPModule>("UnrealMCP");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("UnrealMCP");
	}

private:
	void AddBufferedLogEntry(const FUnrealMCPLogEntry& Entry);

	static constexpr int32 MaxBufferedLogEntries = 4000;

	mutable FCriticalSection BufferedLogEntriesCriticalSection;
	TArray<FUnrealMCPLogEntry> BufferedLogEntries;
	TUniquePtr<class FUnrealMCPLogOutputDevice> LogOutputDevice;

	friend class FUnrealMCPLogOutputDevice;
};
