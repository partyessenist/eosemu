#pragma once

//
// User-settable configuration.
//
// EOSEmu derives or stubs a handful of values a user might reasonably want to
// pin: the display name, the reported locale/country, and -- the headline --
// owned DLC / entitlements so a game's Ecom ownership checks pass. A small INI
// file supplies them. Everything keeps working with no file present; an absent
// or empty config is the default and reproduces the pre-config behaviour
// exactly.
//
// Search order (highest priority first):
//   1. %EOSEMU_CONFIG%           -- explicit path via environment variable.
//   2. eosemu.ini next to the loaded DLL (not the exe -- a game may run from
//      elsewhere).
//   3. <CacheDirectory>/eosemu.ini -- only known after EOS_Platform_Options, so
//      layered on later (see Platform::Configure).
//
// Scalars resolve to the highest-priority source that set them; list-valued
// keys merge across sources. See CLAUDE.md / HANDOFF.md Task 1.
//

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace EOSEmu
{
	class Config
	{
	public:
		// Source priority. A higher number wins for scalar getters; lists merge
		// regardless. The three correspond to the search order above.
		enum class Source : int
		{
			CacheDir = 1,
			DllAdjacent = 2,
			Env = 3,
		};

		Config() = default;

		/// Parses an INI file and merges its keys at the given priority. Returns
		/// false (and changes nothing) if the file does not exist or cannot be
		/// opened. Missing files are the normal case, not an error.
		bool LoadFile(const std::string& Path, Source Priority);

		/// True once at least one file has been successfully parsed.
		bool AnyLoaded() const;

		/// Highest-priority value for [Section] Key, or Default if unset.
		std::string GetString(const std::string& Section, const std::string& Key, const std::string& Default = std::string()) const;

		/// Parses true/false/1/0/yes/no/on/off (case-insensitive). Default on miss
		/// or unparseable.
		bool GetBool(const std::string& Section, const std::string& Key, bool Default = false) const;

		/// Parses a base-10 integer. Default on miss or unparseable.
		int GetInt(const std::string& Section, const std::string& Key, int Default = 0) const;

		/// Every value set for [Section] Key across all loaded sources, in load
		/// order. Empty if the key was never set.
		std::vector<std::string> GetList(const std::string& Section, const std::string& Key) const;

		/// True if [Section] Key was set by any source.
		bool Has(const std::string& Section, const std::string& Key) const;

		/// Every key present in [Section] (across all sources, deduplicated), for
		/// sections whose keys are open-ended (e.g. [Stats] stat=value). Order is
		/// unspecified.
		std::vector<std::string> GetSectionKeys(const std::string& Section) const;

		// --- path resolution (public so Platform can drive the search order) ---

		/// %EOSEMU_CONFIG%, or empty if unset.
		static std::string EnvConfigPath();

		/// eosemu.ini in the directory of the DLL this code is compiled into, or
		/// empty if the module directory cannot be determined.
		static std::string DllAdjacentConfigPath();

	private:
		struct Value
		{
			std::string Text;
			int Priority;
		};

		// section -> key -> values (both section and key lower-cased on store and
		// lookup, so matching is case-insensitive as INI conventionally is).
		std::map<std::string, std::map<std::string, std::vector<Value>>> Data_;
		bool AnyLoaded_ = false;
		// Canonical paths already parsed, so the same file resolved by two search
		// entries (e.g. %EOSEMU_CONFIG% pointing at the DLL-adjacent file) is not
		// merged twice -- which would double list-valued keys.
		std::vector<std::string> LoadedPaths_;
		mutable std::mutex Mutex_;

		const std::vector<Value>* Find(const std::string& Section, const std::string& Key) const;
	};
}
