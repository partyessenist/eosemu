#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "interfaces/Interfaces.h"
#include "interfaces/Common.h"

#include "eos_p2p_types.h"
#include "net/Lan.h"

namespace EOSEmu
{
	// P2P mapped onto the LAN transport. Poll-based receive (the app calls
	// GetNextReceivedPacketSize/ReceivePacket every frame), so inbound data is
	// buffered in a locked queue the receive thread fills directly. Connection
	// lifecycle events are the only callbacks and go through the Dispatcher.
	// See CLAUDE.md, "P2P is the easiest mapping".
	class P2PInterface : public InterfaceBase
	{
	public:
		explicit P2PInterface(Platform& Owner);

		EOS_EResult SendPacket(const EOS_P2P_SendPacketOptions* Options);
		EOS_EResult GetNextReceivedPacketSize(const EOS_P2P_GetNextReceivedPacketSizeOptions* Options, uint32_t* OutSize);
		EOS_EResult ReceivePacket(const EOS_P2P_ReceivePacketOptions* Options,
			EOS_ProductUserId* OutPeerId, EOS_P2P_SocketId* OutSocketId, uint8_t* OutChannel,
			void* OutData, uint32_t* OutBytesWritten);

		EOS_NotificationId AddNotifyPeerConnectionRequest(const EOS_P2P_SocketId* SocketId,
			EOS_P2P_OnIncomingConnectionRequestCallback Cb, void* ClientData);
		void RemoveNotifyPeerConnectionRequest(EOS_NotificationId Id);
		EOS_NotificationId AddNotifyPeerConnectionEstablished(const EOS_P2P_SocketId* SocketId,
			EOS_P2P_OnPeerConnectionEstablishedCallback Cb, void* ClientData);
		void RemoveNotifyPeerConnectionEstablished(EOS_NotificationId Id);
		EOS_NotificationId AddNotifyPeerConnectionInterrupted(const EOS_P2P_SocketId* SocketId,
			EOS_P2P_OnPeerConnectionInterruptedCallback Cb, void* ClientData);
		void RemoveNotifyPeerConnectionInterrupted(EOS_NotificationId Id);
		EOS_NotificationId AddNotifyPeerConnectionClosed(const EOS_P2P_SocketId* SocketId,
			EOS_P2P_OnRemoteConnectionClosedCallback Cb, void* ClientData);
		void RemoveNotifyPeerConnectionClosed(EOS_NotificationId Id);

		EOS_EResult AcceptConnection(const EOS_P2P_AcceptConnectionOptions* Options);
		EOS_EResult CloseConnection(const EOS_P2P_CloseConnectionOptions* Options);
		EOS_EResult CloseConnections(const EOS_P2P_CloseConnectionsOptions* Options);

		void QueryNATType(void* ClientData, EOS_P2P_OnQueryNATTypeCompleteCallback Cb);
		EOS_EResult GetNATType(EOS_ENATType* OutNATType);
		EOS_EResult SetRelayControl(const EOS_P2P_SetRelayControlOptions* Options);
		EOS_EResult GetRelayControl(EOS_ERelayControl* OutRelayControl);
		EOS_EResult SetPortRange(const EOS_P2P_SetPortRangeOptions* Options);
		EOS_EResult GetPortRange(uint16_t* OutPort, uint16_t* OutNumAdditionalPorts);
		EOS_EResult SetPacketQueueSize(const EOS_P2P_SetPacketQueueSizeOptions* Options);
		EOS_EResult GetPacketQueueInfo(EOS_P2P_PacketQueueInfo* OutInfo);
		EOS_NotificationId AddNotifyIncomingPacketQueueFull(EOS_P2P_OnIncomingPacketQueueFullCallback Cb, void* ClientData);
		void RemoveNotifyIncomingPacketQueueFull(EOS_NotificationId Id);
		EOS_EResult ClearPacketQueue(const EOS_P2P_ClearPacketQueueOptions* Options);

		// Called from the LAN receive thread for P2P-typed datagrams.
		void OnDatagram(const net::Endpoint& From, net::MessageType Type, const uint8_t* Payload, uint16_t Len);

		// Retransmits unacknowledged reliable packets past their RTO. Called once
		// per EOS_Platform_Tick (on the caller's thread) -- it only sends
		// datagrams, never invokes a consumer callback.
		void Pump();

	private:
		struct Connection
		{
			EOS_ProductUserId Peer = nullptr;
			std::string SocketName;
			net::Endpoint Endpoint;
			bool bAcceptedLocally = false; // we called AcceptConnection / auto-accepted
			bool bEstablished = false;     // established notification already fired
			// Retransmits exhausted; PeerConnectionInterrupted has fired and the
			// grace timer is running. Cleared (with an EOS_CET_Reconnection
			// established notification) if the peer answers again.
			bool bInterrupted = false;
			std::chrono::steady_clock::time_point InterruptedAt;
		};

		struct IncomingPacket
		{
			EOS_ProductUserId Peer;
			std::string SocketName;
			uint8_t Channel;
			std::vector<uint8_t> Data;
		};

		// A socket-filtered notification registration. SocketName empty means
		// "all sockets".
		template <typename FnPtr>
		struct FilteredEntry
		{
			EOS_NotificationId Id;
			FnPtr Fn;
			void* ClientData;
			std::string SocketName;
		};

		// --- reliability layer ---------------------------------------------
		// A reliable packet carries a per-(peer,socket,channel,ordered) sequence
		// number. The receiver acknowledges (cumulative high-water + a 32-bit
		// selective bitmask); the sender retransmits unacked seqs on the Tick
		// pump with an RTT-estimated, backed-off RTO. Ordered streams release to
		// the inbox only in seq order; unordered-reliable deliver on arrival.
		// Unreliable packets carry no seq and are not tracked. Sequence numbers
		// are 64-bit internally and 32-bit on the wire; both sides extend the
		// wire value against their local watermark, so a stream survives a 2^32
		// wrap. See CLAUDE.md, "reliability is per-packet, above the channel demux".
		struct SendStream
		{
			EOS_ProductUserId Peer = nullptr; // for interrupt/close bookkeeping
			std::string Socket;
			uint64_t NextSeq = 1; // seq 0 is reserved ("nothing acked")
			struct Unacked
			{
				std::vector<uint8_t> Payload; // full framed P2PData payload, ready to resend
				net::Endpoint To;
				std::chrono::steady_clock::time_point FirstSent;
				std::chrono::steady_clock::time_point LastSent;
				uint32_t Tries = 0;
			};
			std::map<uint64_t, Unacked> Pending; // ordered by seq
			// RTT estimation (RFC 6298 flavoured; Karn's rule: only packets acked
			// on their first transmission are sampled). SrttMs < 0 = no sample.
			double SrttMs = -1.0;
			double RttVarMs = 0.0;
		};
		struct RecvStream
		{
			EOS_ProductUserId Peer = nullptr; // delivery metadata for deferred drains
			std::string Socket;
			uint8_t Channel = 0;
			bool Ordered = false;
			uint64_t NextExpected = 1;                          // lowest seq not yet received (ack watermark)
			uint64_t NextToDeliver = 1;                         // ordered: lowest seq not yet released to the inbox
			std::map<uint64_t, std::vector<uint8_t>> Buffer;    // received but not yet delivered
			std::set<uint64_t> Received;                        // dedup + SACK; pruned below NextExpected
		};

		std::string StreamKey(EOS_ProductUserId Peer, const std::string& Socket, uint8_t Channel, bool Ordered) const;
		void ClearStreams(EOS_ProductUserId Peer, const std::string& Socket); // caller holds Mutex_
		// True if the app-visible inbox can take Bytes more. Caller holds Mutex_.
		bool InboxHasRoom(uint64_t Bytes) const;
		// Enqueues a delivered packet into the app-visible inbox. Caller holds
		// Mutex_. Returns true if it was dropped (incoming queue full).
		bool EnqueueInbox(EOS_ProductUserId Peer, const std::string& Socket, uint8_t Channel, std::vector<uint8_t>&& Data);
		// Moves buffered reliable packets into the inbox as room allows. Caller
		// holds Mutex_. Returns true if delivery is blocked on a full inbox.
		bool DrainRecvStream(RecvStream& S);
		// The current retransmission timeout for a stream, before backoff.
		std::chrono::milliseconds RtoFor(const SendStream& S) const;
		// Sends a framed datagram, subject to the test-only loss injector.
		void RawSend(const net::Endpoint& To, net::MessageType Type, const uint8_t* Data, uint16_t Len);

		std::string ConnKey(EOS_ProductUserId Peer, const std::string& Socket) const;
		Connection* FindConn(EOS_ProductUserId Peer, const std::string& Socket);
		net::Endpoint EndpointForPeer(EOS_ProductUserId Peer);
		void SendControl(const net::Endpoint& To, net::MessageType Type, EOS_ProductUserId Peer, const std::string& Socket);

		// Dispatch-thread notification fan-outs (called from OnDatagram via the
		// Dispatcher, so they touch consumer callbacks on the Tick thread).
		void FireConnectionRequest(EOS_ProductUserId Remote, const std::string& Socket);
		void FireConnectionEstablished(EOS_ProductUserId Remote, const std::string& Socket, EOS_EConnectionEstablishedType Type);
		void FireConnectionInterrupted(EOS_ProductUserId Remote, const std::string& Socket);
		void FireConnectionClosed(EOS_ProductUserId Remote, const std::string& Socket, EOS_EConnectionClosedReason Reason);
		// Clears an interrupted flag when the peer answers again. Caller holds
		// Mutex_ and, when this returns true, fires the EOS_CET_Reconnection
		// established notification after releasing it (the Fire* helpers take
		// Mutex_ themselves).
		bool ClearInterrupted(Connection& Conn);

		std::mutex Mutex_; // guards Inbox_/InboxBytes_ (touched by receive thread)

		std::deque<IncomingPacket> Inbox_;
		// Atomic: read on the receive thread's queue-full fan-out without Mutex_.
		std::atomic<uint64_t> MaxInboxBytes_{0}; // 0 == unlimited (EOS_P2P_MAX_QUEUE_SIZE_UNLIMITED)
		uint64_t InboxBytes_ = 0;

		std::unordered_map<std::string, Connection> Connections_;
		std::unordered_map<std::string, SendStream> SendStreams_;
		std::unordered_map<std::string, RecvStream> RecvStreams_;

		// Test-only induced packet loss for the reliable/unreliable data path
		// (EOSEMU_P2P_TESTLOSS = drop probability in per-mille, 0..1000). Control
		// messages are never dropped, so connection setup stays deterministic.
		unsigned TestLossPermille_ = 0;
		std::atomic<uint64_t> SendCounter_{0};

		std::vector<FilteredEntry<EOS_P2P_OnIncomingConnectionRequestCallback>> ConnRequest_;
		std::vector<FilteredEntry<EOS_P2P_OnPeerConnectionEstablishedCallback>> ConnEstablished_;
		std::vector<FilteredEntry<EOS_P2P_OnRemoteConnectionClosedCallback>> ConnClosed_;
		std::vector<FilteredEntry<EOS_P2P_OnPeerConnectionInterruptedCallback>> ConnInterrupted_;
		NotifyRegistry<EOS_P2P_OnIncomingPacketQueueFullCallback> QueueFull_;

		EOS_ERelayControl RelayControl_ = EOS_ERelayControl::EOS_RC_AllowRelays;
		uint16_t PortRangeBase_ = 7777;
		uint16_t PortRangeExtra_ = 99;
	};
}
