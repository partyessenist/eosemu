#pragma once

//
// LAN transport node.
//
// Owns two UDP sockets: a broadcast socket bound to the fixed discovery port
// (shared across processes via SO_REUSEADDR) for announcements, and a unicast
// socket on a per-process ephemeral port for direct payloads. A background
// thread receives on both and hands each datagram to a single handler.
//
// Threading contract: the handler runs on the receive thread. It must be quick
// and thread-safe -- either drop the payload into its own locked queue (P2P
// data, which the app polls) or post a closure to the platform Dispatcher so
// the real work happens on the caller's Tick thread (lifecycle/notifications).
// EOSEmu never invokes a consumer callback from the receive thread.
//

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "net/Wire.h"

namespace EOSEmu
{
	namespace net
	{
		/// An IPv4 host:port, the addressing unit for direct payloads.
		struct Endpoint
		{
			uint32_t Ip = 0;   // host byte order
			uint16_t Port = 0; // host byte order

			bool operator==(const Endpoint& Other) const
			{
				return Ip == Other.Ip && Port == Other.Port;
			}

			bool Valid() const { return Ip != 0 && Port != 0; }

			/// "a.b.c.d:port" -- the form carried in lobby/session host
			/// addresses so a searcher can reach the host directly.
			std::string ToString() const;

			/// Parses "a.b.c.d:port"; returns an invalid Endpoint on failure.
			static Endpoint FromString(const std::string& Text);
		};

		struct EndpointHash
		{
			size_t operator()(const Endpoint& E) const
			{
				return (static_cast<size_t>(E.Ip) << 16) ^ E.Port;
			}
		};

		class Node
		{
		public:
			// (source endpoint, header, payload bytes, payload length)
			using Handler = std::function<void(const Endpoint&, const WireHeader&, const uint8_t*, uint16_t)>;

			Node();
			~Node();

			/// Binds sockets and starts the receive thread. Idempotent-safe:
			/// returns false and logs if the transport could not come up, in
			/// which case the emulator still runs, just without LAN peers.
			/// DiscoveryPort 0 uses the default (net::kDiscoveryPort); a config
			/// override lets two isolated LAN groups coexist on one subnet.
			bool Start(uint32_t SenderTag, const Handler& OnDatagram, uint16_t DiscoveryPort = 0);
			void Stop();

			bool Running() const { return Running_.load(); }

			/// The per-process port peers should target for direct payloads.
			uint16_t UnicastPort() const { return UnicastPort_; }

			/// Sends a framed message directly to one peer.
			void SendTo(const Endpoint& To, MessageType Type, const void* Payload, uint16_t Len);

			/// Broadcasts a framed message to the discovery port: once to the
			/// limited broadcast address and once per interface's directed
			/// broadcast address, so multi-homed hosts reach every subnet they
			/// sit on (the limited broadcast leaves via one interface only).
			void Broadcast(MessageType Type, const void* Payload, uint16_t Len);

		private:
			void ReceiveLoop();
			void Frame(MessageType Type, const void* Payload, uint16_t Len, uint8_t* OutBuffer, int& OutLen);
			// Re-enumerates interface directed-broadcast addresses if the cached
			// list is stale. Only called from Start/Broadcast (game thread).
			void RefreshBroadcastTargets();

			std::atomic<bool> Running_{false};
			std::thread Thread_;
			uint32_t SenderTag_ = 0;
			Handler Handler_;

			// Platform socket handles kept as intptr_t so this header stays free
			// of <winsock2.h>. -1 means unset on both platforms.
			intptr_t DiscoverySock_ = -1;
			intptr_t UnicastSock_ = -1;
			uint16_t UnicastPort_ = 0;
			uint16_t DiscoveryPort_ = kDiscoveryPort;

			// Directed broadcast addresses (host byte order), one per up
			// interface, deduplicated. INADDR_BROADCAST is sent in addition.
			std::vector<uint32_t> BroadcastTargets_;
			std::chrono::steady_clock::time_point LastTargetRefresh_;
		};
	}
}
