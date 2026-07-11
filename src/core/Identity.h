#pragma once

//
// Local user identity.
//
// Auth and Connect are shimmed to always succeed, so we manufacture the IDs
// they would have returned. They must be deterministic (stable across restarts,
// so a peer that saw us yesterday still recognises us) and distinct per machine
// on the LAN. We derive them from the machine name and the OS user profile.
// See CLAUDE.md, "Shim to success".
//

#include <string>

#include "eos_common.h"

namespace EOSEmu
{
	class Config;

	class Identity
	{
	public:
		// Config (if any) may override the derived display name, profile salt, and
		// the Epic/Product ID strings. A null Config reproduces the derived-only
		// behaviour. See [Identity] in eosemu.ini / HANDOFF.md Task 1b.
		explicit Identity(const Config* Cfg = nullptr);

		EOS_EpicAccountId EpicAccountId() const { return EpicId_; }
		EOS_ProductUserId ProductUserId() const { return ProductId_; }

		const std::string& EpicAccountIdString() const { return EpicIdStr_; }
		const std::string& ProductUserIdString() const { return ProductIdStr_; }

		/// Human-readable name shown in friends lists and presence.
		const std::string& DisplayName() const { return DisplayName_; }

	private:
		std::string EpicIdStr_;
		std::string ProductIdStr_;
		std::string DisplayName_;
		EOS_EpicAccountId EpicId_ = nullptr;
		EOS_ProductUserId ProductId_ = nullptr;
	};

	/// 32-character lowercase-hex identifier derived from `Seed`, matching the
	/// shape of a real EOS ID (EOS_*ID_MAX_LENGTH == 32).
	std::string DeriveIdHex(const std::string& Seed);
}
