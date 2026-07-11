#pragma once

//
// LAN peer directory.
//
// Populated from periodic Hello broadcasts. Every remote EOSEmu process that is
// logged in announces its product user id, epic account id, display name and
// direct-payload endpoint; this table caches the most recent announcement per
// peer. It is the single source of truth for "which endpoint reaches this
// Product User ID" (used by P2P) and "who is on the LAN" (used by Friends,
// Presence, UserInfo).
//
// Thread-safe: the receive thread writes, Tick-thread interfaces read.
//

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "eos_common.h"
#include "net/Lan.h"

namespace EOSEmu
{
	// Rich presence a peer announced in its Hello. A pre-extension build only
	// sends the rich text, so everything else keeps these defaults (Online,
	// empty). Replicated verbatim from the peer's EOS_Presence_SetPresence, so
	// whatever signal a game keys "friend is in-game" on (ProductId match, a
	// data record, join info) arrives without us having to interpret it.
	struct PeerPresence
	{
		uint8_t Status = 1; // EOS_Presence_EStatus::EOS_PS_Online
		std::string RichText;
		std::string JoinInfo;
		std::string ProductId;
		std::string ProductVersion;
		std::string ProductName;
		std::vector<std::pair<std::string, std::string>> Data;

		bool operator==(const PeerPresence& O) const
		{
			return Status == O.Status && RichText == O.RichText
				&& JoinInfo == O.JoinInfo && ProductId == O.ProductId
				&& ProductVersion == O.ProductVersion && ProductName == O.ProductName
				&& Data == O.Data;
		}
	};

	struct PeerInfo
	{
		EOS_ProductUserId ProductUserId = nullptr;
		EOS_EpicAccountId EpicAccountId = nullptr;
		std::string DisplayName;
		net::Endpoint Endpoint;
		PeerPresence Presence;
		uint64_t LastSeenTicks = 0;
	};

	class PeerDirectory
	{
	public:
		/// Records or refreshes a peer from a Hello. Interns the ids. Returns
		/// true when the peer is new or its presence content changed, so the
		/// caller can fire OnPresenceChanged observers.
		bool Observe(const std::string& ProductId, const std::string& EpicId,
			const std::string& DisplayName, const PeerPresence& Presence,
			const net::Endpoint& Endpoint, uint64_t NowTicks);

		/// The direct-payload endpoint for a peer, or an invalid Endpoint.
		net::Endpoint EndpointFor(EOS_ProductUserId Product) const;

		/// A copy of everything currently known. Used to build friends lists.
		std::vector<PeerInfo> Snapshot() const;

		/// Look up a single peer by product user id.
		bool Find(EOS_ProductUserId Product, PeerInfo& Out) const;
		bool FindByEpic(EOS_EpicAccountId Epic, PeerInfo& Out) const;

	private:
		mutable std::mutex Mutex_;
		std::unordered_map<EOS_ProductUserId, PeerInfo> Peers_;
	};
}
