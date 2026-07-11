#include "net/Lan.h"

#include "core/Logging.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <iphlpapi.h>
	using socklen_t = int;
#	define EOSEMU_CLOSESOCK closesocket
#	define EOSEMU_BADSOCK INVALID_SOCKET
#	define EOSEMU_SOCKERR SOCKET_ERROR
#else
#	include <arpa/inet.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <net/if.h>
#	include <ifaddrs.h>
#	include <unistd.h>
#	include <fcntl.h>
#	include <errno.h>
	using SOCKET = int;
#	define EOSEMU_CLOSESOCK ::close
#	define EOSEMU_BADSOCK (-1)
#	define EOSEMU_SOCKERR (-1)
#endif

#include <algorithm>

namespace EOSEmu
{
	namespace net
	{
		namespace
		{
#if defined(_WIN32)
			// Winsock needs a process-wide startup/teardown. Reference-counted
			// so multiple Node instances (or create/release cycles) are safe.
			struct WinsockGuard
			{
				bool Ok = false;
				WinsockGuard()
				{
					WSADATA Data;
					Ok = (WSAStartup(MAKEWORD(2, 2), &Data) == 0);
				}
				~WinsockGuard()
				{
					if (Ok)
					{
						WSACleanup();
					}
				}
			};
			WinsockGuard& Winsock()
			{
				static WinsockGuard Guard;
				return Guard;
			}
#endif

			SOCKET AsSock(intptr_t H) { return static_cast<SOCKET>(H); }

			// One directed-broadcast address per up IPv4 interface, host byte
			// order, deduplicated. Empty on failure -- the limited broadcast is
			// always sent regardless.
			std::vector<uint32_t> EnumerateDirectedBroadcasts()
			{
				std::vector<uint32_t> Out;
#if defined(_WIN32)
				ULONG Size = 0;
				if (GetAdaptersInfo(nullptr, &Size) == ERROR_BUFFER_OVERFLOW && Size > 0)
				{
					std::vector<uint8_t> Buffer(Size);
					IP_ADAPTER_INFO* Info = reinterpret_cast<IP_ADAPTER_INFO*>(Buffer.data());
					if (GetAdaptersInfo(Info, &Size) == NO_ERROR)
					{
						for (IP_ADAPTER_INFO* A = Info; A != nullptr; A = A->Next)
						{
							for (IP_ADDR_STRING* Ip = &A->IpAddressList; Ip != nullptr; Ip = Ip->Next)
							{
								in_addr AddrN = {}, MaskN = {};
								if (inet_pton(AF_INET, Ip->IpAddress.String, &AddrN) != 1
									|| inet_pton(AF_INET, Ip->IpMask.String, &MaskN) != 1)
								{
									continue;
								}
								const uint32_t Addr = ntohl(AddrN.s_addr);
								const uint32_t Mask = ntohl(MaskN.s_addr);
								if (Addr == 0 || Mask == 0)
								{
									continue; // down / unconfigured adapter
								}
								Out.push_back((Addr & Mask) | ~Mask);
							}
						}
					}
				}
#else
				ifaddrs* List = nullptr;
				if (getifaddrs(&List) == 0)
				{
					for (ifaddrs* I = List; I != nullptr; I = I->ifa_next)
					{
						if (I->ifa_addr == nullptr || I->ifa_addr->sa_family != AF_INET
							|| (I->ifa_flags & IFF_BROADCAST) == 0 || I->ifa_broadaddr == nullptr)
						{
							continue;
						}
						const auto* B = reinterpret_cast<const sockaddr_in*>(I->ifa_broadaddr);
						const uint32_t Addr = ntohl(B->sin_addr.s_addr);
						if (Addr != 0)
						{
							Out.push_back(Addr);
						}
					}
					freeifaddrs(List);
				}
#endif
				std::sort(Out.begin(), Out.end());
				Out.erase(std::unique(Out.begin(), Out.end()), Out.end());
				// The limited broadcast is sent separately; keep the list to
				// genuinely directed addresses.
				Out.erase(std::remove(Out.begin(), Out.end(), uint32_t(INADDR_BROADCAST)), Out.end());
				return Out;
			}
		}

		std::string Endpoint::ToString() const
		{
			char Buf[32];
			std::snprintf(Buf, sizeof(Buf), "%u.%u.%u.%u:%u",
				(Ip >> 24) & 0xFF, (Ip >> 16) & 0xFF, (Ip >> 8) & 0xFF, Ip & 0xFF, Port);
			return Buf;
		}

		Endpoint Endpoint::FromString(const std::string& Text)
		{
			Endpoint E;
			unsigned a, b, c, d, p;
			if (std::sscanf(Text.c_str(), "%u.%u.%u.%u:%u", &a, &b, &c, &d, &p) == 5)
			{
				E.Ip = (a << 24) | (b << 16) | (c << 8) | d;
				E.Port = static_cast<uint16_t>(p);
			}
			return E;
		}

		Node::Node() = default;

		Node::~Node()
		{
			Stop();
		}

		bool Node::Start(uint32_t SenderTag, const Handler& OnDatagram, uint16_t DiscoveryPort)
		{
			if (Running_.load())
			{
				return true;
			}
#if defined(_WIN32)
			if (!Winsock().Ok)
			{
				EOSEMU_ERROR(P2P, "LAN: WSAStartup failed; networking disabled");
				return false;
			}
#endif
			SenderTag_ = SenderTag;
			Handler_ = OnDatagram;
			DiscoveryPort_ = DiscoveryPort != 0 ? DiscoveryPort : kDiscoveryPort;

			// --- Discovery socket: fixed port, shared, broadcast-capable. ---
			SOCKET Disc = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (Disc == EOSEMU_BADSOCK)
			{
				EOSEMU_ERROR(P2P, "LAN: discovery socket() failed");
				return false;
			}
			int Yes = 1;
			::setsockopt(Disc, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&Yes), sizeof(Yes));
			::setsockopt(Disc, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&Yes), sizeof(Yes));

			sockaddr_in DiscAddr = {};
			DiscAddr.sin_family = AF_INET;
			DiscAddr.sin_addr.s_addr = htonl(INADDR_ANY);
			DiscAddr.sin_port = htons(DiscoveryPort_);
			if (::bind(Disc, reinterpret_cast<sockaddr*>(&DiscAddr), sizeof(DiscAddr)) == EOSEMU_SOCKERR)
			{
				EOSEMU_ERROR(P2P, "LAN: discovery bind(%u) failed", DiscoveryPort_);
				EOSEMU_CLOSESOCK(Disc);
				return false;
			}

			// --- Unicast socket: OS-assigned port, per process. ---
			SOCKET Uni = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (Uni == EOSEMU_BADSOCK)
			{
				EOSEMU_ERROR(P2P, "LAN: unicast socket() failed");
				EOSEMU_CLOSESOCK(Disc);
				return false;
			}
			::setsockopt(Uni, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&Yes), sizeof(Yes));
			sockaddr_in UniAddr = {};
			UniAddr.sin_family = AF_INET;
			UniAddr.sin_addr.s_addr = htonl(INADDR_ANY);
			UniAddr.sin_port = htons(0);
			if (::bind(Uni, reinterpret_cast<sockaddr*>(&UniAddr), sizeof(UniAddr)) == EOSEMU_SOCKERR)
			{
				EOSEMU_ERROR(P2P, "LAN: unicast bind failed");
				EOSEMU_CLOSESOCK(Disc);
				EOSEMU_CLOSESOCK(Uni);
				return false;
			}
			sockaddr_in Bound = {};
			socklen_t BoundLen = sizeof(Bound);
			::getsockname(Uni, reinterpret_cast<sockaddr*>(&Bound), &BoundLen);
			UnicastPort_ = ntohs(Bound.sin_port);

			DiscoverySock_ = static_cast<intptr_t>(Disc);
			UnicastSock_ = static_cast<intptr_t>(Uni);
			Running_.store(true);
			Thread_ = std::thread(&Node::ReceiveLoop, this);

			EOSEMU_INFO(P2P, "LAN: up (unicast port %u, discovery %u)", UnicastPort_, DiscoveryPort_);
			return true;
		}

		void Node::Stop()
		{
			if (!Running_.exchange(false))
			{
				return;
			}
			// Closing the sockets unblocks recvfrom in the loop.
			if (DiscoverySock_ != -1)
			{
				EOSEMU_CLOSESOCK(AsSock(DiscoverySock_));
				DiscoverySock_ = -1;
			}
			if (UnicastSock_ != -1)
			{
				EOSEMU_CLOSESOCK(AsSock(UnicastSock_));
				UnicastSock_ = -1;
			}
			if (Thread_.joinable())
			{
				Thread_.join();
			}
		}

		void Node::Frame(MessageType Type, const void* Payload, uint16_t Len, uint8_t* OutBuffer, int& OutLen)
		{
			WireHeader Header = {};
			Header.Magic = kMagic;
			Header.Version = kProtocolVersion;
			Header.Type = static_cast<uint16_t>(Type);
			Header.SenderTag = SenderTag_;
			Header.UnicastPort = UnicastPort_;
			Header.PayloadLen = Len;

			std::memcpy(OutBuffer, &Header, sizeof(Header));
			if (Len > 0 && Payload != nullptr)
			{
				std::memcpy(OutBuffer + sizeof(Header), Payload, Len);
			}
			OutLen = static_cast<int>(sizeof(Header)) + Len;
		}

		void Node::SendTo(const Endpoint& To, MessageType Type, const void* Payload, uint16_t Len)
		{
			if (Len > kMaxDatagram - static_cast<int>(sizeof(WireHeader)))
			{
				EOSEMU_WARN(P2P, "LAN: dropping oversized datagram (type %d, %u bytes > %d cap)",
					static_cast<int>(Type), Len, kMaxDatagram - static_cast<int>(sizeof(WireHeader)));
				return;
			}
			if (!Running_.load() || !To.Valid())
			{
				return;
			}
			uint8_t Buffer[kMaxDatagram];
			int OutLen = 0;
			Frame(Type, Payload, Len, Buffer, OutLen);

			sockaddr_in Addr = {};
			Addr.sin_family = AF_INET;
			Addr.sin_addr.s_addr = htonl(To.Ip);
			Addr.sin_port = htons(To.Port);
			::sendto(AsSock(UnicastSock_), reinterpret_cast<const char*>(Buffer), OutLen, 0,
				reinterpret_cast<sockaddr*>(&Addr), sizeof(Addr));
		}

		void Node::RefreshBroadcastTargets()
		{
			const auto Now = std::chrono::steady_clock::now();
			if (!BroadcastTargets_.empty() || LastTargetRefresh_.time_since_epoch().count() != 0)
			{
				if (Now - LastTargetRefresh_ < std::chrono::seconds(60))
				{
					return;
				}
			}
			LastTargetRefresh_ = Now;
			BroadcastTargets_ = EnumerateDirectedBroadcasts();
		}

		void Node::Broadcast(MessageType Type, const void* Payload, uint16_t Len)
		{
			if (Len > kMaxDatagram - static_cast<int>(sizeof(WireHeader)))
			{
				EOSEMU_WARN(P2P, "LAN: dropping oversized broadcast (type %d, %u bytes > %d cap)",
					static_cast<int>(Type), Len, kMaxDatagram - static_cast<int>(sizeof(WireHeader)));
				return;
			}
			if (!Running_.load())
			{
				return;
			}
			RefreshBroadcastTargets();

			uint8_t Buffer[kMaxDatagram];
			int OutLen = 0;
			Frame(Type, Payload, Len, Buffer, OutLen);

			// The limited broadcast leaves via one interface picked by the
			// routing table; the per-interface directed broadcasts cover the
			// rest of a multi-homed host's subnets. Broadcasts leave from the
			// unicast socket so the source port peers see matches our advertised
			// UnicastPort (belt-and-braces with the header field).
			auto Send = [&](uint32_t IpHostOrder)
			{
				sockaddr_in Addr = {};
				Addr.sin_family = AF_INET;
				Addr.sin_addr.s_addr = htonl(IpHostOrder);
				Addr.sin_port = htons(DiscoveryPort_);
				::sendto(AsSock(UnicastSock_), reinterpret_cast<const char*>(Buffer), OutLen, 0,
					reinterpret_cast<sockaddr*>(&Addr), sizeof(Addr));
			};
			Send(INADDR_BROADCAST);
			for (uint32_t Target : BroadcastTargets_)
			{
				Send(Target);
			}
		}

		void Node::ReceiveLoop()
		{
			SOCKET Disc = AsSock(DiscoverySock_);
			SOCKET Uni = AsSock(UnicastSock_);

			while (Running_.load())
			{
				fd_set Read;
				FD_ZERO(&Read);
				FD_SET(Disc, &Read);
				FD_SET(Uni, &Read);
				SOCKET MaxFd = Disc > Uni ? Disc : Uni;

				timeval Timeout;
				Timeout.tv_sec = 0;
				Timeout.tv_usec = 200000; // 200ms so Stop() is responsive
				int Ready = ::select(static_cast<int>(MaxFd) + 1, &Read, nullptr, nullptr, &Timeout);
				if (Ready <= 0)
				{
					continue;
				}

				for (SOCKET S : {Disc, Uni})
				{
					if (!FD_ISSET(S, &Read))
					{
						continue;
					}
					uint8_t Buffer[kMaxDatagram];
					sockaddr_in From = {};
					socklen_t FromLen = sizeof(From);
					int Got = ::recvfrom(S, reinterpret_cast<char*>(Buffer), sizeof(Buffer), 0,
						reinterpret_cast<sockaddr*>(&From), &FromLen);
					if (Got < static_cast<int>(sizeof(WireHeader)))
					{
						continue;
					}

					WireHeader Header;
					std::memcpy(&Header, Buffer, sizeof(Header));
					if (Header.Magic != kMagic || Header.Version != kProtocolVersion)
					{
						continue;
					}
					// Ignore our own broadcasts (they loop back on the subnet).
					if (Header.SenderTag == SenderTag_)
					{
						continue;
					}
					const uint16_t PayloadLen = Header.PayloadLen;
					if (static_cast<int>(sizeof(WireHeader)) + PayloadLen > Got)
					{
						continue;
					}

					// The sender's direct-payload endpoint: its real source IP
					// plus the unicast port it advertised in the header.
					Endpoint Src;
					Src.Ip = ntohl(From.sin_addr.s_addr);
					Src.Port = Header.UnicastPort;

					if (Handler_)
					{
						Handler_(Src, Header, Buffer + sizeof(WireHeader), PayloadLen);
					}
				}
			}
		}
	}
}
