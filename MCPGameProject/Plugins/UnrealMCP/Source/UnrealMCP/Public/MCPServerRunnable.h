#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"
#include "Interfaces/IPv4/IPv4Address.h"

class UUnrealMCPBridge;

/**
 * Runnable class for the MCP server thread.
 *
 * Wire protocol 2.0: little-endian uint32 payload length + UTF-8 JSON body.
 */
class FMCPServerRunnable : public FRunnable
{
public:
	FMCPServerRunnable(UUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InListenerSocket);
	virtual ~FMCPServerRunnable();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

protected:
	/** Outcome of a timed exact socket read (used to avoid false protocol-mismatch logs). */
	enum class ERecvExactResult : uint8
	{
		Ok = 0,
		/** Peer closed cleanly before any bytes of this read were received (normal one-shot end). */
		PeerClosed,
		/** Peer closed after a partial read (real framing/session problem). */
		PartialClose,
		/** Idle timeout with no progress. */
		Timeout,
		/** Non-recoverable socket error. */
		SocketError,
		/** Server thread is stopping. */
		Aborted
	};

	/** Read exactly NumBytes into Dest, respecting non-blocking sockets and timeout. */
	ERecvExactResult RecvExact(FSocket* Socket, uint8* Dest, int32 NumBytes, double TimeoutSeconds, int32* OutBytesRead = nullptr) const;

	/** Write exactly NumBytes from Source. */
	bool SendExact(FSocket* Socket, const uint8* Source, int32 NumBytes, double TimeoutSeconds) const;

	/**
	 * Read one length-prefixed UTF-8 JSON payload into OutPayload.
	 * OutResult receives the detailed recv outcome (optional).
	 */
	bool RecvFrame(FSocket* Socket, TArray<uint8>& OutPayload, double TimeoutSeconds, ERecvExactResult* OutResult = nullptr) const;

	/** Send one length-prefixed UTF-8 JSON payload. */
	bool SendFrame(FSocket* Socket, const TArray<uint8>& Payload, double TimeoutSeconds) const;

	/** Parse and execute a JSON command body; send a framed JSON response. */
	void ProcessJsonPayload(FSocket* Socket, const TArray<uint8>& Payload);

private:
	UUnrealMCPBridge* Bridge;
	TSharedPtr<FSocket> ListenerSocket;
	TSharedPtr<FSocket> ClientSocket;
	bool bRunning;

	static constexpr uint32 MaxPayloadBytes = 16u * 1024u * 1024u;
	static constexpr double DefaultIOTimeoutSeconds = 120.0;
};
