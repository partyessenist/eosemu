#pragma once

//
// Ecom interface -- entitlements and ownership served from config.
//
// EOSEmu models no storefront backend, but a game frequently gates content on
// ownership/entitlement checks. Those we can satisfy locally: the [Ecom]
// section of eosemu.ini declares owned catalog items and granted entitlements,
// and this interface answers QueryOwnership / QueryEntitlements / the entitlement
// Copy* getters from that declaration. Offers, catalog browsing, checkout and
// transactions remain inert stubs (no backend to model) -- see Ecom.cpp.
//
// The config is parsed lazily on first use so that a [Ecom] block supplied via
// the CacheDirectory eosemu.ini (loaded after this object is constructed) still
// takes effect. See HANDOFF.md Task 1c.
//

#include <mutex>
#include <string>
#include <vector>

#include "interfaces/Interfaces.h"

namespace EOSEmu
{
	class EcomInterface : public InterfaceBase
	{
	public:
		explicit EcomInterface(Platform& Owner) : InterfaceBase(Owner) {}

		struct Entitlement
		{
			std::string Name;          // EntitlementName (grouping key)
			std::string CatalogItemId; // item the entitlement was granted for
			std::string EntitlementId; // unique id; defaults to Name
			bool Redeemed = false;
		};

		// A catalog item is owned if it was declared owned or an entitlement was
		// granted for it.
		bool IsOwned(const std::string& CatalogItemId);

		// Snapshot of all configured entitlements (respecting redeemed state, which
		// RedeemEntitlements mutates in memory).
		std::vector<Entitlement> Entitlements();
		std::vector<std::string> OwnedItems();

		// Marks the given entitlement ids redeemed in memory and records them as
		// the "last redeemed" set. Returns how many matched.
		uint32_t Redeem(const std::vector<std::string>& EntitlementIds);
		std::vector<std::string> LastRedeemed();

	private:
		void EnsureLoaded();

		std::mutex Mutex_;
		bool Loaded_ = false;
		std::vector<Entitlement> Entitlements_;
		std::vector<std::string> OwnedItems_;
		std::vector<std::string> LastRedeemed_;
	};
}
