#pragma once

//
// The platform instance -- the root object every interface hangs off.
//
// EOS_HPlatform is an opaque pointer; we make it point straight at an
// EOSEmu::Platform. Each EOS_H<Interface> handle likewise points at the
// corresponding member sub-object, so EOS_Platform_Get<X>Interface is a pointer
// offset with no lookup. Interfaces not yet implemented for real still need a
// distinct non-null handle (samples check the getter's result), which comes
// from a small sentinel table.
//

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "eos_sdk.h"

#include "core/Dispatch.h"
#include "core/Peers.h"

namespace EOSEmu
{
	class Config;
	class Identity;
	class AuthInterface;
	class ConnectInterface;
	class P2PInterface;
	class LobbyInterface;
	class SessionsInterface;
	class FriendsInterface;
	class PresenceInterface;
	class UserInfoInterface;
	class CustomInvitesInterface;
	class EcomInterface;
	class ModsInterface;
	class RTCInterface;
	class UIInterface;
	class Overlay;

	namespace net
	{
		class Node;
	}

	// One sentinel per interface getter. Interfaces backed by a real object
	// return that object's address; the rest return &Sentinels[Slot], which is
	// non-null, distinct and stable, and ignored by the generated stubs.
	enum class InterfaceSlot : int
	{
		Metrics, Ecom, UI, RTC, RTCAudio, RTCData, RTCAdmin,
		PlayerDataStorage, TitleStorage, Achievements, Stats, Leaderboards,
		Mods, AntiCheatClient, AntiCheatServer, Reports, Sanctions, KWS,
		ProgressionSnapshot, CustomInvites, IntegratedPlatform,
		Count
	};

	class Platform
	{
	public:
		Platform();
		~Platform();

		Platform(const Platform&) = delete;
		Platform& operator=(const Platform&) = delete;

		void Configure(const EOS_Platform_Options* Options);

		// Called from EOS_Platform_Tick: drains the deferred-callback queue and
		// pumps the LAN transport, all on the caller's thread.
		void Tick();

		Dispatcher& Dispatch() { return Dispatch_; }
		Identity& LocalIdentity() { return *Identity_; }
		net::Node& Net() { return *Net_; }
		PeerDirectory& Peers() { return Peers_; }

		/// User configuration (env + DLL-adjacent + CacheDirectory eosemu.ini).
		/// Always non-null; an absent file yields an empty config that returns
		/// defaults. `class Config` is an elaborated-type-specifier so the type
		/// name and this accessor's name can coexist.
		class Config& Config() { return *Config_; }

		/// Monotonic tick counter, incremented once per EOS_Platform_Tick. A
		/// coarse clock for Lobby/Session announcement freshness. (Peer-directory
		/// liveness uses wall-clock instead -- see PeerInfo::LastSeen.)
		uint64_t TickCount() const { return TickCount_; }

		/// Full local presence, replicated to peers in the Hello extension so a
		/// friend running the same game shows as in-game with join data. Called
		/// by EOS_Presence_SetPresence (any thread); the next Tick re-announces.
		void SetLocalPresence(uint8_t Status, const std::string& RichText,
			const std::string& JoinInfo,
			std::vector<std::pair<std::string, std::string>> Data)
		{
			{
				std::lock_guard<std::mutex> Lock(PresenceMutex_);
				LocalPresence_.Status = Status;
				LocalPresence_.RichText = RichText;
				LocalPresence_.JoinInfo = JoinInfo;
				LocalPresence_.Data = std::move(Data);
			}
			HelloDirty_ = true;
		}

		/// The ProductId from EOS_Platform_Options ("" before Configure). Serves
		/// as EOS_Presence_Info::ProductId so a friend's presence compares equal
		/// to the running game's own product.
		const std::string& ProductId() const { return ProductId_; }

		/// The local user's real SteamID64, reported through Connect's
		/// ExternalAccountInfo so it agrees with what the game's own Steam layer
		/// (real or emulator) sees. Resolution order: [Identity] SteamId config
		/// pin > live query of the loaded steam_api module > the Steam session
		/// ticket a Login call carried. 0 when no source yields one (callers fall
		/// back to the synthesised id). Any thread.
		uint64_t LocalSteamId();

		/// Offers the hex Steam session ticket seen in an Auth/Connect Login as a
		/// SteamId source. Weakest priority: ignored once an id is known or when
		/// the steam_api module answers directly.
		void NoteSteamSessionTicket(const char* HexToken);

		AuthInterface& Auth() { return *Auth_; }
		ConnectInterface& Connect() { return *Connect_; }
		P2PInterface& P2P() { return *P2P_; }
		LobbyInterface& Lobby() { return *Lobby_; }
		SessionsInterface& Sessions() { return *Sessions_; }
		FriendsInterface& Friends() { return *Friends_; }
		PresenceInterface& Presence() { return *Presence_; }
		UserInfoInterface& UserInfo() { return *UserInfo_; }
		CustomInvitesInterface& CustomInvites() { return *CustomInvites_; }
		EcomInterface& Ecom() { return *Ecom_; }
		ModsInterface& Mods() { return *Mods_; }
		RTCInterface& RTC() { return *RTC_; }
		UIInterface& UI() { return *UI_; }
		// `class Overlay` (elaborated) so the type name and this accessor coexist.
		class Overlay& Overlay() { return *Overlay_; }

		void* Sentinel(InterfaceSlot Slot) { return &Sentinels_[static_cast<int>(Slot)]; }

		const std::string& CacheDirectory() const { return CacheDirectory_; }
		uint64_t Flags() const { return Flags_; }

		// Reported locale/country. Active is what a fresh account resolves to;
		// Override is what the game explicitly pinned (empty if none). Both are
		// seeded from [Identity] Language/Country and round-trip through the
		// SetOverride*Code entry points.
		const std::string& ActiveLocaleCode() const { return Locale_; }
		const std::string& OverrideLocaleCode() const { return OverrideLocale_; }
		const std::string& ActiveCountryCode() const { return Country_; }
		const std::string& OverrideCountryCode() const { return OverrideCountry_; }
		void SetOverrideLocaleCode(const char* Code);
		void SetOverrideCountryCode(const char* Code);

	private:
		void OnDatagram(const net::Endpoint& From, const net::WireHeader& Header, const uint8_t* Payload, uint16_t Len);
		void BroadcastHello();
		// Best-effort "I'm leaving" broadcast so peers flag us offline immediately
		// on a clean release instead of waiting out PeerTimeout_. Sent from
		// ~Platform.
		void BroadcastGoodbye();
		// Fires the Presence "went offline" notification for a peer that just
		// transitioned offline (liveness timeout or a graceful Goodbye).
		void NotifyPeerOffline(const PeerInfo& Peer);

		Dispatcher Dispatch_;
		std::unique_ptr<class Config> Config_;
		std::unique_ptr<Identity> Identity_;
		std::unique_ptr<net::Node> Net_;
		PeerDirectory Peers_;
		// Written by Tick (game thread), read by OnDatagram (receive thread).
		std::atomic<uint64_t> TickCount_{0};
		std::chrono::steady_clock::time_point LastHello_;
		// A peer is dropped once this long passes with no Hello from it. Five
		// missed announces at the 2s cadence; overridable via [Network]
		// PeerTimeoutSeconds. The graceful Goodbye handles the common clean-exit
		// case sooner, so this only bounds how long a crash/kill lingers.
		std::chrono::seconds PeerTimeout_{10};

		// Local presence as announced in Hello. Guarded because a game may call
		// EOS_Presence_SetPresence off the Tick thread while Tick broadcasts.
		struct LocalPresenceState
		{
			uint8_t Status = 1; // EOS_Presence_EStatus::EOS_PS_Online
			std::string RichText;
			std::string JoinInfo;
			std::vector<std::pair<std::string, std::string>> Data;
		};
		std::mutex PresenceMutex_;
		LocalPresenceState LocalPresence_;
		// Set by SetLocalPresence so the next Tick re-announces immediately
		// instead of waiting out the 2s cadence.
		std::atomic<bool> HelloDirty_{false};

		std::unique_ptr<AuthInterface> Auth_;
		std::unique_ptr<ConnectInterface> Connect_;
		std::unique_ptr<P2PInterface> P2P_;
		std::unique_ptr<LobbyInterface> Lobby_;
		std::unique_ptr<SessionsInterface> Sessions_;
		std::unique_ptr<FriendsInterface> Friends_;
		std::unique_ptr<PresenceInterface> Presence_;
		std::unique_ptr<UserInfoInterface> UserInfo_;
		std::unique_ptr<CustomInvitesInterface> CustomInvites_;
		std::unique_ptr<EcomInterface> Ecom_;
		std::unique_ptr<ModsInterface> Mods_;
		std::unique_ptr<RTCInterface> RTC_;
		std::unique_ptr<class Overlay> Overlay_;
		std::unique_ptr<UIInterface> UI_;

		uint8_t Sentinels_[static_cast<int>(InterfaceSlot::Count)] = {};
		std::string CacheDirectory_;
		std::string ProductId_;
		uint64_t Flags_ = 0;
		// Resolved SteamID64 (0 = not yet known). Atomic because a Login on the
		// game thread may race a LocalSteamId read from another entry point.
		std::atomic<uint64_t> SteamId_{0};

		std::string Locale_ = "en";
		std::string OverrideLocale_;
		std::string Country_;
		std::string OverrideCountry_;
	};

	/// The one live platform, or null before Create / after Release. EOS creates
	/// a single platform per process in every sample, so a global is faithful
	/// and lets interface entry points reach shared state without threading a
	/// handle through every internal call.
	Platform* CurrentPlatform();

	/// Recovers the Platform from an EOS_HPlatform, validating it against the
	/// live instance. Returns null for a stale or bogus handle.
	Platform* AsPlatform(EOS_HPlatform Handle);

	/// ProductName / ProductVersion the game passed to EOS_Initialize ("" before
	/// it runs). Used to fill EOS_Presence_Info's product fields.
	const std::string& InitializedProductName();
	const std::string& InitializedProductVersion();
}
