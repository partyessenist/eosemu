#pragma once

//
// Seed hooks for the local-state interfaces.
//
// Stats and Achievements keep process-global in-memory stores keyed by Product
// User ID (see Stats.cpp / Achievements.cpp). These let Platform pre-populate a
// user's store from config ([Stats] name=value, [Achievements] Unlocked=id)
// without exposing the internals. Both are idempotent: SeedStat overwrites,
// SeedUnlockedAchievement is a no-op if the id is already present.
//

#include <cstdint>
#include <string>

namespace EOSEmu
{
	void SeedStat(const std::string& ProductId, const std::string& Name, int32_t Value);
	void SeedUnlockedAchievement(const std::string& ProductId, const std::string& AchievementId);
}
