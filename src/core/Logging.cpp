#include "core/Logging.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#else
#	include <unistd.h>
#endif

namespace EOSEmu
{
	namespace
	{
		std::mutex g_LogMutex;
		EOS_LogMessageFunc g_Callback = nullptr;

		// EOSEmu's own sink -- independent of the consumer callback and its levels.
		std::FILE* g_EmuFile = nullptr;
		std::string g_EmuFilePath;
		bool g_EmuConsole = false;
		EOS_ELogLevel g_EmuLevel = EOS_ELogLevel::EOS_LOG_Off; // off until configured

		const char* LevelName(EOS_ELogLevel Level)
		{
			switch (Level)
			{
			case EOS_ELogLevel::EOS_LOG_Off: return "Off";
			case EOS_ELogLevel::EOS_LOG_Fatal: return "Fatal";
			case EOS_ELogLevel::EOS_LOG_Error: return "Error";
			case EOS_ELogLevel::EOS_LOG_Warning: return "Warning";
			case EOS_ELogLevel::EOS_LOG_Info: return "Info";
			case EOS_ELogLevel::EOS_LOG_Verbose: return "Verbose";
			case EOS_ELogLevel::EOS_LOG_VeryVerbose: return "VeryVerbose";
			default: return "?";
			}
		}

		EOS_ELogLevel ParseLevel(const std::string& Name, EOS_ELogLevel Default)
		{
			std::string L = Name;
			std::transform(L.begin(), L.end(), L.begin(),
				[](unsigned char C) { return static_cast<char>(std::tolower(C)); });
			if (L.empty()) return Default;
			if (L == "off") return EOS_ELogLevel::EOS_LOG_Off;
			if (L == "fatal") return EOS_ELogLevel::EOS_LOG_Fatal;
			if (L == "error") return EOS_ELogLevel::EOS_LOG_Error;
			if (L == "warning" || L == "warn") return EOS_ELogLevel::EOS_LOG_Warning;
			if (L == "info") return EOS_ELogLevel::EOS_LOG_Info;
			if (L == "verbose") return EOS_ELogLevel::EOS_LOG_Verbose;
			if (L == "veryverbose") return EOS_ELogLevel::EOS_LOG_VeryVerbose;
			return Default;
		}

		void FormatTimestamp(char* Out, size_t N)
		{
			using namespace std::chrono;
			const auto Now = system_clock::now();
			const std::time_t T = system_clock::to_time_t(Now);
			const auto Ms = duration_cast<milliseconds>(Now.time_since_epoch()) % 1000;
			std::tm Tm = {};
#if defined(_WIN32)
			localtime_s(&Tm, &T);
#else
			localtime_r(&T, &Tm);
#endif
			std::snprintf(Out, N, "%02d:%02d:%02d.%03d",
				Tm.tm_hour, Tm.tm_min, Tm.tm_sec, static_cast<int>(Ms.count()));
		}

		// eos_logging.h:134 -- "By default all log categories will callback for
		// Warnings, Errors, and Fatals", i.e. Warning is the default threshold.
		// Index by EOS_ELogCategory; +1 covers the all-categories sentinel we
		// never index but keeps the intent obvious.
		constexpr int kCategoryCount = static_cast<int>(EOS_ELogCategory::EOS_LC_CustomInvites) + 1;
		EOS_ELogLevel g_Levels[kCategoryCount] = {};
		bool g_LevelsInit = false;

		void EnsureLevels()
		{
			if (!g_LevelsInit)
			{
				for (int i = 0; i < kCategoryCount; ++i)
				{
					g_Levels[i] = EOS_ELogLevel::EOS_LOG_Warning;
				}
				g_LevelsInit = true;
			}
		}

		const char* CategoryName(EOS_ELogCategory Category)
		{
			using C = EOS_ELogCategory;
			switch (Category)
			{
			case C::EOS_LC_Core: return "LogEOS";
			case C::EOS_LC_Auth: return "LogEOSAuth";
			case C::EOS_LC_Friends: return "LogEOSFriends";
			case C::EOS_LC_Presence: return "LogEOSPresence";
			case C::EOS_LC_UserInfo: return "LogEOSUserInfo";
			case C::EOS_LC_HttpSerialization: return "LogHttp";
			case C::EOS_LC_Ecom: return "LogEOSEcom";
			case C::EOS_LC_P2P: return "LogEOSP2P";
			case C::EOS_LC_Sessions: return "LogEOSSessions";
			case C::EOS_LC_RateLimiter: return "LogEOSRateLimiter";
			case C::EOS_LC_PlayerDataStorage: return "LogEOSPlayerDataStorage";
			case C::EOS_LC_Analytics: return "LogEOSAnalytics";
			case C::EOS_LC_Messaging: return "LogEOSMessaging";
			case C::EOS_LC_Connect: return "LogEOSConnect";
			case C::EOS_LC_Overlay: return "LogEOSOverlay";
			case C::EOS_LC_Achievements: return "LogEOSAchievements";
			case C::EOS_LC_Stats: return "LogEOSStats";
			case C::EOS_LC_UI: return "LogEOSUI";
			case C::EOS_LC_Lobby: return "LogEOSLobby";
			case C::EOS_LC_Leaderboards: return "LogEOSLeaderboards";
			case C::EOS_LC_Keychain: return "LogEOSKeychain";
			case C::EOS_LC_IntegratedPlatform: return "LogEOSIntegratedPlatform";
			case C::EOS_LC_TitleStorage: return "LogEOSTitleStorage";
			case C::EOS_LC_Mods: return "LogEOSMods";
			case C::EOS_LC_AntiCheat: return "LogEOSAntiCheat";
			case C::EOS_LC_Reports: return "LogEOSReports";
			case C::EOS_LC_Sanctions: return "LogEOSSanctions";
			case C::EOS_LC_ProgressionSnapshots: return "LogEOSProgressionSnapshots";
			case C::EOS_LC_KWS: return "LogEOSKWS";
			case C::EOS_LC_RTC: return "LogEOSRTC";
			case C::EOS_LC_RTCAdmin: return "LogEOSRTCAdmin";
			case C::EOS_LC_CustomInvites: return "LogEOSCustomInvites";
			default: return "LogEOS";
			}
		}

		// Writes one formatted line to EOSEmu's own sink(s). Caller holds g_LogMutex.
		void WriteEmuSink(EOS_ELogCategory Category, EOS_ELogLevel Level, const char* Msg)
		{
			char Ts[32];
			FormatTimestamp(Ts, sizeof(Ts));
			const char* Cat = CategoryName(Category);
			const char* Lvl = LevelName(Level);
			if (g_EmuFile != nullptr)
			{
				std::fprintf(g_EmuFile, "%s [%-11s] [%s] %s\n", Ts, Lvl, Cat, Msg);
				std::fflush(g_EmuFile);
			}
			if (g_EmuConsole)
			{
				std::fprintf(stderr, "[EOSEmu] %s [%s] [%s] %s\n", Ts, Lvl, Cat, Msg);
				std::fflush(stderr);
#if defined(_WIN32)
				char Line[1152];
				std::snprintf(Line, sizeof(Line), "[EOSEmu] [%s] [%s] %s\n", Lvl, Cat, Msg);
				OutputDebugStringA(Line);
#endif
			}
		}
	}

	void Log(EOS_ELogCategory Category, EOS_ELogLevel Level, const char* Format, ...)
	{
		EOS_LogMessageFunc Callback = nullptr;
		bool WantConsumer = false;
		char Buffer[1024];

		{
			std::lock_guard<std::mutex> Lock(g_LogMutex);
			EnsureLevels();
			Callback = g_Callback;

			// A lower numeric level is more severe; a message passes a sink only if
			// it is at least as severe as that sink's threshold. The consumer sink
			// uses the game's per-category level; the EOSEmu sink its own.
			const int Index = static_cast<int>(Category);
			const bool CategoryOk = Index >= 0 && Index < kCategoryCount;
			WantConsumer = Callback != nullptr &&
				(!CategoryOk || static_cast<int>(Level) <= static_cast<int>(g_Levels[Index]));
			const bool WantEmu = (g_EmuFile != nullptr || g_EmuConsole) &&
				static_cast<int>(Level) <= static_cast<int>(g_EmuLevel);

			if (!WantConsumer && !WantEmu)
			{
				return;
			}

			va_list Args;
			va_start(Args, Format);
			std::vsnprintf(Buffer, sizeof(Buffer), Format, Args);
			va_end(Args);

			// The EOSEmu sink writes under the lock (serialises file/console output
			// and guards against a concurrent reopen).
			if (WantEmu)
			{
				WriteEmuSink(Category, Level, Buffer);
			}
		}

		// The consumer callback fires outside the lock, as it may run arbitrary
		// game code (and must not deadlock against a re-entrant Log call).
		if (WantConsumer && Callback != nullptr)
		{
			EOS_LogMessage Message = {};
			Message.Category = CategoryName(Category);
			Message.Message = Buffer;
			Message.Level = Level;
			Callback(&Message);
		}
	}

	void TraceApiCall(const char* Function)
	{
		if (Function == nullptr)
		{
			return;
		}
		// Map the EOS_<Interface>_ prefix onto a log category so the trace shows
		// up beside the interface's own diagnostics (LogEOSLobby, LogEOSP2P, ...).
		// Longer prefixes are checked before the ones they contain (RTCAdmin
		// before RTC, ActiveSession/Session before nothing that shadows it).
		using C = EOS_ELogCategory;
		struct Entry { const char* Prefix; C Category; };
		static const Entry kMap[] = {
			{"EOS_LobbyRTC", C::EOS_LC_Lobby},
			{"EOS_Lobby", C::EOS_LC_Lobby},
			{"EOS_ActiveSession", C::EOS_LC_Sessions},
			{"EOS_Session", C::EOS_LC_Sessions},
			{"EOS_P2P", C::EOS_LC_P2P},
			{"EOS_Auth", C::EOS_LC_Auth},
			{"EOS_Connect", C::EOS_LC_Connect},
			{"EOS_UserInfo", C::EOS_LC_UserInfo},
			{"EOS_Friends", C::EOS_LC_Friends},
			{"EOS_Presence", C::EOS_LC_Presence},
			{"EOS_CustomInvites", C::EOS_LC_CustomInvites},
			{"EOS_UI", C::EOS_LC_UI},
			{"EOS_Ecom", C::EOS_LC_Ecom},
			{"EOS_Achievements", C::EOS_LC_Achievements},
			{"EOS_Stats", C::EOS_LC_Stats},
			{"EOS_Leaderboards", C::EOS_LC_Leaderboards},
			{"EOS_PlayerDataStorage", C::EOS_LC_PlayerDataStorage},
			{"EOS_TitleStorage", C::EOS_LC_TitleStorage},
			{"EOS_Mods", C::EOS_LC_Mods},
			{"EOS_RTCAdmin", C::EOS_LC_RTCAdmin},
			{"EOS_RTC", C::EOS_LC_RTC},
			{"EOS_Sanctions", C::EOS_LC_Sanctions},
			{"EOS_Reports", C::EOS_LC_Reports},
			{"EOS_AntiCheat", C::EOS_LC_AntiCheat},
			{"EOS_KWS", C::EOS_LC_KWS},
			{"EOS_IntegratedPlatform", C::EOS_LC_IntegratedPlatform},
			{"EOS_ProgressionSnapshot", C::EOS_LC_ProgressionSnapshots},
			{"EOS_Metrics", C::EOS_LC_Analytics},
		};
		C Category = C::EOS_LC_Core;
		for (const Entry& E : kMap)
		{
			const size_t N = std::strlen(E.Prefix);
			if (std::strncmp(Function, E.Prefix, N) == 0)
			{
				Category = E.Category;
				break;
			}
		}
		Log(Category, EOS_ELogLevel::EOS_LOG_VeryVerbose, "%s", Function);
	}

	namespace
	{
		// Expand a "{pid}" token in the log path to the current process id, so two
		// EOSEmu instances on one machine can each write their own file (e.g.
		// File = eosemu_{pid}.log) instead of clobbering a shared one. No token ->
		// path unchanged, preserving the predictable single-instance name.
		std::string ExpandLogPath(const std::string& Path)
		{
			const std::string Token = "{pid}";
			const size_t At = Path.find(Token);
			if (At == std::string::npos)
			{
				return Path;
			}
#if defined(_WIN32)
			const unsigned long Pid = GetCurrentProcessId();
#else
			const unsigned long Pid = static_cast<unsigned long>(getpid());
#endif
			std::string Out = Path;
			Out.replace(At, Token.size(), std::to_string(Pid));
			return Out;
		}
	}

	void ConfigureEmuLogSink(const std::string& RawFilePath, bool Console, const std::string& LevelName)
	{
		std::lock_guard<std::mutex> Lock(g_LogMutex);

		const std::string FilePath = ExpandLogPath(RawFilePath);
		g_EmuConsole = Console;
		const EOS_ELogLevel Level = ParseLevel(LevelName, EOS_ELogLevel::EOS_LOG_Info);
		// With no destination there is nothing to filter for; keep the sink off so
		// Log() short-circuits.
		g_EmuLevel = (!FilePath.empty() || Console) ? Level : EOS_ELogLevel::EOS_LOG_Off;

		// Only (re)open when the path actually changes, so layering config sources
		// (env/DLL then CacheDirectory) does not truncate or churn the file.
		if (FilePath != g_EmuFilePath)
		{
			if (g_EmuFile != nullptr)
			{
				std::fclose(g_EmuFile);
				g_EmuFile = nullptr;
			}
			g_EmuFilePath = FilePath;
			if (!FilePath.empty())
			{
				g_EmuFile = std::fopen(FilePath.c_str(), "a"); // append across runs
				if (g_EmuFile != nullptr)
				{
					char Ts[32];
					FormatTimestamp(Ts, sizeof(Ts));
					std::fprintf(g_EmuFile, "%s [Info       ] [LogEOS] ---- EOSEmu log sink opened (level %s) ----\n",
						Ts, LevelName.empty() ? "Info" : LevelName.c_str());
					std::fflush(g_EmuFile);
				}
			}
		}
	}

	void ResetLogging()
	{
		std::lock_guard<std::mutex> Lock(g_LogMutex);
		g_Callback = nullptr;
		g_LevelsInit = false;
		if (g_EmuFile != nullptr)
		{
			std::fclose(g_EmuFile);
			g_EmuFile = nullptr;
		}
		g_EmuFilePath.clear();
		g_EmuConsole = false;
		g_EmuLevel = EOS_ELogLevel::EOS_LOG_Off;
	}
}

// ----------------------------------------------------------------------------
// Exported entry points.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_EResult) EOS_Logging_SetCallback(EOS_LogMessageFunc Callback)
{
	std::lock_guard<std::mutex> Lock(EOSEmu::g_LogMutex);
	EOSEmu::g_Callback = Callback;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Logging_SetLogLevel(EOS_ELogCategory LogCategory, EOS_ELogLevel LogLevel)
{
	std::lock_guard<std::mutex> Lock(EOSEmu::g_LogMutex);
	EOSEmu::EnsureLevels();
	if (LogCategory == EOS_ELogCategory::EOS_LC_ALL_CATEGORIES)
	{
		for (int i = 0; i < EOSEmu::kCategoryCount; ++i)
		{
			EOSEmu::g_Levels[i] = LogLevel;
		}
		return EOS_EResult::EOS_Success;
	}
	const int Index = static_cast<int>(LogCategory);
	if (Index < 0 || Index >= EOSEmu::kCategoryCount)
	{
		return EOS_EResult::EOS_InvalidParameters;
	}
	EOSEmu::g_Levels[Index] = LogLevel;
	return EOS_EResult::EOS_Success;
}
