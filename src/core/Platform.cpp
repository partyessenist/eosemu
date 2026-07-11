//
// Platform lifecycle and interface access.
//
// EOS_Initialize/Shutdown bracket the whole SDK; EOS_Platform_Create/Release
// bracket one platform instance. Between them, EOS_Platform_Tick is the single
// place consumer callbacks fire (CLAUDE.md callback rule): it drains the
// Dispatcher on the caller's thread. The 27 Get*Interface getters return the
// address of the owning sub-object (real interfaces) or a per-slot sentinel
// (interfaces still served by generated stubs).
//

#include "core/Platform.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "core/Config.h"
#include "core/Identity.h"
#include "core/Logging.h"
#include "core/Memory.h"
#include "core/SteamId.h"

#include "net/Lan.h"
#include "net/Serialize.h"

#include "interfaces/Auth.h"
#include "interfaces/Connect.h"
#include "interfaces/P2P.h"
#include "interfaces/Lobby.h"
#include "interfaces/Sessions.h"
#include "interfaces/Friends.h"
#include "interfaces/Presence.h"
#include "interfaces/UserInfo.h"
#include "interfaces/CustomInvites.h"
#include "interfaces/Ecom.h"
#include "interfaces/Mods.h"
#include "interfaces/RTC.h"
#include "interfaces/UI.h"
#include "interfaces/LocalStores.h"

#include "overlay/Overlay.h"

namespace EOSEmu
{
	namespace
	{
		std::mutex g_LifecycleMutex;
		bool g_Initialized = false; // EOS_Initialize called, EOS_Shutdown not yet
		bool g_HasShutdown = false; // EOS_Shutdown already called -- second Init is an error
		Platform* g_Platform = nullptr;
		// Stashed from EOS_InitializeOptions for EOS_Presence_Info's product
		// fields. Written once under g_LifecycleMutex before any platform exists.
		std::string g_ProductName;
		std::string g_ProductVersion;
	}

	const std::string& InitializedProductName() { return g_ProductName; }
	const std::string& InitializedProductVersion() { return g_ProductVersion; }

	Platform::Platform()
	{
		// Config must load before Identity so [Identity] overrides take effect.
		// Only the env + DLL-adjacent sources are known this early; the
		// CacheDirectory source is layered on in Configure once Options arrives.
		Config_ = std::make_unique<class Config>();
		Config_->LoadFile(Config::EnvConfigPath(), Config::Source::Env);
		Config_->LoadFile(Config::DllAdjacentConfigPath(), Config::Source::DllAdjacent);

		// Bring up EOSEmu's own log sink as early as possible so that transport
		// and overlay bring-up are captured even if the game never registers a
		// logging callback. Re-applied in Configure once the CacheDirectory
		// config layer is known.
		ConfigureEmuLogSink(Config_->GetString("Logging", "File"),
			Config_->GetBool("Logging", "Console", false),
			Config_->GetString("Logging", "Level"));

		Identity_ = std::make_unique<Identity>(Config_.get());
		Net_ = std::make_unique<net::Node>();

		Auth_ = std::make_unique<AuthInterface>(*this);
		Connect_ = std::make_unique<ConnectInterface>(*this);
		P2P_ = std::make_unique<P2PInterface>(*this);
		Lobby_ = std::make_unique<LobbyInterface>(*this);
		Sessions_ = std::make_unique<SessionsInterface>(*this);
		Friends_ = std::make_unique<FriendsInterface>(*this);
		Presence_ = std::make_unique<PresenceInterface>(*this);
		UserInfo_ = std::make_unique<UserInfoInterface>(*this);
		CustomInvites_ = std::make_unique<CustomInvitesInterface>(*this);
		Ecom_ = std::make_unique<EcomInterface>(*this);
		Mods_ = std::make_unique<ModsInterface>(*this);
		RTC_ = std::make_unique<RTCInterface>(*this);
		// Overlay must exist before the UI interface, whose constructor pushes the
		// default toggle shortcut into it. The window/thread is not started until
		// Configure, once the platform flags reveal whether the game wants it.
		Overlay_ = std::make_unique<class Overlay>(*this);
		UI_ = std::make_unique<UIInterface>(*this);
	}

	Platform::~Platform()
	{
		// Stop the receive thread before interfaces tear down so no datagram
		// handler runs against a half-destroyed interface. Likewise join the
		// overlay render thread before the state it reads (peers, identity) goes.
		if (Net_)
		{
			Net_->Stop();
		}
		if (Overlay_)
		{
			Overlay_->Stop();
		}
	}

	void Platform::Configure(const EOS_Platform_Options* Options)
	{
		if (Options == nullptr)
		{
			return;
		}
		// ApiVersion gates which fields exist. Flags arrived at v5; CacheDirectory
		// at v6. Read defensively -- a game built on an older header passed a
		// smaller struct (CLAUDE.md, "ApiVersion is a real versioning mechanism").
		if (Options->ProductId != nullptr)
		{
			ProductId_ = Options->ProductId;
		}
		if (Options->ApiVersion >= 5)
		{
			Flags_ = Options->Flags;
		}
		if (Options->ApiVersion >= 6 && Options->CacheDirectory != nullptr)
		{
			CacheDirectory_ = Options->CacheDirectory;
		}

		// Now that the CacheDirectory is known, layer its eosemu.ini underneath
		// the env + DLL-adjacent sources already loaded in the constructor. It is
		// the lowest priority, so it only supplies values the others left unset.
		if (!CacheDirectory_.empty())
		{
			std::string Sep = CacheDirectory_;
			if (Sep.back() != '\\' && Sep.back() != '/') Sep += '/';
			Config_->LoadFile(Sep + "eosemu.ini", Config::Source::CacheDir);
		}

		// Re-apply the log sink now that the CacheDirectory config is layered in
		// (a [Logging] block may live there). Idempotent when unchanged.
		ConfigureEmuLogSink(Config_->GetString("Logging", "File"),
			Config_->GetBool("Logging", "Console", false),
			Config_->GetString("Logging", "Level"));

		// Seed reported locale/country from config. [Identity] Language, when set,
		// pins both the active and the override locale (a game reading either sees
		// the configured value); unset leaves the pre-config defaults ("en" / "").
		const std::string Language = Config_->GetString("Identity", "Language");
		OverrideLocale_ = Language;                      // empty when unset
		Locale_ = Language.empty() ? "en" : Language;    // active default "en"
		const std::string CountryCode = Config_->GetString("Identity", "Country");
		OverrideCountry_ = CountryCode;                  // empty when unset
		Country_ = CountryCode;                          // active default ""

		// [Identity] SteamId pins the local user's SteamID64 (decimal). When
		// unset it is resolved on demand -- see LocalSteamId.
		const std::string CfgSteamId = Config_->GetString("Identity", "SteamId");
		if (!CfgSteamId.empty())
		{
			const uint64_t Pinned = std::strtoull(CfgSteamId.c_str(), nullptr, 10);
			if (Pinned != 0)
			{
				SteamId_.store(Pinned, std::memory_order_relaxed);
				EOSEMU_INFO(Connect, "Local SteamID64 %llu (pinned by [Identity] SteamId)",
					static_cast<unsigned long long>(Pinned));
			}
			else
			{
				EOSEMU_WARN(Connect, "[Identity] SteamId '%s' is not a decimal SteamID64; ignored",
					CfgSteamId.c_str());
			}
		}

		// Optional starting presence, broadcast in Hello (see [Presence] Status).
		const std::string Presence = Config_->GetString("Presence", "RichText",
			Config_->GetString("Presence", "Status"));
		if (!Presence.empty()) LocalPresence_.RichText = Presence;

		// Pre-populate the local user's Stats/Achievements stores from config, so
		// a game querying its own progress sees the configured baseline. Both
		// seeds are idempotent, which matters if a platform is recreated.
		const std::string& LocalPid = Identity_->ProductUserIdString();
		for (const std::string& Name : Config_->GetSectionKeys("Stats"))
		{
			SeedStat(LocalPid, Name, Config_->GetInt("Stats", Name, 0));
		}
		for (const std::string& Id : Config_->GetList("Achievements", "Unlocked"))
		{
			SeedUnlockedAchievement(LocalPid, Id);
		}

		// Bring up the LAN transport. The SenderTag lets a node ignore its own
		// broadcasts; derive it from the product user id so it is stable.
		const std::string& Pid = Identity_->ProductUserIdString();
		uint32_t Tag = 0;
		for (char C : Pid)
		{
			Tag = Tag * 131u + static_cast<unsigned char>(C);
		}

		Platform* Self = this;
		LastHello_ = std::chrono::steady_clock::now();
		// Optional discovery-port override isolates independent LAN groups on one
		// subnet ([Network] DiscoveryPort). 0 keeps the default.
		const uint16_t DiscoveryPort = static_cast<uint16_t>(Config_->GetInt("Network", "DiscoveryPort", 0));
		Net_->Start(Tag, [Self](const net::Endpoint& From, const net::WireHeader& Header,
			const uint8_t* Payload, uint16_t Len)
		{
			Self->OnDatagram(From, Header, Payload, Len);
		}, DiscoveryPort);

		// Bring up the social overlay unless the game opted out. The enable-flags
		// (D3D9/10/GL) are only a hint; we honour the two disable flags and detect
		// nothing else because our separate window uses its own device.
		const bool OverlayEnabled = (Flags_ & (EOS_PF_DISABLE_OVERLAY | EOS_PF_DISABLE_SOCIAL_OVERLAY)) == 0;
		Overlay_->Start(OverlayEnabled);
	}

	uint64_t Platform::LocalSteamId()
	{
		uint64_t Id = SteamId_.load(std::memory_order_relaxed);
		if (Id == 0)
		{
			// Ask the game's own Steam layer. Cached on success only, so a query
			// that ran before SteamAPI_Init can succeed later.
			Id = SteamId::QueryLoadedSteamApi();
			if (Id != 0)
			{
				SteamId_.store(Id, std::memory_order_relaxed);
				EOSEMU_INFO(Connect, "Local SteamID64 %llu (from steam_api module)",
					static_cast<unsigned long long>(Id));
			}
		}
		return Id;
	}

	void Platform::NoteSteamSessionTicket(const char* HexToken)
	{
		// LocalSteamId covers the two stronger sources (config pin, live module
		// query); the ticket only fills in when both come up empty.
		if (LocalSteamId() != 0)
		{
			return;
		}
		const uint64_t Id = SteamId::FromSessionTicketHex(HexToken);
		if (Id != 0)
		{
			SteamId_.store(Id, std::memory_order_relaxed);
			EOSEMU_INFO(Connect, "Local SteamID64 %llu (from Steam session ticket)",
				static_cast<unsigned long long>(Id));
		}
	}

	void Platform::OnDatagram(const net::Endpoint& From, const net::WireHeader& Header, const uint8_t* Payload, uint16_t Len)
	{
		// Runs on the receive thread. Peer bookkeeping is thread-safe; P2P data
		// lands in a locked queue; anything touching consumer callbacks is
		// posted to the Dispatcher by the interface itself.
		const auto Type = static_cast<net::MessageType>(Header.Type);
		switch (Type)
		{
		case net::MessageType::Hello:
		{
			net::ByteReader R(Payload, Len);
			const std::string ProductId = R.Str();
			const std::string EpicId = R.Str();
			const std::string DisplayName = R.Str();
			PeerPresence Pres;
			Pres.RichText = R.Str();
			if (!R.Ok())
			{
				break;
			}
			// Presence extension: absent from pre-extension builds (defaults
			// apply), committed only if it parses whole.
			if (R.Remaining() > 0)
			{
				PeerPresence Ext = Pres;
				Ext.Status = R.U8();
				Ext.JoinInfo = R.Str();
				Ext.ProductId = R.Str();
				Ext.ProductVersion = R.Str();
				Ext.ProductName = R.Str();
				const uint32_t Count = R.U32();
				for (uint32_t i = 0; R.Ok() && i < Count; ++i)
				{
					std::string Key = R.Str();
					std::string Value = R.Str();
					Ext.Data.emplace_back(std::move(Key), std::move(Value));
				}
				if (R.Ok())
				{
					Pres = std::move(Ext);
				}
			}
			const bool PresenceChanged = Peers_.Observe(ProductId, EpicId, DisplayName, Pres, From, TickCount_);
			if (PresenceChanged && !EpicId.empty())
			{
				// Posts to the Dispatcher; the observers fire from Tick.
				Presence().OnRemotePresenceChanged(EpicId);
			}
			break;
		}
		case net::MessageType::P2PData:
		case net::MessageType::P2PConnectRequest:
		case net::MessageType::P2PConnectAccept:
		case net::MessageType::P2PConnectClose:
		case net::MessageType::P2PAck:
		case net::MessageType::P2PConnectIgnored:
			P2P().OnDatagram(From, Type, Payload, Len);
			break;
		case net::MessageType::LobbyAnnounce:
		case net::MessageType::LobbyUpdate:
		case net::MessageType::LobbyJoinRequest:
		case net::MessageType::LobbyJoinResponse:
		case net::MessageType::LobbyLeave:
		case net::MessageType::LobbyInvite:
			Lobby().OnDatagram(From, Type, Payload, Len);
			break;
		case net::MessageType::SessionAnnounce:
		case net::MessageType::SessionWithdraw:
		case net::MessageType::SessionInvite:
			Sessions().OnDatagram(From, Type, Payload, Len);
			break;
		case net::MessageType::CustomInvite:
			CustomInvites().OnDatagram(From, Type, Payload, Len);
			break;
		}
	}

	void Platform::BroadcastHello()
	{
		// Only announce once the local user has a product id (always, since it
		// is derived deterministically at construction).
		LocalPresenceState Pres;
		{
			std::lock_guard<std::mutex> Lock(PresenceMutex_);
			Pres = LocalPresence_;
		}
		net::ByteWriter W;
		W.Str(Identity_->ProductUserIdString());
		W.Str(Identity_->EpicAccountIdString());
		W.Str(Identity_->DisplayName());
		W.Str(Pres.RichText);
		// Presence extension (additive -- an old receiver stops after the rich
		// text). Carries what a game needs to see this peer as in-game: status,
		// join info, the product identity, and the presence data records.
		W.U8(Pres.Status);
		W.Str(Pres.JoinInfo);
		W.Str(ProductId_);
		W.Str(InitializedProductVersion());
		W.Str(InitializedProductName());
		W.U32(static_cast<uint32_t>(Pres.Data.size()));
		for (const auto& Kv : Pres.Data)
		{
			W.Str(Kv.first);
			W.Str(Kv.second);
		}
		Net_->Broadcast(net::MessageType::Hello, W.Data().data(), W.Size());
	}

	void Platform::Tick()
	{
		++TickCount_;

		// Periodic discovery: re-announce ourselves ~every 2 seconds so peers
		// that start later still find us and existing peers stay fresh. A
		// SetPresence marks the Hello dirty so peers converge without waiting
		// out the cadence.
		const auto Now = std::chrono::steady_clock::now();
		const bool HelloDue = Now - LastHello_ >= std::chrono::seconds(2);
		if (HelloDirty_.exchange(false) || HelloDue)
		{
			BroadcastHello();
		}
		if (HelloDue)
		{
			LastHello_ = Now;
			// Owners re-broadcast their lobbies/sessions so late searchers and
			// members converge without depending on a change event.
			Lobby().PeriodicAnnounce();
			Sessions().PeriodicAnnounce();
		}

		// Retransmit any unacknowledged reliable P2P packets past their RTO.
		P2P_->Pump();

		// Reconcile lobby-managed RTC rooms (join/leave/member churn) so their
		// notifications land in this same drain, after any lobby completion
		// posted earlier -- the registration window eos_rtc.h documents.
		RTC_->PumpTick();

		// Reconcile user-driven overlay state and queue its notifications before
		// the dispatcher drains, so they fire on this same tick.
		UI_->PumpTick();

		// The one place consumer callbacks fire, on the caller's thread.
		Dispatch_.Drain();
	}

	void Platform::SetOverrideLocaleCode(const char* Code)
	{
		OverrideLocale_ = Code ? Code : "";
		// Active locale follows an explicit override, falling back to the default
		// when the override is cleared -- so Get/Set round-trips.
		Locale_ = OverrideLocale_.empty() ? "en" : OverrideLocale_;
	}

	void Platform::SetOverrideCountryCode(const char* Code)
	{
		OverrideCountry_ = Code ? Code : "";
		Country_ = OverrideCountry_;
	}

	Platform* CurrentPlatform()
	{
		return g_Platform;
	}

	Platform* AsPlatform(EOS_HPlatform Handle)
	{
		Platform* P = reinterpret_cast<Platform*>(Handle);
		if (P != nullptr && P == g_Platform)
		{
			return P;
		}
		return nullptr;
	}
}

using namespace EOSEmu;

// ----------------------------------------------------------------------------
// SDK lifecycle.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_EResult) EOS_Initialize(const EOS_InitializeOptions* Options)
{
	EOSEMU_API_TRACE();
	std::lock_guard<std::mutex> Lock(g_LifecycleMutex);
	if (g_Initialized || g_HasShutdown)
	{
		// The shipped DLL latches "configured once": a second Initialize
		// returns AlreadyConfigured even after a successful Shutdown
		// (oracle-verified).
		return EOS_EResult::EOS_AlreadyConfigured;
	}
	if (Options == nullptr)
	{
		return EOS_EResult::EOS_InvalidParameters;
	}

	// The allocator triple is all-or-nothing (eos_init.h:81).
	const bool AnyAlloc = Options->AllocateMemoryFunction != nullptr
		|| Options->ReallocateMemoryFunction != nullptr
		|| Options->ReleaseMemoryFunction != nullptr;
	const bool AllAlloc = Options->AllocateMemoryFunction != nullptr
		&& Options->ReallocateMemoryFunction != nullptr
		&& Options->ReleaseMemoryFunction != nullptr;
	if (AnyAlloc && !AllAlloc)
	{
		return EOS_EResult::EOS_InvalidParameters;
	}
	if (AllAlloc)
	{
		SetAllocators(Options->AllocateMemoryFunction, Options->ReallocateMemoryFunction, Options->ReleaseMemoryFunction);
	}

	g_Initialized = true;
	EOSEMU_INFO(Core, "EOSEmu initialized (product '%s')",
		Options->ProductName ? Options->ProductName : "?");
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Shutdown()
{
	EOSEMU_API_TRACE();
	std::lock_guard<std::mutex> Lock(g_LifecycleMutex);
	if (!g_Initialized)
	{
		// Oracle-verified: NotConfigured when never initialized, but a second
		// Shutdown after a successful one is UnexpectedError.
		return g_HasShutdown ? EOS_EResult::EOS_UnexpectedError
		                     : EOS_EResult::EOS_NotConfigured;
	}
	g_Initialized = false;
	g_HasShutdown = true;
	ClearAllocators();
	ResetLogging();
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_HPlatform) EOS_Platform_Create(const EOS_Platform_Options* Options)
{
	EOSEMU_API_TRACE();
	std::lock_guard<std::mutex> Lock(g_LifecycleMutex);
	if (!g_Initialized || g_Platform != nullptr)
	{
		return nullptr;
	}
	if (Options == nullptr)
	{
		// Oracle-verified: the shipped DLL returns null rather than creating a
		// default-configured platform.
		return nullptr;
	}
	// EOS_PLATFORM_OPTIONS_API_LATEST is 14, but a valid older struct still has
	// at least ApiVersion + the required id fields; we accept and read
	// defensively rather than reject on version.
	Platform* P = new Platform();
	P->Configure(Options);
	g_Platform = P;
	return reinterpret_cast<EOS_HPlatform>(P);
}

EOS_DECLARE_FUNC(void) EOS_Platform_Release(EOS_HPlatform Handle)
{
	EOSEMU_API_TRACE();
	std::lock_guard<std::mutex> Lock(g_LifecycleMutex);
	Platform* P = reinterpret_cast<Platform*>(Handle);
	if (P == nullptr || P != g_Platform)
	{
		return;
	}
	g_Platform = nullptr;
	delete P;
}

EOS_DECLARE_FUNC(void) EOS_Platform_Tick(EOS_HPlatform Handle)
{
	if (auto* P = AsPlatform(Handle))
	{
		P->Tick();
	}
}

// ----------------------------------------------------------------------------
// Interface getters.
// ----------------------------------------------------------------------------

#define EOSEMU_REAL_GETTER(FnName, HandleType, Accessor)                     \
	EOS_DECLARE_FUNC(HandleType) FnName(EOS_HPlatform Handle)                \
	{                                                                        \
		EOSEMU_API_TRACE();                                                  \
		auto* P = AsPlatform(Handle);                                        \
		return P ? reinterpret_cast<HandleType>(&P->Accessor()) : nullptr;   \
	}

#define EOSEMU_STUB_GETTER(FnName, HandleType, Slot)                         \
	EOS_DECLARE_FUNC(HandleType) FnName(EOS_HPlatform Handle)                \
	{                                                                        \
		EOSEMU_API_TRACE();                                                  \
		auto* P = AsPlatform(Handle);                                        \
		return P ? reinterpret_cast<HandleType>(P->Sentinel(Slot)) : nullptr;\
	}

EOSEMU_REAL_GETTER(EOS_Platform_GetAuthInterface, EOS_HAuth, Auth)
EOSEMU_REAL_GETTER(EOS_Platform_GetConnectInterface, EOS_HConnect, Connect)
EOSEMU_REAL_GETTER(EOS_Platform_GetP2PInterface, EOS_HP2P, P2P)
EOSEMU_REAL_GETTER(EOS_Platform_GetLobbyInterface, EOS_HLobby, Lobby)
EOSEMU_REAL_GETTER(EOS_Platform_GetSessionsInterface, EOS_HSessions, Sessions)
EOSEMU_REAL_GETTER(EOS_Platform_GetFriendsInterface, EOS_HFriends, Friends)
EOSEMU_REAL_GETTER(EOS_Platform_GetPresenceInterface, EOS_HPresence, Presence)
EOSEMU_REAL_GETTER(EOS_Platform_GetUserInfoInterface, EOS_HUserInfo, UserInfo)

EOSEMU_STUB_GETTER(EOS_Platform_GetMetricsInterface, EOS_HMetrics, InterfaceSlot::Metrics)
EOSEMU_REAL_GETTER(EOS_Platform_GetEcomInterface, EOS_HEcom, Ecom)
EOSEMU_REAL_GETTER(EOS_Platform_GetUIInterface, EOS_HUI, UI)
EOSEMU_REAL_GETTER(EOS_Platform_GetRTCInterface, EOS_HRTC, RTC)
EOSEMU_STUB_GETTER(EOS_Platform_GetRTCAdminInterface, EOS_HRTCAdmin, InterfaceSlot::RTCAdmin)
EOSEMU_STUB_GETTER(EOS_Platform_GetPlayerDataStorageInterface, EOS_HPlayerDataStorage, InterfaceSlot::PlayerDataStorage)
EOSEMU_STUB_GETTER(EOS_Platform_GetTitleStorageInterface, EOS_HTitleStorage, InterfaceSlot::TitleStorage)
EOSEMU_STUB_GETTER(EOS_Platform_GetAchievementsInterface, EOS_HAchievements, InterfaceSlot::Achievements)
EOSEMU_STUB_GETTER(EOS_Platform_GetStatsInterface, EOS_HStats, InterfaceSlot::Stats)
EOSEMU_STUB_GETTER(EOS_Platform_GetLeaderboardsInterface, EOS_HLeaderboards, InterfaceSlot::Leaderboards)
EOSEMU_REAL_GETTER(EOS_Platform_GetModsInterface, EOS_HMods, Mods)
EOSEMU_STUB_GETTER(EOS_Platform_GetAntiCheatClientInterface, EOS_HAntiCheatClient, InterfaceSlot::AntiCheatClient)
EOSEMU_STUB_GETTER(EOS_Platform_GetAntiCheatServerInterface, EOS_HAntiCheatServer, InterfaceSlot::AntiCheatServer)
EOSEMU_STUB_GETTER(EOS_Platform_GetProgressionSnapshotInterface, EOS_HProgressionSnapshot, InterfaceSlot::ProgressionSnapshot)
EOSEMU_STUB_GETTER(EOS_Platform_GetReportsInterface, EOS_HReports, InterfaceSlot::Reports)
EOSEMU_STUB_GETTER(EOS_Platform_GetSanctionsInterface, EOS_HSanctions, InterfaceSlot::Sanctions)
EOSEMU_STUB_GETTER(EOS_Platform_GetKWSInterface, EOS_HKWS, InterfaceSlot::KWS)
EOSEMU_REAL_GETTER(EOS_Platform_GetCustomInvitesInterface, EOS_HCustomInvites, CustomInvites)
EOSEMU_STUB_GETTER(EOS_Platform_GetIntegratedPlatformInterface, EOS_HIntegratedPlatform, InterfaceSlot::IntegratedPlatform)

// ----------------------------------------------------------------------------
// Platform scalar/string getters.
// ----------------------------------------------------------------------------

namespace
{
	EOS_EResult CopyFixedString(const char* Value, char* OutBuffer, int32_t* InOutBufferLength)
	{
		if (OutBuffer == nullptr || InOutBufferLength == nullptr)
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		const int32_t Required = static_cast<int32_t>(std::strlen(Value)) + 1;
		if (*InOutBufferLength < Required)
		{
			*InOutBufferLength = Required;
			return EOS_EResult::EOS_LimitExceeded;
		}
		std::memcpy(OutBuffer, Value, Required);
		*InOutBufferLength = Required;
		return EOS_EResult::EOS_Success;
	}
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_GetActiveCountryCode(EOS_HPlatform Handle, EOS_EpicAccountId LocalUserId, char* OutBuffer, int32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	(void)LocalUserId;
	auto* P = AsPlatform(Handle);
	if (P == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return CopyFixedString(P->ActiveCountryCode().c_str(), OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_GetActiveLocaleCode(EOS_HPlatform Handle, EOS_EpicAccountId LocalUserId, char* OutBuffer, int32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	(void)LocalUserId;
	auto* P = AsPlatform(Handle);
	if (P == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return CopyFixedString(P->ActiveLocaleCode().c_str(), OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_GetOverrideCountryCode(EOS_HPlatform Handle, char* OutBuffer, int32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	auto* P = AsPlatform(Handle);
	if (P == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return CopyFixedString(P->OverrideCountryCode().c_str(), OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_GetOverrideLocaleCode(EOS_HPlatform Handle, char* OutBuffer, int32_t* InOutBufferLength)
{
	EOSEMU_API_TRACE();
	auto* P = AsPlatform(Handle);
	if (P == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return CopyFixedString(P->OverrideLocaleCode().c_str(), OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_SetOverrideCountryCode(EOS_HPlatform Handle, const char* NewCountryCode)
{
	EOSEMU_API_TRACE();
	auto* P = AsPlatform(Handle);
	if (P == nullptr) return EOS_EResult::EOS_InvalidParameters;
	P->SetOverrideCountryCode(NewCountryCode);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_SetOverrideLocaleCode(EOS_HPlatform Handle, const char* NewLocaleCode)
{
	EOSEMU_API_TRACE();
	auto* P = AsPlatform(Handle);
	if (P == nullptr) return EOS_EResult::EOS_InvalidParameters;
	P->SetOverrideLocaleCode(NewLocaleCode);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_CheckForLauncherAndRestart(EOS_HPlatform Handle)
{
	EOSEMU_API_TRACE();
	// We are not launched by the Epic Games Launcher and never need a restart.
	// The documented "no restart necessary" code is EOS_NoChange.
	return AsPlatform(Handle) ? EOS_EResult::EOS_NoChange : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_GetDesktopCrossplayStatus(EOS_HPlatform Handle, const EOS_Platform_GetDesktopCrossplayStatusOptions* Options, EOS_Platform_DesktopCrossplayStatusInfo* OutDesktopCrossplayStatusInfo)
{
	EOSEMU_API_TRACE();
	(void)Options;
	if (AsPlatform(Handle) == nullptr || OutDesktopCrossplayStatusInfo == nullptr)
	{
		return EOS_EResult::EOS_InvalidParameters;
	}
	OutDesktopCrossplayStatusInfo->Status = EOS_EDesktopCrossplayStatus::EOS_DCS_OK;
	OutDesktopCrossplayStatusInfo->ServiceInitResult = 0;
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_SetApplicationStatus(EOS_HPlatform Handle, const EOS_EApplicationStatus NewStatus)
{
	EOSEMU_API_TRACE();
	(void)NewStatus;
	return AsPlatform(Handle) ? EOS_EResult::EOS_Success : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_EApplicationStatus) EOS_Platform_GetApplicationStatus(EOS_HPlatform Handle)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	return EOS_EApplicationStatus::EOS_AS_Foreground;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_SetNetworkStatus(EOS_HPlatform Handle, const EOS_ENetworkStatus NewStatus)
{
	EOSEMU_API_TRACE();
	(void)NewStatus;
	return AsPlatform(Handle) ? EOS_EResult::EOS_Success : EOS_EResult::EOS_InvalidParameters;
}

EOS_DECLARE_FUNC(EOS_ENetworkStatus) EOS_Platform_GetNetworkStatus(EOS_HPlatform Handle)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	// LAN transport is always up as far as the consumer is concerned.
	return EOS_ENetworkStatus::EOS_NS_Online;
}

// These two are documented never-null (eos_types.h), so a stub returning
// nullptr would crash the first logging path -- implement them for real.
EOS_DECLARE_FUNC(const char*) EOS_EApplicationStatus_ToString(EOS_EApplicationStatus ApplicationStatus)
{
	switch (ApplicationStatus)
	{
	case EOS_EApplicationStatus::EOS_AS_BackgroundConstrained: return "EOS_AS_BackgroundConstrained";
	case EOS_EApplicationStatus::EOS_AS_BackgroundUnconstrained: return "EOS_AS_BackgroundUnconstrained";
	case EOS_EApplicationStatus::EOS_AS_BackgroundSuspended: return "EOS_AS_BackgroundSuspended";
	case EOS_EApplicationStatus::EOS_AS_Foreground: return "EOS_AS_Foreground";
	default: return "EOS_AS_Foreground";
	}
}

EOS_DECLARE_FUNC(const char*) EOS_ENetworkStatus_ToString(EOS_ENetworkStatus NetworkStatus)
{
	switch (NetworkStatus)
	{
	case EOS_ENetworkStatus::EOS_NS_Disabled: return "EOS_NS_Disabled";
	case EOS_ENetworkStatus::EOS_NS_Offline: return "EOS_NS_Offline";
	case EOS_ENetworkStatus::EOS_NS_Online: return "EOS_NS_Online";
	default: return "EOS_NS_Online";
	}
}
