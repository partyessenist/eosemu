//
// INI configuration loader. See Config.h for the search order and semantics.
//

#include "core/Config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#endif

namespace EOSEmu
{
	namespace
	{
		std::string Lower(std::string S)
		{
			std::transform(S.begin(), S.end(), S.begin(),
				[](unsigned char C) { return static_cast<char>(std::tolower(C)); });
			return S;
		}

		std::string Trim(const std::string& S)
		{
			size_t Begin = 0;
			size_t End = S.size();
			while (Begin < End && std::isspace(static_cast<unsigned char>(S[Begin]))) ++Begin;
			while (End > Begin && std::isspace(static_cast<unsigned char>(S[End - 1]))) --End;
			return S.substr(Begin, End - Begin);
		}

		// A canonical key for a file path so the same file resolved two different
		// ways compares equal. On Windows paths are case-insensitive and may mix
		// slashes, so fully qualify and lower-case; elsewhere use the path as-is.
		std::string CanonicalPath(const std::string& Path)
		{
#if defined(_WIN32)
			char Full[MAX_PATH] = {};
			const DWORD N = GetFullPathNameA(Path.c_str(), MAX_PATH, Full, nullptr);
			std::string Result = (N > 0 && N < MAX_PATH) ? std::string(Full, N) : Path;
			return Lower(Result);
#else
			return Path;
#endif
		}

		// Strip one matched pair of surrounding double quotes. Batch files that do
		// `set VAR="a path"` embed the quotes in the value (the correct form is
		// `set "VAR=a path"`); a quoted path then fails to open. Tolerate the
		// common mistake rather than silently losing the whole config.
		std::string StripQuotes(std::string S)
		{
			if (S.size() >= 2 && S.front() == '"' && S.back() == '"')
			{
				S = S.substr(1, S.size() - 2);
			}
			return S;
		}

		std::string ReadEnv(const char* Name)
		{
#if defined(_WIN32)
			char Buffer[1024] = {};
			size_t Length = 0;
			if (getenv_s(&Length, Buffer, sizeof(Buffer), Name) == 0 && Length > 1)
			{
				return StripQuotes(std::string(Buffer, Length - 1));
			}
			return {};
#else
			const char* Value = std::getenv(Name);
			return Value ? StripQuotes(std::string(Value)) : std::string();
#endif
		}
	}

	bool Config::LoadFile(const std::string& Path, Source Priority)
	{
		if (Path.empty())
		{
			return false;
		}
		std::ifstream File(Path);
		if (!File)
		{
			return false;
		}

		std::lock_guard<std::mutex> Lock(Mutex_);
		// Skip a file already parsed under a different search entry.
		const std::string Canonical = CanonicalPath(Path);
		for (const std::string& Seen : LoadedPaths_)
		{
			if (Seen == Canonical) return false;
		}
		LoadedPaths_.push_back(Canonical);

		std::string Section;
		std::string Line;
		bool First = true;
		while (std::getline(File, Line))
		{
			// Tolerate CRLF files opened in text mode on non-Windows.
			if (!Line.empty() && Line.back() == '\r') Line.pop_back();
			// Strip a leading UTF-8 BOM (Notepad / PowerShell Set-Content -Encoding
			// utf8 emit one); otherwise the first line -- often a [Section] header
			// -- fails to parse.
			if (First)
			{
				First = false;
				if (Line.size() >= 3 &&
					static_cast<unsigned char>(Line[0]) == 0xEF &&
					static_cast<unsigned char>(Line[1]) == 0xBB &&
					static_cast<unsigned char>(Line[2]) == 0xBF)
				{
					Line.erase(0, 3);
				}
			}
			const std::string Trimmed = Trim(Line);
			if (Trimmed.empty() || Trimmed[0] == '#' || Trimmed[0] == ';')
			{
				continue; // blank or whole-line comment
			}
			if (Trimmed.front() == '[' && Trimmed.back() == ']')
			{
				Section = Lower(Trim(Trimmed.substr(1, Trimmed.size() - 2)));
				continue;
			}
			const size_t Eq = Trimmed.find('=');
			if (Eq == std::string::npos)
			{
				continue; // not a key=value line
			}
			const std::string Key = Lower(Trim(Trimmed.substr(0, Eq)));
			const std::string Val = Trim(Trimmed.substr(Eq + 1));
			if (Key.empty())
			{
				continue;
			}
			Data_[Section][Key].push_back(Value{Val, static_cast<int>(Priority)});
		}
		AnyLoaded_ = true;
		return true;
	}

	bool Config::AnyLoaded() const
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		return AnyLoaded_;
	}

	const std::vector<Config::Value>* Config::Find(const std::string& Section, const std::string& Key) const
	{
		auto Sit = Data_.find(Lower(Section));
		if (Sit == Data_.end()) return nullptr;
		auto Kit = Sit->second.find(Lower(Key));
		if (Kit == Sit->second.end()) return nullptr;
		return &Kit->second;
	}

	std::string Config::GetString(const std::string& Section, const std::string& Key, const std::string& Default) const
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		const std::vector<Value>* Values = Find(Section, Key);
		if (Values == nullptr || Values->empty())
		{
			return Default;
		}
		// Highest priority wins; on a tie, the later-inserted value wins.
		const Value* Best = &Values->front();
		for (const Value& V : *Values)
		{
			if (V.Priority >= Best->Priority) Best = &V;
		}
		return Best->Text;
	}

	bool Config::GetBool(const std::string& Section, const std::string& Key, bool Default) const
	{
		const std::string Raw = Lower(GetString(Section, Key, Default ? "true" : "false"));
		if (Raw == "true" || Raw == "1" || Raw == "yes" || Raw == "on") return true;
		if (Raw == "false" || Raw == "0" || Raw == "no" || Raw == "off") return false;
		return Default;
	}

	int Config::GetInt(const std::string& Section, const std::string& Key, int Default) const
	{
		const std::string Raw = GetString(Section, Key);
		if (Raw.empty()) return Default;
		try
		{
			size_t Consumed = 0;
			const int Parsed = std::stoi(Raw, &Consumed, 10);
			return Consumed > 0 ? Parsed : Default;
		}
		catch (...)
		{
			return Default;
		}
	}

	std::vector<std::string> Config::GetList(const std::string& Section, const std::string& Key) const
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		std::vector<std::string> Out;
		const std::vector<Value>* Values = Find(Section, Key);
		if (Values != nullptr)
		{
			for (const Value& V : *Values) Out.push_back(V.Text);
		}
		return Out;
	}

	bool Config::Has(const std::string& Section, const std::string& Key) const
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		const std::vector<Value>* Values = Find(Section, Key);
		return Values != nullptr && !Values->empty();
	}

	std::vector<std::string> Config::GetSectionKeys(const std::string& Section) const
	{
		std::lock_guard<std::mutex> Lock(Mutex_);
		std::vector<std::string> Keys;
		auto Sit = Data_.find(Lower(Section));
		if (Sit != Data_.end())
		{
			for (const auto& KV : Sit->second) Keys.push_back(KV.first);
		}
		return Keys;
	}

	std::string Config::EnvConfigPath()
	{
		return ReadEnv("EOSEMU_CONFIG");
	}

	std::string Config::DllAdjacentConfigPath()
	{
#if defined(_WIN32)
		// An address in this module's data section pins the module, so the
		// resolved path is the EOSEmu DLL itself rather than the host exe. Using
		// the exe directory would be wrong for a game that runs from elsewhere.
		static char Anchor = 0;
		HMODULE Module = nullptr;
		if (!GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(&Anchor), &Module))
		{
			return {};
		}
		char PathBuffer[MAX_PATH] = {};
		const DWORD Length = GetModuleFileNameA(Module, PathBuffer, MAX_PATH);
		if (Length == 0 || Length >= MAX_PATH)
		{
			return {};
		}
		std::string Path(PathBuffer, Length);
		const size_t Slash = Path.find_last_of("\\/");
		if (Slash == std::string::npos)
		{
			return {};
		}
		return Path.substr(0, Slash + 1) + "eosemu.ini";
#else
		// Module-directory resolution is Windows-only here; on other platforms the
		// env var and CacheDirectory fallbacks still apply.
		return {};
#endif
	}
}
