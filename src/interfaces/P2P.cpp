//
// P2P interface over the LAN transport.
//
// Mapping to the header's model (eos_p2p.h):
//   * SendPacket implicitly opens a connection -- there is no Connect(). On the
//     first send to a (peer, socket) we auto-accept (unless the caller disabled
//     it) and send a ConnectRequest so the peer's PeerConnectionRequest fires.
//   * Receiving is poll-based: datagrams land in a locked inbox that the app
//     drains via GetNextReceivedPacketSize + ReceivePacket every frame.
//   * Only connection lifecycle is delivered by callback, and those callbacks
//     fire from the Dispatcher (Tick thread), never from the receive thread.
//
// Reliability is real: EOS_PR_ReliableUnordered and EOS_PR_ReliableOrdered are
// backed by a seq/ack/retransmit layer (a per-(peer,socket,channel,ordered)
// stream; see P2P.h). Ordered packets are released to the inbox only in
// sequence; unreliable packets are a straight passthrough. The channel travels
// with every packet so the demux in ReceivePacket is exact.
//

#include "interfaces/P2P.h"

#include "core/Identity.h"
#include "core/Ids.h"
#include "core/Logging.h"
#include "core/Peers.h"
#include "core/Platform.h"
#include "net/Serialize.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace EOSEmu
{
	namespace
	{
		std::string SocketName(const EOS_P2P_SocketId* SocketId)
		{
			if (SocketId == nullptr)
			{
				return {};
			}
			// SocketName is a fixed char[33]; guarantee termination.
			const char* Name = SocketId->SocketName;
			size_t Len = 0;
			while (Len < EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1 && Name[Len] != '\0')
			{
				++Len;
			}
			return std::string(Name, Len);
		}

		void FillSocketId(EOS_P2P_SocketId& Out, const std::string& Name)
		{
			Out.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
			std::memset(Out.SocketName, 0, sizeof(Out.SocketName));
			const size_t N = Name.size() < (EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1)
				? Name.size() : (EOS_P2P_SOCKETID_SOCKETNAME_SIZE - 1);
			std::memcpy(Out.SocketName, Name.data(), N);
		}

		bool SocketMatches(const std::string& Filter, const std::string& Socket)
		{
			return Filter.empty() || Filter == Socket;
		}

		// Reliability timing. The RTO starts at kInitialRto until an RTT sample
		// lands, then tracks SRTT + 4*RTTVAR clamped to [kMinRto, kMaxRto]; each
		// unacked retransmission doubles the wait up to kMaxRto. A packet still
		// unacked after kMaxTransmits transmissions marks the connection
		// interrupted (PeerConnectionInterrupted); retransmission continues at
		// kMaxRto, and if the peer stays silent for kInterruptedGrace the
		// connection closes with EOS_CCR_TimedOut. At the test-loss ceiling of
		// 30%, 0.3^16 makes a spurious interruption negligible.
		constexpr std::chrono::milliseconds kInitialRto{50};
		constexpr std::chrono::milliseconds kMinRto{10};
		constexpr std::chrono::milliseconds kMaxRto{500};
		constexpr uint32_t kMaxTransmits = 16;
		constexpr std::chrono::milliseconds kInterruptedGrace{10000};

		// Reconstructs a 64-bit sequence number from its low 32 wire bits: the
		// value with those low bits closest to Ref (the local watermark). Valid
		// as long as sender and receiver stay within 2^31 of each other, which
		// the retransmit window guarantees by orders of magnitude.
		uint64_t Extend32(uint64_t Ref, uint32_t Low)
		{
			uint64_t Candidate = (Ref & ~0xFFFFFFFFull) | Low;
			if (Candidate + 0x80000000ull < Ref)
			{
				return Candidate + 0x100000000ull;
			}
			if (Candidate >= Ref + 0x80000000ull && Candidate >= 0x100000000ull)
			{
				return Candidate - 0x100000000ull;
			}
			return Candidate;
		}

		unsigned ReadLossPermille()
		{
#if defined(_WIN32)
			char Buffer[16] = {};
			size_t Length = 0;
			if (getenv_s(&Length, Buffer, sizeof(Buffer), "EOSEMU_P2P_TESTLOSS") != 0 || Length <= 1)
			{
				return 0;
			}
			int V = std::atoi(Buffer);
#else
			const char* Env = std::getenv("EOSEMU_P2P_TESTLOSS");
			if (Env == nullptr) return 0;
			int V = std::atoi(Env);
#endif
			if (V < 0) V = 0;
			if (V > 1000) V = 1000;
			return static_cast<unsigned>(V);
		}
	}

	P2PInterface::P2PInterface(Platform& Owner) : InterfaceBase(Owner)
	{
		TestLossPermille_ = ReadLossPermille();
		if (TestLossPermille_ > 0)
		{
			EOSEMU_WARN(P2P, "P2P test-loss injector active: dropping ~%u/1000 data+ack datagrams", TestLossPermille_);
		}
	}

	std::string P2PInterface::StreamKey(EOS_ProductUserId Peer, const std::string& Socket, uint8_t Channel, bool Ordered) const
	{
		std::string Key = ConnKey(Peer, Socket);
		Key.push_back('\0');
		Key.push_back(static_cast<char>(Channel));
		Key.push_back(Ordered ? 'O' : 'U');
		return Key;
	}

	void P2PInterface::ClearStreams(EOS_ProductUserId Peer, const std::string& Socket)
	{
		// caller holds Mutex_. Streams are keyed by ConnKey(Peer,Socket) + '\0' +
		// suffix; include the separator so socket "CHAT" cannot match "CHATX".
		std::string Prefix = ConnKey(Peer, Socket);
		Prefix.push_back('\0');
		auto Erase = [&Prefix](auto& Map)
		{
			for (auto It = Map.begin(); It != Map.end();)
			{
				if (It->first.size() >= Prefix.size() && It->first.compare(0, Prefix.size(), Prefix) == 0)
					It = Map.erase(It);
				else
					++It;
			}
		};
		Erase(SendStreams_);
		Erase(RecvStreams_);
	}

	void P2PInterface::RawSend(const net::Endpoint& To, net::MessageType Type, const uint8_t* Data, uint16_t Len)
	{
		if (TestLossPermille_ > 0)
		{
			// Scramble a monotonic counter so drops are spread out, not periodic.
			const uint64_t N = SendCounter_.fetch_add(1, std::memory_order_relaxed);
			uint64_t H = N * 6364136223846793005ull + 1442695040888963407ull;
			H ^= H >> 33;
			if (static_cast<unsigned>(H % 1000u) < TestLossPermille_)
			{
				return; // dropped
			}
		}
		Platform_.Net().SendTo(To, Type, Data, Len);
	}

	bool P2PInterface::InboxHasRoom(uint64_t Bytes) const
	{
		// caller holds Mutex_
		return MaxInboxBytes_ == 0 || InboxBytes_ + Bytes <= MaxInboxBytes_;
	}

	bool P2PInterface::EnqueueInbox(EOS_ProductUserId Peer, const std::string& Socket, uint8_t Channel, std::vector<uint8_t>&& Data)
	{
		// caller holds Mutex_
		const uint64_t Bytes = Data.size();
		if (!InboxHasRoom(Bytes))
		{
			return true; // dropped: incoming queue full
		}
		IncomingPacket Pkt;
		Pkt.Peer = Peer;
		Pkt.SocketName = Socket;
		Pkt.Channel = Channel;
		Pkt.Data = std::move(Data);
		InboxBytes_ += Bytes;
		Inbox_.push_back(std::move(Pkt));
		return false;
	}

	bool P2PInterface::DrainRecvStream(RecvStream& S)
	{
		// caller holds Mutex_. Reliable packets are never dropped on a full
		// inbox -- they wait in S.Buffer (already acked, since they are safely
		// held) until the app drains the inbox; Pump() retries every Tick.
		bool Blocked = false;
		if (S.Ordered)
		{
			while (true)
			{
				auto It = S.Buffer.find(S.NextToDeliver);
				if (It == S.Buffer.end())
				{
					break;
				}
				if (!InboxHasRoom(It->second.size()))
				{
					Blocked = true;
					break;
				}
				EnqueueInbox(S.Peer, S.Socket, S.Channel, std::move(It->second));
				S.Buffer.erase(It);
				++S.NextToDeliver;
			}
		}
		else
		{
			for (auto It = S.Buffer.begin(); It != S.Buffer.end();)
			{
				if (!InboxHasRoom(It->second.size()))
				{
					Blocked = true;
					break;
				}
				EnqueueInbox(S.Peer, S.Socket, S.Channel, std::move(It->second));
				It = S.Buffer.erase(It);
			}
		}
		return Blocked;
	}

	std::chrono::milliseconds P2PInterface::RtoFor(const SendStream& S) const
	{
		if (S.SrttMs < 0.0)
		{
			return kInitialRto;
		}
		const double Rto = S.SrttMs + (4.0 * S.RttVarMs < 1.0 ? 1.0 : 4.0 * S.RttVarMs);
		const auto Ms = std::chrono::milliseconds(static_cast<long long>(Rto + 0.5));
		return Ms < kMinRto ? kMinRto : (Ms > kMaxRto ? kMaxRto : Ms);
	}

	bool P2PInterface::ClearInterrupted(Connection& Conn)
	{
		// caller holds Mutex_
		if (!Conn.bInterrupted)
		{
			return false;
		}
		Conn.bInterrupted = false;
		return true;
	}

	std::string P2PInterface::ConnKey(EOS_ProductUserId Peer, const std::string& Socket) const
	{
		std::string Key = Ids::ProductString(Peer);
		Key.push_back('\0');
		Key += Socket;
		return Key;
	}

	P2PInterface::Connection* P2PInterface::FindConn(EOS_ProductUserId Peer, const std::string& Socket)
	{
		auto It = Connections_.find(ConnKey(Peer, Socket));
		return It != Connections_.end() ? &It->second : nullptr;
	}

	net::Endpoint P2PInterface::EndpointForPeer(EOS_ProductUserId Peer)
	{
		return Platform_.Peers().EndpointFor(Peer);
	}

	void P2PInterface::SendControl(const net::Endpoint& To, net::MessageType Type, EOS_ProductUserId, const std::string& Socket)
	{
		net::ByteWriter W;
		W.Str(Platform_.LocalIdentity().ProductUserIdString());
		W.Str(Socket);
		Platform_.Net().SendTo(To, Type, W.Data().data(), W.Size());
	}

	// ------------------------------------------------------------------------
	// Send / receive
	// ------------------------------------------------------------------------

	EOS_EResult P2PInterface::SendPacket(const EOS_P2P_SendPacketOptions* Options)
	{
		if (Options == nullptr || Options->RemoteUserId == nullptr || Options->SocketId == nullptr
			|| (Options->Data == nullptr && Options->DataLengthBytes > 0))
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		if (!VersionInRange(Options->ApiVersion, EOS_P2P_SENDPACKET_API_LATEST))
		{
			return EOS_EResult::EOS_IncompatibleVersion;
		}
		if (Options->DataLengthBytes > EOS_P2P_MAX_PACKET_SIZE)
		{
			return EOS_EResult::EOS_LimitExceeded;
		}
		const std::string Socket = SocketName(Options->SocketId);
		if (Socket.empty())
		{
			return EOS_EResult::EOS_InvalidParameters;
		}

		// v2 added Reliability (pre-1.8 P2P was always reliable-ordered),
		// v3 added bDisableAutoAcceptConnection.
		const EOS_EPacketReliability Reliability = HasField(Options->ApiVersion, 2)
			? Options->Reliability : EOS_EPacketReliability::EOS_PR_ReliableOrdered;
		const bool AutoAccept = !HasField(Options->ApiVersion, 3)
			|| (Options->bDisableAutoAcceptConnection == EOS_FALSE);

		std::string ToNotifyEstablishedSocket;
		bool FireEstablished = false;
		net::Endpoint Endpoint;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			Endpoint = EndpointForPeer(Options->RemoteUserId);
			Connection* Conn = FindConn(Options->RemoteUserId, Socket);
			const bool NewConn = (Conn == nullptr);
			if (NewConn)
			{
				if (!AutoAccept)
				{
					// Header: with auto-accept disabled and no accepted
					// connection, the caller must AcceptConnection first.
					return EOS_EResult::EOS_NoConnection;
				}
				Connection Fresh;
				Fresh.Peer = Options->RemoteUserId;
				Fresh.SocketName = Socket;
				Fresh.Endpoint = Endpoint;
				Fresh.bAcceptedLocally = true;
				Connections_[ConnKey(Options->RemoteUserId, Socket)] = Fresh;
				Conn = FindConn(Options->RemoteUserId, Socket);
			}
			else if (!Conn->bAcceptedLocally && !AutoAccept)
			{
				return EOS_EResult::EOS_NoConnection;
			}
			else if (!Conn->bAcceptedLocally)
			{
				Conn->bAcceptedLocally = true;
			}
			if (Conn->Endpoint.Valid() == false && Endpoint.Valid())
			{
				Conn->Endpoint = Endpoint;
			}

			// On a brand new outbound connection, announce ourselves so the
			// peer's PeerConnectionRequest can fire.
			if (NewConn && Endpoint.Valid())
			{
				SendControl(Endpoint, net::MessageType::P2PConnectRequest, Options->RemoteUserId, Socket);
			}
		}

		if (!Endpoint.Valid())
		{
			// Peer not discovered yet. The send is accepted (queued) but has
			// nowhere to go; a real SDK would hold it for delayed delivery.
			return EOS_EResult::EOS_Success;
		}

		const uint8_t Mode = static_cast<uint8_t>(Reliability);
		const bool Reliable = Reliability != EOS_EPacketReliability::EOS_PR_UnreliableUnordered;
		const bool Ordered = Reliability == EOS_EPacketReliability::EOS_PR_ReliableOrdered;
		const std::string& LocalPid = Platform_.LocalIdentity().ProductUserIdString();

		// Payload: [sender][socket][channel][mode][seq][data]. Seq is the low 32
		// bits of the stream's 64-bit counter; 0 for unreliable (ignored).
		std::vector<uint8_t> Payload;
		{
			std::unique_lock<std::mutex> Lock(Mutex_, std::defer_lock);
			if (Reliable)
			{
				Lock.lock();
				SendStream& S = SendStreams_[StreamKey(Options->RemoteUserId, Socket, Options->Channel, Ordered)];
				S.Peer = Options->RemoteUserId;
				S.Socket = Socket;
				const uint64_t Seq = S.NextSeq++;

				net::ByteWriter W;
				W.Str(LocalPid);
				W.Str(Socket);
				W.U8(Options->Channel);
				W.U8(Mode);
				W.U32(static_cast<uint32_t>(Seq));
				W.Bytes(Options->Data, Options->DataLengthBytes);
				Payload = W.Data();

				SendStream::Unacked U;
				U.Payload = Payload;
				U.To = Endpoint;
				U.FirstSent = std::chrono::steady_clock::now();
				U.LastSent = U.FirstSent;
				U.Tries = 1;
				S.Pending[Seq] = std::move(U);
			}
			else
			{
				net::ByteWriter W;
				W.Str(LocalPid);
				W.Str(Socket);
				W.U8(Options->Channel);
				W.U8(Mode);
				W.U32(0);
				W.Bytes(Options->Data, Options->DataLengthBytes);
				Payload = W.Data();
			}
		}
		// First transmission (outside the lock). If dropped by the injector, the
		// Tick pump retransmits reliable packets; unreliable ones are simply lost.
		RawSend(Endpoint, net::MessageType::P2PData, Payload.data(), static_cast<uint16_t>(Payload.size()));

		(void)ToNotifyEstablishedSocket;
		(void)FireEstablished;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult P2PInterface::GetNextReceivedPacketSize(const EOS_P2P_GetNextReceivedPacketSizeOptions* Options, uint32_t* OutSize)
	{
		if (Options == nullptr || OutSize == nullptr)
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		if (!VersionInRange(Options->ApiVersion, EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST))
		{
			return EOS_EResult::EOS_IncompatibleVersion;
		}
		// RequestedChannel was added in v2; a v1 caller's struct ends before it.
		const uint8_t* RequestedChannel = HasField(Options->ApiVersion, 2) ? Options->RequestedChannel : nullptr;
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (const auto& Pkt : Inbox_)
		{
			if (RequestedChannel == nullptr || Pkt.Channel == *RequestedChannel)
			{
				*OutSize = static_cast<uint32_t>(Pkt.Data.size());
				return EOS_EResult::EOS_Success;
			}
		}
		return EOS_EResult::EOS_NotFound;
	}

	EOS_EResult P2PInterface::ReceivePacket(const EOS_P2P_ReceivePacketOptions* Options,
		EOS_ProductUserId* OutPeerId, EOS_P2P_SocketId* OutSocketId, uint8_t* OutChannel,
		void* OutData, uint32_t* OutBytesWritten)
	{
		if (Options == nullptr || OutPeerId == nullptr || OutSocketId == nullptr
			|| OutChannel == nullptr || OutData == nullptr || OutBytesWritten == nullptr)
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		if (!VersionInRange(Options->ApiVersion, EOS_P2P_RECEIVEPACKET_API_LATEST))
		{
			return EOS_EResult::EOS_IncompatibleVersion;
		}
		// RequestedChannel was added in v2; a v1 caller's struct ends before it.
		const uint8_t* RequestedChannel = HasField(Options->ApiVersion, 2) ? Options->RequestedChannel : nullptr;
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (auto It = Inbox_.begin(); It != Inbox_.end(); ++It)
		{
			if (RequestedChannel != nullptr && It->Channel != *RequestedChannel)
			{
				continue;
			}
			const uint32_t Copy = It->Data.size() < Options->MaxDataSizeBytes
				? static_cast<uint32_t>(It->Data.size()) : Options->MaxDataSizeBytes;
			if (Copy > 0)
			{
				std::memcpy(OutData, It->Data.data(), Copy);
			}
			*OutPeerId = It->Peer;
			FillSocketId(*OutSocketId, It->SocketName);
			*OutChannel = It->Channel;
			*OutBytesWritten = Copy;

			InboxBytes_ -= It->Data.size();
			Inbox_.erase(It);
			return EOS_EResult::EOS_Success;
		}
		return EOS_EResult::EOS_NotFound;
	}

	// ------------------------------------------------------------------------
	// Inbound datagram handling (receive thread)
	// ------------------------------------------------------------------------

	void P2PInterface::OnDatagram(const net::Endpoint& From, net::MessageType Type, const uint8_t* Payload, uint16_t Len)
	{
		net::ByteReader R(Payload, Len);
		const std::string SenderProduct = R.Str();
		const std::string Socket = R.Str();
		if (!R.Ok() || SenderProduct.empty() || Socket.empty())
		{
			return;
		}
		EOS_ProductUserId Peer = Ids::InternProduct(SenderProduct);

		switch (Type)
		{
		case net::MessageType::P2PConnectRequest:
		{
			bool HasListener = false;
			bool AlreadyAccepted = false;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				if (Connection* Conn = FindConn(Peer, Socket))
				{
					AlreadyAccepted = Conn->bAcceptedLocally;
				}
				for (const auto& E : ConnRequest_)
				{
					if (SocketMatches(E.SocketName, Socket)) { HasListener = true; break; }
				}
				if (!AlreadyAccepted && HasListener)
				{
					Connection& Conn = Connections_[ConnKey(Peer, Socket)];
					Conn.Peer = Peer;
					Conn.SocketName = Socket;
					Conn.Endpoint = From;
				}
			}
			// If already accepted, silently ignore (header semantics). If no
			// listener is bound the request is dropped and the requester is
			// told, so its side reports EOS_CCR_ConnectionIgnored.
			if (!AlreadyAccepted && HasListener)
			{
				FireConnectionRequest(Peer, Socket);
			}
			else if (!AlreadyAccepted)
			{
				SendControl(From, net::MessageType::P2PConnectIgnored, Peer, Socket);
			}
			break;
		}
		case net::MessageType::P2PConnectIgnored:
		{
			// Our earlier connection request found no listener on the peer.
			bool Existed = false;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				Existed = Connections_.erase(ConnKey(Peer, Socket)) > 0;
				ClearStreams(Peer, Socket);
			}
			if (Existed)
			{
				FireConnectionClosed(Peer, Socket, EOS_EConnectionClosedReason::EOS_CCR_ConnectionIgnored);
			}
			break;
		}
		case net::MessageType::P2PConnectAccept:
		{
			bool Established = false;
			bool Reconnected = false;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				Connection& Conn = Connections_[ConnKey(Peer, Socket)];
				Conn.Peer = Peer;
				Conn.SocketName = Socket;
				Conn.Endpoint = From;
				Conn.bAcceptedLocally = true;
				if (!Conn.bEstablished)
				{
					Conn.bEstablished = true;
					Established = true;
				}
				Reconnected = ClearInterrupted(Conn);
			}
			if (Established)
			{
				FireConnectionEstablished(Peer, Socket, EOS_EConnectionEstablishedType::EOS_CET_NewConnection);
			}
			else if (Reconnected)
			{
				FireConnectionEstablished(Peer, Socket, EOS_EConnectionEstablishedType::EOS_CET_Reconnection);
			}
			break;
		}
		case net::MessageType::P2PConnectClose:
		{
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				Connections_.erase(ConnKey(Peer, Socket));
				ClearStreams(Peer, Socket);
			}
			FireConnectionClosed(Peer, Socket, EOS_EConnectionClosedReason::EOS_CCR_ClosedByPeer);
			break;
		}
		case net::MessageType::P2PData:
		{
			const uint8_t Channel = R.U8();
			const uint8_t Mode = R.U8();
			const uint32_t WireSeq = R.U32();
			std::vector<uint8_t> Data = R.Bytes();
			if (!R.Ok())
			{
				return;
			}
			const bool Reliable = Mode != static_cast<uint8_t>(EOS_EPacketReliability::EOS_PR_UnreliableUnordered);
			const bool Ordered = Mode == static_cast<uint8_t>(EOS_EPacketReliability::EOS_PR_ReliableOrdered);

			bool Established = false;
			bool Reconnected = false;
			bool Dropped = false;
			std::vector<uint8_t> AckPayload; // non-empty -> owe an ack to From
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				// Receiving data implies the peer opened a connection to us.
				Connection& Conn = Connections_[ConnKey(Peer, Socket)];
				Conn.Peer = Peer;
				Conn.SocketName = Socket;
				Conn.Endpoint = From;
				if (!Conn.bEstablished)
				{
					Conn.bEstablished = true;
					Established = true;
				}
				Reconnected = ClearInterrupted(Conn);

				if (!Reliable)
				{
					Dropped = EnqueueInbox(Peer, Socket, Channel, std::move(Data));
				}
				else
				{
					RecvStream& S = RecvStreams_[StreamKey(Peer, Socket, Channel, Ordered)];
					S.Peer = Peer;
					S.Socket = Socket;
					S.Channel = Channel;
					S.Ordered = Ordered;
					const uint64_t Seq = Extend32(S.NextExpected, WireSeq);
					const bool Duplicate = Seq < S.NextExpected || S.Received.count(Seq) != 0;
					if (!Duplicate)
					{
						// Buffer first (never dropped -- a buffered packet is as
						// good as delivered for ack purposes), then release what
						// the inbox can take. Ordered releases in seq order;
						// unordered-reliable releases on arrival.
						S.Received.insert(Seq);
						S.Buffer[Seq] = std::move(Data);
						while (S.Received.count(S.NextExpected) != 0)
							++S.NextExpected;
						// Received only needs to track seqs at/above the hole.
						while (!S.Received.empty() && *S.Received.begin() < S.NextExpected)
							S.Received.erase(S.Received.begin());
					}
					Dropped = DrainRecvStream(S) || Dropped;

					// Acknowledge even a duplicate, so a lost ack still recovers.
					const uint64_t AckSeq = S.NextExpected - 1; // highest contiguous received
					uint32_t Sack = 0;
					for (uint32_t i = 0; i < 32; ++i)
						if (S.Received.count(S.NextExpected + 1 + i) != 0) Sack |= (1u << i);
					net::ByteWriter W;
					W.Str(Platform_.LocalIdentity().ProductUserIdString());
					W.Str(Socket);
					W.U8(Channel);
					W.U8(Ordered ? 1 : 0);
					W.U32(static_cast<uint32_t>(AckSeq));
					W.U32(Sack);
					AckPayload = W.Data();
				}
			}
			if (!AckPayload.empty())
			{
				RawSend(From, net::MessageType::P2PAck, AckPayload.data(), static_cast<uint16_t>(AckPayload.size()));
			}
			if (Established)
			{
				FireConnectionEstablished(Peer, Socket, EOS_EConnectionEstablishedType::EOS_CET_NewConnection);
			}
			else if (Reconnected)
			{
				FireConnectionEstablished(Peer, Socket, EOS_EConnectionEstablishedType::EOS_CET_Reconnection);
			}
			if (Dropped)
			{
				// Notify the incoming-queue-full observers so the app can drain.
				for (const auto& E : QueueFull_.Snapshot())
				{
					EOS_P2P_OnIncomingPacketQueueFullCallback Fn = E.Fn;
					void* Cd = E.ClientData;
					uint64_t MaxBytes = MaxInboxBytes_;
					Platform_.Dispatch().Post([Fn, Cd, MaxBytes]
					{
						EOS_P2P_OnIncomingPacketQueueFullInfo Info = {};
						Info.ClientData = Cd;
						Info.PacketQueueMaxSizeBytes = MaxBytes;
						Fn(&Info);
					});
				}
			}
			break;
		}
		case net::MessageType::P2PAck:
		{
			const uint8_t Channel = R.U8();
			const uint8_t OrderedByte = R.U8();
			const uint32_t WireAckSeq = R.U32();
			const uint32_t Sack = R.U32();
			if (!R.Ok())
			{
				return;
			}
			bool Reconnected = false;
			{
				std::lock_guard<std::mutex> Lock(Mutex_);
				// An ack is proof of life for the connection.
				if (Connection* Conn = FindConn(Peer, Socket))
				{
					Reconnected = ClearInterrupted(*Conn);
				}
				auto It = SendStreams_.find(StreamKey(Peer, Socket, Channel, OrderedByte != 0));
				if (It != SendStreams_.end())
				{
					SendStream& S = It->second;
					const uint64_t AckSeq = Extend32(S.NextSeq, WireAckSeq);
					const auto Now = std::chrono::steady_clock::now();
					auto Sample = [&S, Now](const SendStream::Unacked& U)
					{
						// Karn's rule: only never-retransmitted packets give an
						// unambiguous RTT sample.
						if (U.Tries != 1)
						{
							return;
						}
						const double Rtt = std::chrono::duration<double, std::milli>(Now - U.FirstSent).count();
						if (S.SrttMs < 0.0)
						{
							S.SrttMs = Rtt;
							S.RttVarMs = Rtt / 2.0;
						}
						else
						{
							const double Err = S.SrttMs - Rtt;
							S.RttVarMs = 0.75 * S.RttVarMs + 0.25 * (Err < 0 ? -Err : Err);
							S.SrttMs = 0.875 * S.SrttMs + 0.125 * Rtt;
						}
					};
					// Cumulative: everything up to AckSeq is delivered.
					for (auto Pit = S.Pending.begin(); Pit != S.Pending.end() && Pit->first <= AckSeq;)
					{
						Sample(Pit->second);
						Pit = S.Pending.erase(Pit);
					}
					// Selective: bit i marks seq (AckSeq + 2 + i) received past the hole.
					for (uint32_t i = 0; i < 32; ++i)
					{
						if (Sack & (1u << i))
						{
							auto Pit = S.Pending.find(AckSeq + 2 + i);
							if (Pit != S.Pending.end())
							{
								Sample(Pit->second);
								S.Pending.erase(Pit);
							}
						}
					}
				}
			}
			if (Reconnected)
			{
				FireConnectionEstablished(Peer, Socket, EOS_EConnectionEstablishedType::EOS_CET_Reconnection);
			}
			break;
		}
		default:
			break;
		}
	}

	// ------------------------------------------------------------------------
	// Reliable retransmission pump (Tick thread)
	// ------------------------------------------------------------------------

	void P2PInterface::Pump()
	{
		const auto Now = std::chrono::steady_clock::now();
		std::vector<std::pair<net::Endpoint, std::vector<uint8_t>>> ToSend;
		std::vector<std::pair<EOS_ProductUserId, std::string>> Interrupted;
		std::vector<std::pair<EOS_ProductUserId, std::string>> TimedOut;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);

			// Reliable packets deferred on a full inbox: the app may have
			// drained it since, so retry delivery. (The queue-full notification
			// already fired when the packet arrived.)
			for (auto& Rkv : RecvStreams_)
			{
				if (!Rkv.second.Buffer.empty())
				{
					DrainRecvStream(Rkv.second);
				}
			}

			for (auto& Skv : SendStreams_)
			{
				SendStream& S = Skv.second;
				if (S.Pending.empty())
				{
					continue;
				}
				const auto Rto = RtoFor(S);
				for (auto& Pkv : S.Pending)
				{
					SendStream::Unacked& U = Pkv.second;
					// Exponential backoff per unacked retransmission, capped.
					auto Wait = Rto;
					for (uint32_t T = 1; T < U.Tries && Wait < kMaxRto; ++T)
					{
						Wait += Wait;
					}
					if (Wait > kMaxRto)
					{
						Wait = kMaxRto;
					}
					if (Now - U.LastSent < Wait)
					{
						continue;
					}
					if (U.Tries >= kMaxTransmits)
					{
						// The peer has stopped acknowledging. Interrupt the
						// connection once and keep retransmitting at the capped
						// RTO; the grace sweep below closes it if the peer never
						// answers, and an ack/data arrival clears the flag with
						// an EOS_CET_Reconnection notification.
						Connection* Conn = FindConn(S.Peer, S.Socket);
						if (Conn != nullptr && !Conn->bInterrupted)
						{
							Conn->bInterrupted = true;
							Conn->InterruptedAt = Now;
							Interrupted.emplace_back(S.Peer, S.Socket);
						}
					}
					U.LastSent = Now;
					++U.Tries;
					ToSend.emplace_back(U.To, U.Payload);
				}
			}

			// Close connections whose interruption never recovered.
			for (auto It = Connections_.begin(); It != Connections_.end();)
			{
				Connection& Conn = It->second;
				if (Conn.bInterrupted && Now - Conn.InterruptedAt >= kInterruptedGrace)
				{
					TimedOut.emplace_back(Conn.Peer, Conn.SocketName);
					ClearStreams(Conn.Peer, Conn.SocketName);
					It = Connections_.erase(It);
				}
				else
				{
					++It;
				}
			}
		}
		// Retransmit outside the lock (RawSend is a syscall + may itself drop).
		for (auto& T : ToSend)
		{
			RawSend(T.first, net::MessageType::P2PData, T.second.data(), static_cast<uint16_t>(T.second.size()));
		}
		for (auto& I : Interrupted)
		{
			FireConnectionInterrupted(I.first, I.second);
		}
		for (auto& C : TimedOut)
		{
			FireConnectionClosed(C.first, C.second, EOS_EConnectionClosedReason::EOS_CCR_TimedOut);
		}
	}

	// ------------------------------------------------------------------------
	// Notification fan-outs -- snapshot under lock, invoke on the Tick thread.
	// ------------------------------------------------------------------------

	void P2PInterface::FireConnectionRequest(EOS_ProductUserId Remote, const std::string& Socket)
	{
		std::vector<std::pair<EOS_P2P_OnIncomingConnectionRequestCallback, void*>> Targets;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			for (const auto& E : ConnRequest_)
			{
				if (SocketMatches(E.SocketName, Socket))
				{
					Targets.emplace_back(E.Fn, E.ClientData);
				}
			}
		}
		EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
		for (auto& T : Targets)
		{
			auto Fn = T.first; void* Cd = T.second;
			Platform_.Dispatch().Post([Fn, Cd, Local, Remote, Socket]
			{
				EOS_P2P_SocketId Sock; FillSocketId(Sock, Socket);
				EOS_P2P_OnIncomingConnectionRequestInfo Info = {};
				Info.ClientData = Cd;
				Info.LocalUserId = Local;
				Info.RemoteUserId = Remote;
				Info.SocketId = &Sock;
				Fn(&Info);
			});
		}
	}

	void P2PInterface::FireConnectionEstablished(EOS_ProductUserId Remote, const std::string& Socket, EOS_EConnectionEstablishedType EstType)
	{
		std::vector<std::pair<EOS_P2P_OnPeerConnectionEstablishedCallback, void*>> Targets;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			for (const auto& E : ConnEstablished_)
			{
				if (SocketMatches(E.SocketName, Socket))
				{
					Targets.emplace_back(E.Fn, E.ClientData);
				}
			}
		}
		EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
		for (auto& T : Targets)
		{
			auto Fn = T.first; void* Cd = T.second;
			Platform_.Dispatch().Post([Fn, Cd, Local, Remote, Socket, EstType]
			{
				EOS_P2P_SocketId Sock; FillSocketId(Sock, Socket);
				EOS_P2P_OnPeerConnectionEstablishedInfo Info = {};
				Info.ClientData = Cd;
				Info.LocalUserId = Local;
				Info.RemoteUserId = Remote;
				Info.SocketId = &Sock;
				Info.ConnectionType = EstType;
				Info.NetworkType = EOS_ENetworkConnectionType::EOS_NCT_DirectConnection;
				Fn(&Info);
			});
		}
	}

	void P2PInterface::FireConnectionInterrupted(EOS_ProductUserId Remote, const std::string& Socket)
	{
		std::vector<std::pair<EOS_P2P_OnPeerConnectionInterruptedCallback, void*>> Targets;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			for (const auto& E : ConnInterrupted_)
			{
				if (SocketMatches(E.SocketName, Socket))
				{
					Targets.emplace_back(E.Fn, E.ClientData);
				}
			}
		}
		EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
		for (auto& T : Targets)
		{
			auto Fn = T.first; void* Cd = T.second;
			Platform_.Dispatch().Post([Fn, Cd, Local, Remote, Socket]
			{
				EOS_P2P_SocketId Sock; FillSocketId(Sock, Socket);
				EOS_P2P_OnPeerConnectionInterruptedInfo Info = {};
				Info.ClientData = Cd;
				Info.LocalUserId = Local;
				Info.RemoteUserId = Remote;
				Info.SocketId = &Sock;
				Fn(&Info);
			});
		}
	}

	void P2PInterface::FireConnectionClosed(EOS_ProductUserId Remote, const std::string& Socket, EOS_EConnectionClosedReason Reason)
	{
		std::vector<std::pair<EOS_P2P_OnRemoteConnectionClosedCallback, void*>> Targets;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			for (const auto& E : ConnClosed_)
			{
				if (SocketMatches(E.SocketName, Socket))
				{
					Targets.emplace_back(E.Fn, E.ClientData);
				}
			}
		}
		EOS_ProductUserId Local = Platform_.LocalIdentity().ProductUserId();
		for (auto& T : Targets)
		{
			auto Fn = T.first; void* Cd = T.second;
			Platform_.Dispatch().Post([Fn, Cd, Local, Remote, Socket, Reason]
			{
				EOS_P2P_SocketId Sock; FillSocketId(Sock, Socket);
				EOS_P2P_OnRemoteConnectionClosedInfo Info = {};
				Info.ClientData = Cd;
				Info.LocalUserId = Local;
				Info.RemoteUserId = Remote;
				Info.SocketId = &Sock;
				Info.Reason = Reason;
				Fn(&Info);
			});
		}
	}

	// ------------------------------------------------------------------------
	// Accept / close
	// ------------------------------------------------------------------------

	EOS_EResult P2PInterface::AcceptConnection(const EOS_P2P_AcceptConnectionOptions* Options)
	{
		if (Options == nullptr || Options->RemoteUserId == nullptr || Options->SocketId == nullptr)
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		const std::string Socket = SocketName(Options->SocketId);
		net::Endpoint Endpoint;
		bool FireEstablished = false;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			Connection& Conn = Connections_[ConnKey(Options->RemoteUserId, Socket)];
			Conn.Peer = Options->RemoteUserId;
			Conn.SocketName = Socket;
			if (!Conn.Endpoint.Valid())
			{
				Conn.Endpoint = EndpointForPeer(Options->RemoteUserId);
			}
			Conn.bAcceptedLocally = true;
			Endpoint = Conn.Endpoint;
			if (!Conn.bEstablished)
			{
				Conn.bEstablished = true;
				FireEstablished = true;
			}
		}
		// Tell the peer we accepted so their side establishes too.
		if (Endpoint.Valid())
		{
			SendControl(Endpoint, net::MessageType::P2PConnectAccept, Options->RemoteUserId, Socket);
		}
		if (FireEstablished)
		{
			FireConnectionEstablished(Options->RemoteUserId, Socket, EOS_EConnectionEstablishedType::EOS_CET_NewConnection);
		}
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult P2PInterface::CloseConnection(const EOS_P2P_CloseConnectionOptions* Options)
	{
		if (Options == nullptr || Options->RemoteUserId == nullptr)
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		const std::string Socket = SocketName(Options->SocketId);
		net::Endpoint Endpoint;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			if (Connection* Conn = FindConn(Options->RemoteUserId, Socket))
			{
				Endpoint = Conn->Endpoint;
			}
			Connections_.erase(ConnKey(Options->RemoteUserId, Socket));
			ClearStreams(Options->RemoteUserId, Socket);
		}
		if (Endpoint.Valid())
		{
			SendControl(Endpoint, net::MessageType::P2PConnectClose, Options->RemoteUserId, Socket);
		}
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult P2PInterface::CloseConnections(const EOS_P2P_CloseConnectionsOptions* Options)
	{
		if (Options == nullptr || Options->SocketId == nullptr)
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		const std::string Socket = SocketName(Options->SocketId);
		std::vector<std::pair<net::Endpoint, EOS_ProductUserId>> ToClose;
		{
			std::lock_guard<std::mutex> Lock(Mutex_);
			for (auto It = Connections_.begin(); It != Connections_.end();)
			{
				if (It->second.SocketName == Socket)
				{
					ToClose.emplace_back(It->second.Endpoint, It->second.Peer);
					ClearStreams(It->second.Peer, Socket);
					It = Connections_.erase(It);
				}
				else
				{
					++It;
				}
			}
		}
		for (auto& C : ToClose)
		{
			if (C.first.Valid())
			{
				SendControl(C.first, net::MessageType::P2PConnectClose, C.second, Socket);
			}
		}
		return EOS_EResult::EOS_Success;
	}

	// ------------------------------------------------------------------------
	// Notification registration
	// ------------------------------------------------------------------------

	EOS_NotificationId P2PInterface::AddNotifyPeerConnectionRequest(const EOS_P2P_SocketId* SocketId, EOS_P2P_OnIncomingConnectionRequestCallback Cb, void* ClientData)
	{
		if (Cb == nullptr) return EOS_INVALID_NOTIFICATIONID;
		std::lock_guard<std::mutex> Lock(Mutex_);
		const EOS_NotificationId Id = NextNotificationId();
		ConnRequest_.push_back({Id, Cb, ClientData, SocketName(SocketId)});
		return Id;
	}
	void P2PInterface::RemoveNotifyPeerConnectionRequest(EOS_NotificationId Id)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (auto It = ConnRequest_.begin(); It != ConnRequest_.end(); ++It)
			if (It->Id == Id) { ConnRequest_.erase(It); return; }
	}

	EOS_NotificationId P2PInterface::AddNotifyPeerConnectionEstablished(const EOS_P2P_SocketId* SocketId, EOS_P2P_OnPeerConnectionEstablishedCallback Cb, void* ClientData)
	{
		if (Cb == nullptr) return EOS_INVALID_NOTIFICATIONID;
		std::lock_guard<std::mutex> Lock(Mutex_);
		const EOS_NotificationId Id = NextNotificationId();
		ConnEstablished_.push_back({Id, Cb, ClientData, SocketName(SocketId)});
		return Id;
	}
	void P2PInterface::RemoveNotifyPeerConnectionEstablished(EOS_NotificationId Id)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (auto It = ConnEstablished_.begin(); It != ConnEstablished_.end(); ++It)
			if (It->Id == Id) { ConnEstablished_.erase(It); return; }
	}

	EOS_NotificationId P2PInterface::AddNotifyPeerConnectionInterrupted(const EOS_P2P_SocketId* SocketId, EOS_P2P_OnPeerConnectionInterruptedCallback Cb, void* ClientData)
	{
		if (Cb == nullptr) return EOS_INVALID_NOTIFICATIONID;
		std::lock_guard<std::mutex> Lock(Mutex_);
		const EOS_NotificationId Id = NextNotificationId();
		ConnInterrupted_.push_back({Id, Cb, ClientData, SocketName(SocketId)});
		return Id;
	}
	void P2PInterface::RemoveNotifyPeerConnectionInterrupted(EOS_NotificationId Id)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (auto It = ConnInterrupted_.begin(); It != ConnInterrupted_.end(); ++It)
			if (It->Id == Id) { ConnInterrupted_.erase(It); return; }
	}

	EOS_NotificationId P2PInterface::AddNotifyPeerConnectionClosed(const EOS_P2P_SocketId* SocketId, EOS_P2P_OnRemoteConnectionClosedCallback Cb, void* ClientData)
	{
		if (Cb == nullptr) return EOS_INVALID_NOTIFICATIONID;
		std::lock_guard<std::mutex> Lock(Mutex_);
		const EOS_NotificationId Id = NextNotificationId();
		ConnClosed_.push_back({Id, Cb, ClientData, SocketName(SocketId)});
		return Id;
	}
	void P2PInterface::RemoveNotifyPeerConnectionClosed(EOS_NotificationId Id)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (auto It = ConnClosed_.begin(); It != ConnClosed_.end(); ++It)
			if (It->Id == Id) { ConnClosed_.erase(It); return; }
	}

	EOS_NotificationId P2PInterface::AddNotifyIncomingPacketQueueFull(EOS_P2P_OnIncomingPacketQueueFullCallback Cb, void* ClientData)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		return QueueFull_.Add(Cb, ClientData);
	}
	void P2PInterface::RemoveNotifyIncomingPacketQueueFull(EOS_NotificationId Id)
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		QueueFull_.Remove(Id);
	}

	// ------------------------------------------------------------------------
	// NAT / relay / ports / queue config
	// ------------------------------------------------------------------------

	void P2PInterface::QueryNATType(void* ClientData, EOS_P2P_OnQueryNATTypeCompleteCallback Cb)
	{
		if (Cb == nullptr) return;
		Platform_.Dispatch().Post([Cb, ClientData]
		{
			EOS_P2P_OnQueryNATTypeCompleteInfo Info = {};
			Info.ResultCode = EOS_EResult::EOS_Success;
			Info.ClientData = ClientData;
			Info.NATType = EOS_ENATType::EOS_NAT_Open; // LAN: everyone is open
			Cb(&Info);
		});
	}

	EOS_EResult P2PInterface::GetNATType(EOS_ENATType* OutNATType)
	{
		if (OutNATType == nullptr) return EOS_EResult::EOS_InvalidParameters;
		*OutNATType = EOS_ENATType::EOS_NAT_Open;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult P2PInterface::SetRelayControl(const EOS_P2P_SetRelayControlOptions* Options)
	{
		if (Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::lock_guard<std::mutex> Lock(Mutex_);
		RelayControl_ = Options->RelayControl;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult P2PInterface::GetRelayControl(EOS_ERelayControl* OutRelayControl)
	{
		if (OutRelayControl == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::lock_guard<std::mutex> Lock(Mutex_);
		*OutRelayControl = RelayControl_;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult P2PInterface::SetPortRange(const EOS_P2P_SetPortRangeOptions* Options)
	{
		if (Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::lock_guard<std::mutex> Lock(Mutex_);
		PortRangeBase_ = Options->Port;
		PortRangeExtra_ = Options->MaxAdditionalPortsToTry;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult P2PInterface::GetPortRange(uint16_t* OutPort, uint16_t* OutNumAdditionalPorts)
	{
		if (OutPort == nullptr || OutNumAdditionalPorts == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::lock_guard<std::mutex> Lock(Mutex_);
		*OutPort = PortRangeBase_;
		*OutNumAdditionalPorts = PortRangeExtra_;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult P2PInterface::SetPacketQueueSize(const EOS_P2P_SetPacketQueueSizeOptions* Options)
	{
		if (Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::lock_guard<std::mutex> Lock(Mutex_);
		MaxInboxBytes_ = Options->IncomingPacketQueueMaxSizeBytes;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult P2PInterface::GetPacketQueueInfo(EOS_P2P_PacketQueueInfo* OutInfo)
	{
		if (OutInfo == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::lock_guard<std::mutex> Lock(Mutex_);
		OutInfo->IncomingPacketQueueMaxSizeBytes = MaxInboxBytes_;
		OutInfo->IncomingPacketQueueCurrentSizeBytes = InboxBytes_;
		OutInfo->IncomingPacketQueueCurrentPacketCount = Inbox_.size();
		// Outgoing queue = reliable packets sent but not yet acknowledged.
		uint64_t OutBytes = 0, OutCount = 0;
		for (const auto& Skv : SendStreams_)
		{
			for (const auto& Pkv : Skv.second.Pending)
			{
				OutBytes += Pkv.second.Payload.size();
				++OutCount;
			}
		}
		OutInfo->OutgoingPacketQueueMaxSizeBytes = EOS_P2P_MAX_QUEUE_SIZE_UNLIMITED;
		OutInfo->OutgoingPacketQueueCurrentSizeBytes = OutBytes;
		OutInfo->OutgoingPacketQueueCurrentPacketCount = OutCount;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult P2PInterface::ClearPacketQueue(const EOS_P2P_ClearPacketQueueOptions* Options)
	{
		if (Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
		std::lock_guard<std::mutex> Lock(Mutex_);
		if (Options->RemoteUserId == nullptr && Options->SocketId == nullptr)
		{
			Inbox_.clear();
			InboxBytes_ = 0;
			return EOS_EResult::EOS_Success;
		}
		const std::string Socket = SocketName(Options->SocketId);
		for (auto It = Inbox_.begin(); It != Inbox_.end();)
		{
			const bool PeerMatch = (Options->RemoteUserId == nullptr) || It->Peer == Options->RemoteUserId;
			const bool SockMatch = Socket.empty() || It->SocketName == Socket;
			if (PeerMatch && SockMatch)
			{
				InboxBytes_ -= It->Data.size();
				It = Inbox_.erase(It);
			}
			else
			{
				++It;
			}
		}
		return EOS_EResult::EOS_Success;
	}
}

// ----------------------------------------------------------------------------
// Exported entry points.
// ----------------------------------------------------------------------------

using namespace EOSEmu;

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SendPacket(EOS_HP2P Handle, const EOS_P2P_SendPacketOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->SendPacket(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetNextReceivedPacketSize(EOS_HP2P Handle, const EOS_P2P_GetNextReceivedPacketSizeOptions* Options, uint32_t* OutPacketSizeBytes)
{
	auto* I = As<P2PInterface>(Handle);
	return I ? I->GetNextReceivedPacketSize(Options, OutPacketSizeBytes) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_ReceivePacket(EOS_HP2P Handle, const EOS_P2P_ReceivePacketOptions* Options, EOS_ProductUserId* OutPeerId, EOS_P2P_SocketId* OutSocketId, uint8_t* OutChannel, void* OutData, uint32_t* OutBytesWritten)
{
	auto* I = As<P2PInterface>(Handle);
	return I ? I->ReceivePacket(Options, OutPeerId, OutSocketId, OutChannel, OutData, OutBytesWritten) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionRequest(EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionRequestOptions* Options, void* ClientData, EOS_P2P_OnIncomingConnectionRequestCallback ConnectionRequestHandler)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->AddNotifyPeerConnectionRequest(Options ? Options->SocketId : nullptr, ConnectionRequestHandler, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionRequest(EOS_HP2P Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<P2PInterface>(Handle)) I->RemoveNotifyPeerConnectionRequest(NotificationId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionEstablished(EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionEstablishedOptions* Options, void* ClientData, EOS_P2P_OnPeerConnectionEstablishedCallback ConnectionEstablishedHandler)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->AddNotifyPeerConnectionEstablished(Options ? Options->SocketId : nullptr, ConnectionEstablishedHandler, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionEstablished(EOS_HP2P Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<P2PInterface>(Handle)) I->RemoveNotifyPeerConnectionEstablished(NotificationId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionInterrupted(EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionInterruptedOptions* Options, void* ClientData, EOS_P2P_OnPeerConnectionInterruptedCallback ConnectionInterruptedHandler)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->AddNotifyPeerConnectionInterrupted(Options ? Options->SocketId : nullptr, ConnectionInterruptedHandler, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionInterrupted(EOS_HP2P Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<P2PInterface>(Handle)) I->RemoveNotifyPeerConnectionInterrupted(NotificationId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyPeerConnectionClosed(EOS_HP2P Handle, const EOS_P2P_AddNotifyPeerConnectionClosedOptions* Options, void* ClientData, EOS_P2P_OnRemoteConnectionClosedCallback ConnectionClosedHandler)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->AddNotifyPeerConnectionClosed(Options ? Options->SocketId : nullptr, ConnectionClosedHandler, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyPeerConnectionClosed(EOS_HP2P Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<P2PInterface>(Handle)) I->RemoveNotifyPeerConnectionClosed(NotificationId);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_AcceptConnection(EOS_HP2P Handle, const EOS_P2P_AcceptConnectionOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->AcceptConnection(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_CloseConnection(EOS_HP2P Handle, const EOS_P2P_CloseConnectionOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->CloseConnection(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_CloseConnections(EOS_HP2P Handle, const EOS_P2P_CloseConnectionsOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->CloseConnections(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(void) EOS_P2P_QueryNATType(EOS_HP2P Handle, const EOS_P2P_QueryNATTypeOptions* Options, void* ClientData, const EOS_P2P_OnQueryNATTypeCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Options;
	if (auto* I = As<P2PInterface>(Handle)) I->QueryNATType(ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetNATType(EOS_HP2P Handle, const EOS_P2P_GetNATTypeOptions* Options, EOS_ENATType* OutNATType)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<P2PInterface>(Handle);
	return I ? I->GetNATType(OutNATType) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SetRelayControl(EOS_HP2P Handle, const EOS_P2P_SetRelayControlOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->SetRelayControl(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetRelayControl(EOS_HP2P Handle, const EOS_P2P_GetRelayControlOptions* Options, EOS_ERelayControl* OutRelayControl)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<P2PInterface>(Handle);
	return I ? I->GetRelayControl(OutRelayControl) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SetPortRange(EOS_HP2P Handle, const EOS_P2P_SetPortRangeOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->SetPortRange(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetPortRange(EOS_HP2P Handle, const EOS_P2P_GetPortRangeOptions* Options, uint16_t* OutPort, uint16_t* OutNumAdditionalPortsToTry)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<P2PInterface>(Handle);
	return I ? I->GetPortRange(OutPort, OutNumAdditionalPortsToTry) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_SetPacketQueueSize(EOS_HP2P Handle, const EOS_P2P_SetPacketQueueSizeOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->SetPacketQueueSize(Options) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_GetPacketQueueInfo(EOS_HP2P Handle, const EOS_P2P_GetPacketQueueInfoOptions* Options, EOS_P2P_PacketQueueInfo* OutPacketQueueInfo)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<P2PInterface>(Handle);
	return I ? I->GetPacketQueueInfo(OutPacketQueueInfo) : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_P2P_AddNotifyIncomingPacketQueueFull(EOS_HP2P Handle, const EOS_P2P_AddNotifyIncomingPacketQueueFullOptions* Options, void* ClientData, EOS_P2P_OnIncomingPacketQueueFullCallback IncomingPacketQueueFullHandler)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<P2PInterface>(Handle);
	return I ? I->AddNotifyIncomingPacketQueueFull(IncomingPacketQueueFullHandler, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_P2P_RemoveNotifyIncomingPacketQueueFull(EOS_HP2P Handle, EOS_NotificationId NotificationId)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<P2PInterface>(Handle)) I->RemoveNotifyIncomingPacketQueueFull(NotificationId);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_P2P_ClearPacketQueue(EOS_HP2P Handle, const EOS_P2P_ClearPacketQueueOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<P2PInterface>(Handle);
	return I ? I->ClearPacketQueue(Options) : EOS_EResult::EOS_InvalidParameters;
}
