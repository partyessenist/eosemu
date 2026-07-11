#include "core/Peers.h"

#include "core/Ids.h"

namespace EOSEmu
{
	bool PeerDirectory::Observe(const std::string& ProductId, const std::string& EpicId,
		const std::string& DisplayName, const PeerPresence& Presence,
		const net::Endpoint& Endpoint, uint64_t NowTicks)
	{
		if (ProductId.empty())
		{
			return false;
		}
		EOS_ProductUserId Product = Ids::InternProduct(ProductId);
		EOS_EpicAccountId Epic = EpicId.empty() ? nullptr : Ids::InternEpic(EpicId);

		std::lock_guard<std::mutex> Lock(Mutex_);
		const auto It = Peers_.find(Product);
		const bool PresenceChanged = (It == Peers_.end()) || !(It->second.Presence == Presence);
		PeerInfo& Info = Peers_[Product];
		Info.ProductUserId = Product;
		Info.EpicAccountId = Epic;
		Info.DisplayName = DisplayName;
		Info.Presence = Presence;
		Info.Endpoint = Endpoint;
		Info.LastSeenTicks = NowTicks;
		return PresenceChanged;
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
