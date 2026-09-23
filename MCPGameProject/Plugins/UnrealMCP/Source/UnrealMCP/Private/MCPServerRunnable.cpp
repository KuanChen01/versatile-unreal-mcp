#include "MCPServerRunnable.h"
#include "UnrealMCPBridge.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealMCPServer, Log, All);

namespace
{
	const TCHAR* ProtocolIncompatibleHint =
		TEXT("UnrealMCP protocol incompatible or framing failed. "
			 "Upgrade the UnrealMCP editor plugin and Python server together "
			 "(protocol 2.0, length-prefixed frames).");

	uint32 ReadLittleEndianUInt32(const uint8* Bytes)
	{
		return (uint32)Bytes[0]
			| ((uint32)Bytes[1] << 8)
			| ((uint32)Bytes[2] << 16)
			| ((uint32)Bytes[3] << 24);
	}

	void WriteLittleEndianUInt32(uint32 Value, uint8* Bytes)
	{
		Bytes[0] = (uint8)(Value & 0xFF);
		Bytes[1] = (uint8)((Value >> 8) & 0xFF);
		Bytes[2] = (uint8)((Value >> 16) & 0xFF);
		Bytes[3] = (uint8)((Value >> 24) & 0xFF);
	}

	// FIN leaves the previous WSA error in place. Only these codes were set by the recv that just failed.
	bool IsFreshHardClose(ESocketErrors Error)
	{
		switch (Error)
		{
		case SE_ECONNRESET:
		case SE_ECONNABORTED:
		case SE_ENETRESET:
		case SE_ESHUTDOWN:
		case SE_ENOTCONN:
		case SE_ECONNREFUSED:
		case SE_ETIMEDOUT:
		case SE_ENETDOWN:
		case SE_ENETUNREACH:
			return true;
		default:
			return false;
		}
	}
}

FMCPServerRunnable::FMCPServerRunnable(UUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InListenerSocket)
	: Bridge(InBridge)
	, ListenerSocket(InListenerSocket)
	, bRunning(true)
{
	UE_LOG(LogUnrealMCPServer, Display, TEXT("MCPServerRunnable: Created server runnable (protocol 2.0 length-prefix)"));
}

FMCPServerRunnable::~FMCPServerRunnable()
{
	// Sockets are owned by the bridge.
}

bool FMCPServerRunnable::Init()
{
	return true;
}

FMCPServerRunnable::ERecvExactResult FMCPServerRunnable::RecvExact(
	FSocket* Socket,
	uint8* Dest,
	int32 NumBytes,
	double TimeoutSeconds,
	int32* OutBytesRead) const
{
	if (OutBytesRead)
	{
		*OutBytesRead = 0;
	}

	if (!Socket || !Dest || NumBytes <= 0)
	{
		return ERecvExactResult::SocketError;
	}

	int32 TotalRead = 0;
	const double StartTime = FPlatformTime::Seconds();

	while (TotalRead < NumBytes && bRunning)
	{
		const double Elapsed = FPlatformTime::Seconds() - StartTime;
		if (Elapsed > TimeoutSeconds)
		{
			if (OutBytesRead)
			{
				*OutBytesRead = TotalRead;
			}
			return ERecvExactResult::Timeout;
		}

		int32 BytesRead = 0;
		// FSocketBSD stream Recv: true+0 is would-block; false+0 is FIN or a hard error.
		if (Socket->Recv(Dest + TotalRead, NumBytes - TotalRead, BytesRead))
		{
			if (BytesRead > 0)
			{
				TotalRead += BytesRead;
				continue;
			}

			const double Remaining = TimeoutSeconds - (FPlatformTime::Seconds() - StartTime);
			if (Remaining <= 0.0)
			{
				if (OutBytesRead)
				{
					*OutBytesRead = TotalRead;
				}
				return ERecvExactResult::Timeout;
			}
			Socket->Wait(
				ESocketWaitConditions::WaitForRead,
				FTimespan::FromSeconds(FMath::Min(0.05, Remaining)));
			continue;
		}

		uint32 PendingSize = 0;
		const bool bHasPayload = Socket->HasPendingData(PendingSize);
		const ESocketErrors LastError = ISocketSubsystem::Get()->GetLastErrorCode();
		// Readable with FIONREAD 0 is a graceful FIN, unless this recv just stored a hard error.
		if (!bHasPayload
			&& !IsFreshHardClose(LastError)
			&& Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::Zero()))
		{
			if (OutBytesRead)
			{
				*OutBytesRead = TotalRead;
			}
			return TotalRead == 0 ? ERecvExactResult::PeerClosed : ERecvExactResult::PartialClose;
		}

		if (LastError == SE_EWOULDBLOCK || LastError == SE_EINTR)
		{
			const double Remaining = TimeoutSeconds - (FPlatformTime::Seconds() - StartTime);
			if (Remaining <= 0.0)
			{
				if (OutBytesRead)
				{
					*OutBytesRead = TotalRead;
				}
				return ERecvExactResult::Timeout;
			}
			Socket->Wait(
				ESocketWaitConditions::WaitForRead,
				FTimespan::FromSeconds(FMath::Min(0.05, Remaining)));
			continue;
		}

		UE_LOG(LogUnrealMCPServer, Warning,
			TEXT("MCPServerRunnable: RecvExact failed error=%d (%d/%d)"),
			static_cast<int32>(LastError), TotalRead, NumBytes);
		if (OutBytesRead)
		{
			*OutBytesRead = TotalRead;
		}
		return ERecvExactResult::SocketError;
	}

	if (OutBytesRead)
	{
		*OutBytesRead = TotalRead;
	}

	if (!bRunning && TotalRead < NumBytes)
	{
		return ERecvExactResult::Aborted;
	}

	return TotalRead == NumBytes ? ERecvExactResult::Ok : ERecvExactResult::Aborted;
}

bool FMCPServerRunnable::SendExact(FSocket* Socket, const uint8* Source, int32 NumBytes, double TimeoutSeconds) const
{
	if (!Socket || !Source || NumBytes <= 0)
	{
		return false;
	}

	int32 TotalSent = 0;
	const double StartTime = FPlatformTime::Seconds();

	while (TotalSent < NumBytes && bRunning)
	{
		int32 BytesSent = 0;
		if (Socket->Send(Source + TotalSent, NumBytes - TotalSent, BytesSent))
		{
			if (BytesSent == 0)
			{
				UE_LOG(LogUnrealMCPServer, Warning, TEXT("MCPServerRunnable: Send returned 0 bytes (%d/%d)"), TotalSent, NumBytes);
				return false;
			}
			TotalSent += BytesSent;
			continue;
		}

		const int32 LastError = (int32)ISocketSubsystem::Get()->GetLastErrorCode();
		if (LastError == SE_EWOULDBLOCK || LastError == SE_EINTR)
		{
			if ((FPlatformTime::Seconds() - StartTime) > TimeoutSeconds)
			{
				UE_LOG(LogUnrealMCPServer, Warning, TEXT("MCPServerRunnable: SendExact timed out (%d/%d)"), TotalSent, NumBytes);
				return false;
			}
			FPlatformProcess::Sleep(0.001f);
			continue;
		}

		UE_LOG(LogUnrealMCPServer, Warning, TEXT("MCPServerRunnable: SendExact failed error=%d (%d/%d)"), LastError, TotalSent, NumBytes);
		return false;
	}

	return TotalSent == NumBytes;
}

bool FMCPServerRunnable::RecvFrame(
	FSocket* Socket,
	TArray<uint8>& OutPayload,
	double TimeoutSeconds,
	ERecvExactResult* OutResult) const
{
	uint8 Header[4];
	int32 HeaderBytes = 0;
	const ERecvExactResult HeaderResult = RecvExact(Socket, Header, 4, TimeoutSeconds, &HeaderBytes);
	if (OutResult)
	{
		*OutResult = HeaderResult;
	}

	if (HeaderResult != ERecvExactResult::Ok)
	{
		switch (HeaderResult)
		{
		case ERecvExactResult::PeerClosed:
			// Normal one-shot client lifecycle: connect → one command → close.
			UE_LOG(LogUnrealMCPServer, Verbose, TEXT("MCPServerRunnable: Client closed connection (clean EOF before next frame)"));
			break;
		case ERecvExactResult::Timeout:
			// Idle session end after serving request(s). Expected with one-shot clients.
			UE_LOG(LogUnrealMCPServer, Verbose, TEXT("MCPServerRunnable: Client idle timeout waiting for next frame header"));
			break;
		case ERecvExactResult::PartialClose:
			UE_LOG(LogUnrealMCPServer, Warning,
				TEXT("MCPServerRunnable: Connection closed mid frame header (%d/4 bytes). %s"),
				HeaderBytes, ProtocolIncompatibleHint);
			break;
		case ERecvExactResult::SocketError:
			UE_LOG(LogUnrealMCPServer, Warning,
				TEXT("MCPServerRunnable: Socket error reading frame header. %s"),
				ProtocolIncompatibleHint);
			break;
		case ERecvExactResult::Aborted:
			UE_LOG(LogUnrealMCPServer, Display, TEXT("MCPServerRunnable: RecvFrame aborted (server stopping)"));
			break;
		default:
			break;
		}
		return false;
	}

	const uint32 Length = ReadLittleEndianUInt32(Header);
	if (Length == 0 || Length > MaxPayloadBytes)
	{
		if (OutResult)
		{
			*OutResult = ERecvExactResult::SocketError;
		}
		UE_LOG(LogUnrealMCPServer, Error,
			TEXT("MCPServerRunnable: Invalid payload length %u (max %u). %s"),
			Length, MaxPayloadBytes, ProtocolIncompatibleHint);
		return false;
	}

	OutPayload.SetNumUninitialized((int32)Length);
	int32 PayloadBytes = 0;
	const ERecvExactResult PayloadResult = RecvExact(Socket, OutPayload.GetData(), (int32)Length, TimeoutSeconds, &PayloadBytes);
	if (OutResult)
	{
		*OutResult = PayloadResult;
	}

	if (PayloadResult != ERecvExactResult::Ok)
	{
		UE_LOG(LogUnrealMCPServer, Warning,
			TEXT("MCPServerRunnable: Failed to read frame payload (%d/%u bytes, result=%d)"),
			PayloadBytes, Length, static_cast<int32>(PayloadResult));
		return false;
	}

	return true;
}

bool FMCPServerRunnable::SendFrame(FSocket* Socket, const TArray<uint8>& Payload, double TimeoutSeconds) const
{
	if (Payload.Num() <= 0 || (uint32)Payload.Num() > MaxPayloadBytes)
	{
		UE_LOG(LogUnrealMCPServer, Error, TEXT("MCPServerRunnable: Refusing to send invalid payload size %d"), Payload.Num());
		return false;
	}

	uint8 Header[4];
	WriteLittleEndianUInt32((uint32)Payload.Num(), Header);

	if (!SendExact(Socket, Header, 4, TimeoutSeconds))
	{
		return false;
	}
	return SendExact(Socket, Payload.GetData(), Payload.Num(), TimeoutSeconds);
}

void FMCPServerRunnable::ProcessJsonPayload(FSocket* Socket, const TArray<uint8>& Payload)
{
	// Null-terminate a copy so UTF-8 conversion APIs can treat the buffer as a C string.
	TArray<uint8> NullTerminated;
	NullTerminated.Reserve(Payload.Num() + 1);
	NullTerminated.Append(Payload);
	NullTerminated.Add(0);
	const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(NullTerminated.GetData()));
	const FString ReceivedText(Converter.Get());

	UE_LOG(LogUnrealMCPServer, Display, TEXT("MCPServerRunnable: Received frame (%d bytes)"), Payload.Num());
	UE_LOG(LogUnrealMCPServer, Verbose, TEXT("MCPServerRunnable: Payload: %s"), *ReceivedText);

	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReceivedText);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogUnrealMCPServer, Warning, TEXT("MCPServerRunnable: Failed to parse JSON payload. %s"), ProtocolIncompatibleHint);
		return;
	}

	FString CommandType;
	if (!JsonObject->TryGetStringField(TEXT("type"), CommandType))
	{
		UE_LOG(LogUnrealMCPServer, Warning, TEXT("MCPServerRunnable: Missing 'type' field in command"));
		return;
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	if (JsonObject->HasField(TEXT("params")))
	{
		const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("params"), ParamsObject) && ParamsObject && ParamsObject->IsValid())
		{
			Params = *ParamsObject;
		}
	}

	FString RequestId;
	JsonObject->TryGetStringField(TEXT("request_id"), RequestId);
	if (RequestId.IsEmpty() && Params.IsValid())
	{
		// Optional fallback if a client nested the id
		Params->TryGetStringField(TEXT("request_id"), RequestId);
	}

	if (!RequestId.IsEmpty())
	{
		UE_LOG(LogUnrealMCPServer, Display, TEXT("MCPServerRunnable: command=%s request_id=%s"), *CommandType, *RequestId);
	}

	const FString Response = Bridge->ExecuteCommand(CommandType, Params, RequestId);
	if (RequestId.IsEmpty())
	{
		UE_LOG(LogUnrealMCPServer, Display, TEXT("MCPServerRunnable: Sending response for %s (%d chars)"), *CommandType, Response.Len());
	}
	else
	{
		UE_LOG(LogUnrealMCPServer, Display, TEXT("MCPServerRunnable: Sending response for %s request_id=%s (%d chars)"), *CommandType, *RequestId, Response.Len());
	}

	FTCHARToUTF8 ResponseUtf8(*Response);
	TArray<uint8> ResponseBytes;
	ResponseBytes.Append(reinterpret_cast<const uint8*>(ResponseUtf8.Get()), ResponseUtf8.Length());

	if (!SendFrame(Socket, ResponseBytes, DefaultIOTimeoutSeconds))
	{
		UE_LOG(LogUnrealMCPServer, Warning, TEXT("MCPServerRunnable: Failed to send framed response for %s"), *CommandType);
	}
	else
	{
		UE_LOG(LogUnrealMCPServer, Display, TEXT("MCPServerRunnable: Framed response sent (%d bytes body)"), ResponseBytes.Num());
	}
}

uint32 FMCPServerRunnable::Run()
{
	UE_LOG(LogUnrealMCPServer, Display, TEXT("MCPServerRunnable: Server thread starting (protocol 2.0)..."));

	while (bRunning)
	{
		bool bPending = false;
		if (ListenerSocket.IsValid() && ListenerSocket->HasPendingConnection(bPending) && bPending)
		{
			UE_LOG(LogUnrealMCPServer, Display, TEXT("MCPServerRunnable: Client connection pending, accepting..."));

			ClientSocket = MakeShareable(ListenerSocket->Accept(TEXT("MCPClient")));
			if (ClientSocket.IsValid())
			{
				UE_LOG(LogUnrealMCPServer, Display, TEXT("MCPServerRunnable: Client connection accepted"));

				ClientSocket->SetNoDelay(true);
				// Non-blocking so RecvExact / Wait can enforce idle timeouts without wedging Accept.
				ClientSocket->SetNonBlocking(true);
				int32 SocketBufferSize = 65536;
				ClientSocket->SetSendBufferSize(SocketBufferSize, SocketBufferSize);
				ClientSocket->SetReceiveBufferSize(SocketBufferSize, SocketBufferSize);

				// Serve multiple framed requests on one TCP session until the client
				// disconnects or goes idle. Do NOT gate on GetConnectionState() — on
				// Windows/UE it can spuriously report non-Connected after a response
				// and kill session reuse (agents then hit WinError 10053 on the next tool).
				//
				// Idle wait is sliced (100ms) so we can:
				//  1) detect peer EOF promptly via WaitForRead
				//  2) yield the accept slot when another MCP client is pending
				//     (multi-agent hosts: Codex/Claude/Grok share one Editor bridge)
				constexpr double ClientIdleTimeoutSeconds = 30.0;
				constexpr double IdlePollSliceSeconds = 0.1;
				int32 RequestsOnSession = 0;
				while (bRunning && ClientSocket.IsValid())
				{
					bool bReadable = false;
					bool bYieldToPendingClient = false;
					const double IdleDeadline = FPlatformTime::Seconds() + ClientIdleTimeoutSeconds;

					while (bRunning && ClientSocket.IsValid())
					{
						const double Remaining = IdleDeadline - FPlatformTime::Seconds();
						if (Remaining <= 0.0)
						{
							break;
						}

						const double Slice = FMath::Min(IdlePollSliceSeconds, Remaining);
						if (ClientSocket->Wait(
							ESocketWaitConditions::WaitForRead,
							FTimespan::FromSeconds(Slice)))
						{
							bReadable = true;
							break;
						}

						// Fairness: if we already served >=1 request and another TCP
						// client is waiting in the backlog, end this idle session so
						// Accept can proceed. One-shot Python clients disconnect after
						// each command; long-lived reuse clients must re-connect after yield.
						bool bIncomingClient = false;
						if (RequestsOnSession > 0
							&& ListenerSocket.IsValid()
							&& ListenerSocket->HasPendingConnection(bIncomingClient)
							&& bIncomingClient)
						{
							bYieldToPendingClient = true;
							break;
						}
					}

					if (!bRunning)
					{
						break;
					}

					if (bYieldToPendingClient)
					{
						UE_LOG(LogUnrealMCPServer, Display,
							TEXT("MCPServerRunnable: Yielding idle session to pending client (served %d request(s))"),
							RequestsOnSession);
						break;
					}

					if (!bReadable)
					{
						UE_LOG(LogUnrealMCPServer, Display,
							TEXT("MCPServerRunnable: Ending client session after idle timeout (served %d request(s))"),
							RequestsOnSession);
						break;
					}

					// WaitForRead is also true on FIN. FIONREAD == 0 means there is no frame to read.
					uint32 PendingBytes = 0;
					if (!ClientSocket->HasPendingData(PendingBytes))
					{
						UE_LOG(LogUnrealMCPServer, Verbose,
							TEXT("MCPServerRunnable: Ending client session after clean disconnect (served %d request(s))"),
							RequestsOnSession);
						break;
					}

					TArray<uint8> Payload;
					ERecvExactResult RecvResult = ERecvExactResult::Ok;
					// Payload bytes are already queued. This timeout covers the rest of the frame.
					if (!RecvFrame(ClientSocket.Get(), Payload, DefaultIOTimeoutSeconds, &RecvResult))
					{
						if (RecvResult == ERecvExactResult::PeerClosed)
						{
							UE_LOG(LogUnrealMCPServer, Verbose,
								TEXT("MCPServerRunnable: Ending client session after clean disconnect (served %d request(s))"),
								RequestsOnSession);
						}
						else if (RecvResult == ERecvExactResult::Timeout)
						{
							UE_LOG(LogUnrealMCPServer, Display,
								TEXT("MCPServerRunnable: Ending client session after mid-frame timeout (served %d request(s))"),
								RequestsOnSession);
						}
						else if (RecvResult == ERecvExactResult::PartialClose)
						{
							UE_LOG(LogUnrealMCPServer, Warning,
								TEXT("MCPServerRunnable: Ending client session after partial close (served %d request(s))"),
								RequestsOnSession);
						}
						else if (RecvResult == ERecvExactResult::SocketError)
						{
							UE_LOG(LogUnrealMCPServer, Warning,
								TEXT("MCPServerRunnable: Ending client session after socket error (served %d request(s))"),
								RequestsOnSession);
						}
						else
						{
							UE_LOG(LogUnrealMCPServer, Display,
								TEXT("MCPServerRunnable: Ending client session result=%d (served %d request(s))"),
								static_cast<int32>(RecvResult), RequestsOnSession);
						}
						break;
					}

					++RequestsOnSession;
					ProcessJsonPayload(ClientSocket.Get(), Payload);
				}

				if (ClientSocket.IsValid())
				{
					ClientSocket->Close();
					ClientSocket.Reset();
				}

				// After a session ends, immediately check for the next pending client
				// instead of sleeping — reduces races when agents reconnect quickly.
				continue;
			}
			else
			{
				UE_LOG(LogUnrealMCPServer, Warning, TEXT("MCPServerRunnable: Failed to accept client connection"));
			}
		}

		FPlatformProcess::Sleep(0.01f);
	}

	UE_LOG(LogUnrealMCPServer, Display, TEXT("MCPServerRunnable: Server thread stopping"));
	return 0;
}

void FMCPServerRunnable::Stop()
{
	bRunning = false;
}

void FMCPServerRunnable::Exit()
{
}
