#pragma once

//
// Logging for EOSEmu.
//
// The consumer registers a single sink via EOS_Logging_SetCallback and filters
// it per-category with EOS_Logging_SetLogLevel (eos_logging.h). We route every
// internal diagnostic through the same sink so that the fastest way to learn
// what a given game needs is to read its own log output -- see CLAUDE.md, "Log
// every stub hit once".
//
// The logging interface "functions entirely on the local system" and needs no
// platform handle (eos_logging.h:8), so this state is process-global, guarded
// by its own mutex, and usable before EOS_Platform_Create.
//

#include <string>

#include "eos_common.h"
#include "eos_logging.h"

namespace EOSEmu
{
	/// Emits a formatted message to two independent sinks -- the consumer's
	/// callback (filtered by the level it set via EOS_Logging_SetLogLevel) and
	/// EOSEmu's own file/console sink (filtered by its own configured level).
	/// Either may be absent. Safe to call from any thread and at any point in the
	/// lifecycle.
	void Log(EOS_ELogCategory Category, EOS_ELogLevel Level, const char* Format, ...);

	/// Records that an exported public API entry point was called, at
	/// EOS_LOG_VeryVerbose, under the log category implied by the function's
	/// EOS_<Interface>_ prefix. Function is the bare name (pass __FUNCTION__ via
	/// EOSEMU_API_TRACE). Cheap when no sink is at VeryVerbose. This is what makes
	/// a game's exact call sequence visible in the log without touching any
	/// per-function code by hand.
	void TraceApiCall(const char* Function);

	/// Configures EOSEmu's own log sink from the [Logging] config, independent of
	/// whether the game ever calls EOS_Logging_SetCallback. FilePath empty +
	/// Console false disables it (the default, so a fresh install logs nothing on
	/// its own). LevelName is one of off/fatal/error/warning/info/verbose/
	/// veryverbose (case-insensitive); empty defaults to Info. Idempotent -- the
	/// file is only reopened when the path changes.
	void ConfigureEmuLogSink(const std::string& FilePath, bool Console, const std::string& LevelName);

	// Exported entry points live here rather than in a generated stub because
	// they carry real state. Declared so Platform.cpp can reset them on Shutdown.
	void ResetLogging();
}

// Terse call sites. Cat is the bare category suffix (Core, Auth, P2P, Lobby,
// ...) and expands to the scoped EOS_ELogCategory enumerator, so call sites stay
// short without leaking the enum-class qualification everywhere.
#define EOSEMU_LOG(Cat, Level, ...) ::EOSEmu::Log(EOS_ELogCategory::EOS_LC_##Cat, (Level), __VA_ARGS__)
#define EOSEMU_WARN(Cat, ...) ::EOSEmu::Log(EOS_ELogCategory::EOS_LC_##Cat, EOS_ELogLevel::EOS_LOG_Warning, __VA_ARGS__)
#define EOSEMU_ERROR(Cat, ...) ::EOSEmu::Log(EOS_ELogCategory::EOS_LC_##Cat, EOS_ELogLevel::EOS_LOG_Error, __VA_ARGS__)
#define EOSEMU_INFO(Cat, ...) ::EOSEmu::Log(EOS_ELogCategory::EOS_LC_##Cat, EOS_ELogLevel::EOS_LOG_Info, __VA_ARGS__)

// Entry trace for an exported public API function. Placed as the first line of
// every exported entry point so a game's call sequence shows up under
// VeryVerbose. __FUNCTION__ yields the bare name on MSVC, GCC and Clang, so the
// name never has to be spelled out at the call site.
#define EOSEMU_API_TRACE() ::EOSEmu::TraceApiCall(__FUNCTION__)

// Entry trace for an internal interface method, at VeryVerbose under an explicit
// category (its __FUNCTION__ is a scoped C++ name like Foo::Bar that carries no
// EOS_<Interface>_ prefix for TraceApiCall to map, so the category is named
// here). Logs the scoped name -- on MSVC that includes the class, e.g.
// "SessionsInterface::UpdateSession", which reads clearly beside the boundary
// trace "EOS_Sessions_UpdateSession".
#define EOSEMU_TRACE(Cat) ::EOSEmu::Log(EOS_ELogCategory::EOS_LC_##Cat, EOS_ELogLevel::EOS_LOG_VeryVerbose, "%s", __FUNCTION__)
