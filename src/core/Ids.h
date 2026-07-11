#pragma once

//
// Opaque account IDs.
//
// EOS_EpicAccountId and EOS_ProductUserId are opaque *pointers*, but consumers
// compare them with raw == and order them with < (Samples AccountHelpers.h:98,
// Friends.cpp:95). So there must be exactly one canonical pointer per distinct
// ID string, stable for the whole process lifetime and never freed:
// FromString("X") twice returns the same pointer. This intern table is a
// correctness requirement, not an optimisation. See CLAUDE.md, "Opaque IDs".
//

#include <string>

#include "eos_common.h"

namespace EOSEmu
{
	namespace Ids
	{
		/// Canonical pointer for an Epic Account ID string. Same string in ->
		/// same pointer out. Never returns null for a non-empty string.
		EOS_EpicAccountId InternEpic(const std::string& Value);

		/// Canonical pointer for a Product User ID string.
		EOS_ProductUserId InternProduct(const std::string& Value);

		/// Canonical pointer for a continuance token string.
		EOS_ContinuanceToken InternContinuance(const std::string& Value);

		/// The interned string backing an ID, or empty if the handle is null.
		const std::string& EpicString(EOS_EpicAccountId Id);
		const std::string& ProductString(EOS_ProductUserId Id);
	}
}
