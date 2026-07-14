#pragma once

//
// LAN wire protocol.
//
// Every datagram EOSEmu sends starts with WireHeader. The protocol is versioned
// from day one (CLAUDE.md, "Keep the discovery protocol versioned") so that a
// future format change is a clean rejection rather than a misparse. Payload
// encoding past the header is per-message and defined by each interface.
//

#include <cstdint>

namespace EOSEmu
{
	namespace net
	{
		// "EOSE" little-endian. Distinguishes our traffic from stray datagrams
		// that happen to land on our ports.
		constexpr uint32_t kMagic = 0x45534F45u;

		// Bump on any incompatible change to a message payload layout.
		constexpr uint16_t kProtocolVersion = 1;

		// Fixed UDP port for broadcast discovery/announcement. Direct payloads
		// go to per-process ephemeral ports advertised within announcements.
		constexpr uint16_t kDiscoveryPort = 47100;

		enum class MessageType : uint16_t
		{
			// Discovery: periodic "here I am" broadcast carrying identity, the
			// sender's unicast port, and (as an additive trailing extension)
			// the sender's rich presence: status, join info, product identity
			// and data records. Old readers stop after the base fields.
			Hello = 1,

			// Graceful departure: broadcast once from EOS_Platform_Release so
			// receivers drop the sender from their peer directory immediately
			// rather than waiting out the Hello liveness timeout. Best-effort --
			// a crash or kill sends none, so the timeout sweep in Platform::Tick
			// stays the reliable backstop. Payload: the sender's product user id.
			Goodbye = 2,

			// P2P (eos_p2p): direct payloads and connection lifecycle. These
			// are unicast to the peer's advertised port. P2PAck carries the
			// cumulative + selective acknowledgement for the reliable layer.
			// P2PConnectIgnored tells a requester that no connection-request
			// listener was bound for the socket (-> EOS_CCR_ConnectionIgnored).
			P2PData = 10,
			P2PConnectRequest = 11,
			P2PConnectAccept = 12,
			P2PConnectClose = 13,
			P2PAck = 14,
			P2PConnectIgnored = 15,

			// Lobby (eos_lobby): broadcast announcements plus unicast state
			// replication to members.
			LobbyAnnounce = 20,
			LobbyUpdate = 21,
			LobbyJoinRequest = 22,
			LobbyJoinResponse = 23,
			LobbyLeave = 24,
			LobbyInvite = 25,

			// Sessions (eos_sessions): broadcast announcements answering
			// searches. Sessions do not replicate member state.
			SessionAnnounce = 30,
			SessionWithdraw = 31,
			SessionInvite = 32,

			// Custom invites (eos_custominvites): application-defined payloads.
			CustomInvite = 40,
		};

#pragma pack(push, 1)
		struct WireHeader
		{
			uint32_t Magic;
			uint16_t Version;
			uint16_t Type;
			// A stable per-sender token (low bits of the product user id hash)
			// so a receiver can ignore its own broadcasts.
			uint32_t SenderTag;
			uint16_t UnicastPort; // sender's direct-payload port
			uint16_t PayloadLen;
		};
#pragma pack(pop)

		static_assert(sizeof(WireHeader) == 16, "WireHeader must stay a fixed 16 bytes");

		// Largest datagram we ever emit/receive. P2P payloads stay small
		// (EOS_P2P_MAX_PACKET_SIZE is 1170), but lobby/session *announces* carry
		// the full replicated record -- one shipped game's coop lobby alone sets 65
		// attributes (~2 KB serialized). A 1400-byte cap silently dropped those
		// announces, so a joiner never learned the host's game lobby and an
		// ID-fetch failed with NotFound. Size for the largest plausible lobby
		// (64 members + 64 attributes); UDP fragments this across a LAN/loopback
		// fine. If fragment loss ever bites on real hardware, fragment announces
		// at the application layer rather than shrinking this again.
		constexpr int kMaxDatagram = 16384;
	}
}
