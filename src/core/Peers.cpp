#include "core/Peers.h"

#include "core/Ids.h"

namespace EOSEmu
{
	bool PeerDirectory::Observe(const std::string& ProductId, const std::string& EpicId,
		const std::string& DisplayName, const PeerPresence& Presence,
		const net::Endpoint& Endpoint)
	{
		if (ProductId.empty())
		{
			return false;
		}
		EOS_ProductUserId Product = Ids::InternProduct(ProductId);
		EOS_EpicAccountId Epic = EpicId.empty() ? nullptr : Ids::InternEpic(EpicId);

		std::lock_guard<std::mutex> Lock(Mutex_);
		const auto It = Peers_.find(Product);
		// Notify on a new peer, changed presence content, or a stale->online
		// flip (the peer's rich text may be byte-identical to before it went
		// away, but "back online" is still a presence change the game must see).
		const bool CameBackOnline = (It != Peers_.end()) && !It->second.Online;
		const bool PresenceChanged = (It == Peers_.end())
			|| !(It->second.Presence == Presence) || CameBackOnline;
		PeerInfo& Info = Peers_[Product];
		Info.ProductUserId = Product;
		Info.EpicAccountId = Epic;
		Info.DisplayName = DisplayName;
		Info.Presence = Presence;
		Info.Endpoint = Endpoint;
		Info.LastSeen = std::chrono::steady_clock::now();
		Info.Online = true;
		return PresenceChanged;
	}

	std::vector<PeerInfo> PeerDirectory::MarkStale(std::chrono::steady_clock::duration Timeout)
	{
		const auto Now = std::chrono::steady_clock::now();
		std::vector<PeerInfo> Transitioned;
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (auto& Kv : Peers_)
		{
			PeerInfo& Info = Kv.second;
			if (Info.Online && Now - Info.LastSeen > Timeout)
			{
				Info.Online = false;
				Transitioned.push_back(Info);
			}
		}
		return Transitioned;
	}

	bool PeerDirectory::MarkOffline(const std::string& ProductId, PeerInfo& Out)
	{
		if (ProductId.empty())
		{
			return false;
		}
		EOS_ProductUserId Product = Ids::InternProduct(ProductId);
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = Peers_.find(Product);
		if (It == Peers_.end() || !It->second.Online)
		{
			return false;
		}
		It->second.Online = false;
		Out = It->second;
		return true;
	}

	net::Endpoint PeerDirectory::EndpointFor(EOS_ProductUserId Product) const
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = Peers_.find(Product);
		return It != Peers_.end() ? It->second.Endpoint : net::Endpoint{};
	}

	std::vector<PeerInfo> PeerDirectory::Snapshot() const
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		std::vector<PeerInfo> Out;
		Out.reserve(Peers_.size());
		for (const auto& Kv : Peers_)
		{
			Out.push_back(Kv.second);
		}
		return Out;
	}

	bool PeerDirectory::Find(EOS_ProductUserId Product, PeerInfo& Out) const
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		auto It = Peers_.find(Product);
		if (It == Peers_.end())
		{
			return false;
		}
		Out = It->second;
		return true;
	}

	bool PeerDirectory::FindByEpic(EOS_EpicAccountId Epic, PeerInfo& Out) const
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		for (const auto& Kv : Peers_)
		{
			if (Kv.second.EpicAccountId == Epic)
			{
				Out = Kv.second;
				return true;
			}
		}
		return false;
	}
}
