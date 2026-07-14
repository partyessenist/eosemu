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

#include <chrono>
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
		// Wall-clock arrival of the peer's most recent Hello. Drives liveness
		// expiry (see MarkStale). Wall-clock, not a tick count, so the timeout is
		// independent of the game's frame rate.
		std::chrono::steady_clock::time_point LastSeen;
		// False once the peer stops announcing (timeout) or sends a Goodbye. The
		// peer stays in the directory -- it is still a friend -- but Presence
		// reports it EOS_PS_Offline until a fresh Hello brings it back online.
		bool Online = true;
	};

	class PeerDirectory
	{
	public:
		/// Records or refreshes a peer from a Hello, stamping LastSeen and marking
		/// it Online. Interns the ids. Returns true when the caller should fire
		/// OnPresenceChanged observers: the peer is new, its presence content
		/// changed, or it just came back online after being marked stale.
		bool Observe(const std::string& ProductId, const std::string& EpicId,
			const std::string& DisplayName, const PeerPresence& Presence,
			const net::Endpoint& Endpoint);

		/// Flags every peer whose last Hello is older than Timeout as offline and
		/// returns those that transitioned this call (were Online, now aren't),
		/// so the caller announces each once. Peers stay in the directory -- they
		/// remain friends and reappear online on their next Hello. Called per Tick.
		std::vector<PeerInfo> MarkStale(std::chrono::steady_clock::duration Timeout);

		/// Flags a single peer offline in response to a graceful Goodbye. Returns
		/// true and fills Out only on the Online->offline transition, so the
		/// caller announces it once; false if unknown or already offline.
		bool MarkOffline(const std::string& ProductId, PeerInfo& Out);

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
