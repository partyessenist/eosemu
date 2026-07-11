#pragma once

//
// Local Steam account id (SteamID64).
//
// A game running on Steam -- or on a Steam emulator, whose id is
// user-configurable -- has one real SteamID64, and the real EOS SDK reports it
// back through Connect's ExternalAccountInfo. Fabricating a value there breaks
// the correspondence with the id the game's own Steam layer sees, so we
// recover the real one instead: read live from the steam_api module already
// loaded in this process, or parsed out of the Steam auth session ticket the
// game hands to a Login call. Platform::LocalSteamId owns the resolution
// order; these are the stateless sources.
//

#include <cstdint>

namespace EOSEmu
{
	namespace SteamId
	{
		/// True for a public individual SteamID64 (universe 1, account type 1,
		/// non-zero account number) -- the only shape a logged-in local user has.
		/// Used to reject garbage from a misparsed ticket or a stale interface.
		bool IsIndividualId(uint64_t Id);

		/// SteamID64 embedded in a hex-encoded Steam auth session ticket -- the
		/// EOS_ECT_STEAM_SESSION_TICKET token format (eos_common.h:514 mandates
		/// hex). Returns 0 if the string does not parse as one; an encrypted app
		/// ticket (EOS_ECT_STEAM_APP_TICKET) fails the layout checks by design.
		uint64_t FromSessionTicketHex(const char* Token);

		/// SteamID64 the steam_api module loaded in this process reports for the
		/// current user, or 0 when there is no module / no initialised user. Real
		/// Steam and the common emulators both export the flat C API this probes,
		/// so the answer is whatever id the module itself is configured with.
		uint64_t QueryLoadedSteamApi();
	}
}
