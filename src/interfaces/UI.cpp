//
// UI interface implementation. See UI.h and src/overlay/Overlay.h.
//

#include "interfaces/UI.h"

#include "interfaces/UserInfo.h"
#include "interfaces/Lobby.h"
#include "interfaces/Sessions.h"
#include "overlay/Overlay.h"

#include "core/Ids.h"
#include "core/Logging.h"
#include "core/Peers.h"
#include "core/Platform.h"

#include "eos_ui.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace EOSEmu
{
	namespace
	{
		// Reads a boolean-ish env var ("1"/nonzero-first-char => true). Mirrors
		// the getenv handling used elsewhere (Stub.cpp / P2P.cpp) so the overlay's
		// automation hooks match the existing env-var convention.
		bool EnvFlag(const char* Name)
		{
#if defined(_MSC_VER)
			size_t Length = 0;
			char Buffer[8] = {};
			if (getenv_s(&Length, Buffer, sizeof(Buffer), Name) != 0 || Length == 0) return false;
			return Buffer[0] != '0';
#else
			const char* Value = std::getenv(Name);
			return Value != nullptr && Value[0] != '0';
#endif
		}

		using KeyCombo = EOS_UI_EKeyCombination;
		// EOS key-combination fields as plain ints (the enum is an enum class, so
		// its enumerators need qualification; naming them once keeps the logic
		// below readable).
		constexpr int kEosShift = static_cast<int>(KeyCombo::EOS_UIK_Shift);
		constexpr int kEosCtrl = static_cast<int>(KeyCombo::EOS_UIK_Control);
		constexpr int kEosAlt = static_cast<int>(KeyCombo::EOS_UIK_Alt);
		constexpr int kEosMeta = static_cast<int>(KeyCombo::EOS_UIK_Meta);
		constexpr int kEosKeyMask = static_cast<int>(KeyCombo::EOS_UIK_KeyTypeMask);
		constexpr int kEosF1 = static_cast<int>(KeyCombo::EOS_UIK_F1);
		constexpr int kEosF3 = static_cast<int>(KeyCombo::EOS_UIK_F3);
		constexpr int kEosF12 = static_cast<int>(KeyCombo::EOS_UIK_F12);
		constexpr int kEosSpace = static_cast<int>(KeyCombo::EOS_UIK_Space);
		constexpr int kEosBackspace = static_cast<int>(KeyCombo::EOS_UIK_Backspace);
		constexpr int kEosEscape = static_cast<int>(KeyCombo::EOS_UIK_Escape);
		constexpr int kEosTab = static_cast<int>(KeyCombo::EOS_UIK_Tab);
		constexpr int kDefaultToggle = kEosShift | kEosF3; // Shift+F3

		// Win32 virtual-key codes, hardcoded so this TU stays platform-agnostic
		// (it must compile in a headless build with no overlay / no <windows.h>).
		constexpr int kVkBack = 0x08, kVkTab = 0x09, kVkEsc = 0x1B, kVkSpace = 0x20, kVkF1 = 0x70;

		// Maps the single-key portion of an EOS key combination to a Win32 vk.
		// Only the combinations the header deems valid need to resolve.
		int EosKeyToVk(int KeyType)
		{
			if (KeyType >= kEosF1 && KeyType <= kEosF12) return kVkF1 + (KeyType - kEosF1);
			if (KeyType == kEosSpace) return kVkSpace;
			if (KeyType == kEosBackspace) return kVkBack;
			if (KeyType == kEosEscape) return kVkEsc;
			if (KeyType == kEosTab) return kVkTab;
			return 0;
		}
	}

	UIInterface::UIInterface(Platform& Owner) : InterfaceBase(Owner)
	{
		ToggleKey_ = static_cast<EOS_UI_EKeyCombination>(kDefaultToggle); // Shift+F3
		ApplyToggleShortcut();
		AutoAccept_ = EnvFlag("EOSEMU_OVERLAY_AUTOACCEPT");
		AutoJoin_ = EnvFlag("EOSEMU_OVERLAY_AUTOJOIN");
	}

	bool UIInterface::ExclusiveInput() const
	{
		return Platform_.Overlay().IsExclusiveInput();
	}

	void UIInterface::ApplyToggleShortcut()
	{
		const int Combo = static_cast<int>(ToggleKey_);
		const bool Shift = (Combo & kEosShift) != 0;
		const bool Ctrl = (Combo & kEosCtrl) != 0;
		const bool Alt = (Combo & kEosAlt) != 0;
		const int KeyType = Combo & kEosKeyMask;
		Platform_.Overlay().SetToggleShortcut(EosKeyToVk(KeyType), Shift, Ctrl, Alt);
	}

	void UIInterface::SetVisibleInternal(bool Visible)
	{
		if (Visible_ == Visible) return;
		Visible_ = Visible;
		Platform_.Overlay().SetVisible(Visible);
	}

	bool UIInterface::IsValidKeyCombination(EOS_UI_EKeyCombination Key)
	{
		const int Combo = static_cast<int>(Key);
		const int Mods = kEosShift | kEosCtrl | kEosAlt | kEosMeta;
		const bool HasModifier = (Combo & Mods) != 0;
		const int KeyType = Combo & kEosKeyMask;
		const bool KeyOk =
			(KeyType >= kEosF1 && KeyType <= kEosF12) ||
			KeyType == kEosSpace || KeyType == kEosBackspace ||
			KeyType == kEosEscape || KeyType == kEosTab;
		return HasModifier && KeyOk;
	}

	EOS_EResult UIInterface::SetToggleKey(EOS_UI_EKeyCombination Key)
	{
		if (static_cast<int>(Key) == 0) // EOS_UIK_None
		{
			// Documented: revert to the system default.
			Key = static_cast<EOS_UI_EKeyCombination>(kDefaultToggle);
		}
		else if (!IsValidKeyCombination(Key))
		{
			return EOS_EResult::EOS_InvalidParameters;
		}
		if (Key == ToggleKey_) return EOS_EResult::EOS_NoChange;
		ToggleKey_ = Key;
		ApplyToggleShortcut();
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult UIInterface::SetToggleButton(EOS_UI_EInputStateButtonFlags Button)
	{
		if (Button == ToggleButton_) return EOS_EResult::EOS_NoChange;
		ToggleButton_ = Button;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult UIInterface::SetDisplayPreference(EOS_UI_ENotificationLocation Location)
	{
		if (Location == NotifyLocation_) return EOS_EResult::EOS_NoChange;
		NotifyLocation_ = Location;
		return EOS_EResult::EOS_Success;
	}

	EOS_EResult UIInterface::PauseSocialOverlay(bool Paused)
	{
		if (Paused && !Paused_)
		{
			WasVisibleBeforePause_ = Visible_;
			if (Visible_) SetVisibleInternal(false);
			Paused_ = true;
		}
		else if (!Paused && Paused_)
		{
			Paused_ = false;
			if (WasVisibleBeforePause_) SetVisibleInternal(true);
			PendingInitialNotify_ = true; // flush the delayed display-settings notify
		}
		return EOS_EResult::EOS_Success;
	}

	void UIInterface::ShowFriends(EOS_EpicAccountId Local, void* ClientData, EOS_UI_OnShowFriendsCallback Cb)
	{
		if (!Platform_.Overlay().Available())
		{
			// No overlay exists (disabled by flag, headless build, or its
			// thread failed to init). Claiming visibility anyway soft-locks a
			// game that pauses while the overlay is up: nothing renders and no
			// user action can ever produce the hide event. eos_ui.h documents
			// EOS_NotConfigured for exactly this.
			if (Cb == nullptr) return;
			Platform_.Dispatch().Post([Cb, ClientData, Local]
			{
				EOS_UI_ShowFriendsCallbackInfo Info = {};
				Info.ResultCode = EOS_EResult::EOS_NotConfigured;
				Info.ClientData = ClientData;
				Info.LocalUserId = Local;
				Cb(&Info);
			});
			return;
		}
		const bool AlreadyVisible = Visible_;
		if (!Paused_) SetVisibleInternal(true);
		else WasVisibleBeforePause_ = true; // documented: show once unpaused
		if (Cb == nullptr) return;
		const EOS_EResult Result = AlreadyVisible ? EOS_EResult::EOS_NoChange : EOS_EResult::EOS_Success;
		Platform_.Dispatch().Post([Cb, ClientData, Local, Result]
		{
			EOS_UI_ShowFriendsCallbackInfo Info = {};
			Info.ResultCode = Result;
			Info.ClientData = ClientData;
			Info.LocalUserId = Local;
			Cb(&Info);
		});
	}

	void UIInterface::HideFriends(EOS_EpicAccountId Local, void* ClientData, EOS_UI_OnHideFriendsCallback Cb)
	{
		const bool WasVisible = Visible_;
		SetVisibleInternal(false);
		if (Cb == nullptr) return;
		const EOS_EResult Result = WasVisible ? EOS_EResult::EOS_Success : EOS_EResult::EOS_NoChange;
		Platform_.Dispatch().Post([Cb, ClientData, Local, Result]
		{
			EOS_UI_HideFriendsCallbackInfo Info = {};
			Info.ResultCode = Result;
			Info.ClientData = ClientData;
			Info.LocalUserId = Local;
			Cb(&Info);
		});
	}

	void UIInterface::ShowPlayerModal(const char* Kind, EOS_EpicAccountId Target)
	{
		std::string Name = Platform_.UserInfo().DisplayNameFor(Target);
		if (Name.empty()) Name = "player";
		Platform_.Overlay().PushToast(std::string(Kind) + ": " + Name);
		if (!Paused_) SetVisibleInternal(true);
	}

	void UIInterface::FireDisplaySettings(bool Visible, bool Exclusive)
	{
		for (const auto& E : DisplayNotifies_.Snapshot())
		{
			auto Fn = E.Fn;
			void* Cd = E.ClientData;
			Platform_.Dispatch().Post([Fn, Cd, Visible, Exclusive]
			{
				EOS_UI_OnDisplaySettingsUpdatedCallbackInfo Info = {};
				Info.ClientData = Cd;
				Info.bIsVisible = Visible ? EOS_TRUE : EOS_FALSE;
				Info.bIsExclusiveInput = Exclusive ? EOS_TRUE : EOS_FALSE;
				Fn(&Info);
			});
		}
	}

	std::string UIInterface::NameForPid(const std::string& Pid) const
	{
		PeerInfo Info;
		if (Platform_.Peers().Find(Ids::InternProduct(Pid), Info) && !Info.DisplayName.empty())
			return Info.DisplayName;
		return Pid.substr(0, 8);
	}

	void UIInterface::PumpSocial()
	{
		LobbyInterface& Lob = Platform_.Lobby();
		SessionsInterface& Ses = Platform_.Sessions();
		class Overlay& Ov = Platform_.Overlay();

		// Snapshot pending invites + joinable games. Both interfaces already hide
		// anything we've joined, so a completed accept/join drops off the list.
		const std::vector<LobbyInviteView> LInv = Lob.SnapshotInvites();
		const std::vector<LobbyJoinableView> LJoin = Lob.SnapshotJoinable();
		const std::vector<SessionInviteView> SInv = Ses.SnapshotInvites();
		const std::vector<SessionJoinableView> SJoin = Ses.SnapshotJoinable();

		// Publish rows for the overlay to render.
		if (Ov.Available())
		{
			std::vector<OverlaySocialItem> Items;
			for (const auto& I : LInv) Items.push_back({OverlaySocialKind::LobbyInvite, I.InviteId, NameForPid(I.FromPid) + " invited you to a lobby"});
			for (const auto& I : SInv) Items.push_back({OverlaySocialKind::SessionInvite, I.InviteId, NameForPid(I.FromPid) + " invited you to a session"});
			for (const auto& J : LJoin) Items.push_back({OverlaySocialKind::LobbyJoin, J.LobbyId, "Join " + NameForPid(J.OwnerPid) + "'s lobby"});
			for (const auto& J : SJoin) Items.push_back({OverlaySocialKind::SessionJoin, J.SessionId, "Join " + NameForPid(J.OwnerPid) + "'s session"});
			Ov.SetSocialItems(std::move(Items));
		}

		// Collect user actions from the overlay, then synthesize auto actions for
		// the headless automation hooks (dedup keyed so each fires exactly once).
		std::vector<OverlaySocialAction> Actions;
		if (Ov.Available()) Ov.ConsumeSocialActions(Actions);
		if (AutoAccept_)
		{
			for (const auto& I : LInv) if (HandledInvites_.insert("L:" + I.InviteId).second) Actions.push_back({OverlaySocialKind::LobbyInvite, OverlayActionKind::Accept, I.InviteId});
			for (const auto& I : SInv) if (HandledInvites_.insert("S:" + I.InviteId).second) Actions.push_back({OverlaySocialKind::SessionInvite, OverlayActionKind::Accept, I.InviteId});
		}
		if (AutoJoin_)
		{
			for (const auto& J : LJoin) if (HandledJoins_.insert("L:" + J.LobbyId).second) Actions.push_back({OverlaySocialKind::LobbyJoin, OverlayActionKind::Join, J.LobbyId});
			for (const auto& J : SJoin) if (HandledJoins_.insert("S:" + J.SessionId).second) Actions.push_back({OverlaySocialKind::SessionJoin, OverlayActionKind::Join, J.SessionId});
		}

		for (const OverlaySocialAction& A : Actions)
		{
			switch (A.Kind)
			{
			case OverlaySocialKind::LobbyInvite:
				if (A.Action == OverlayActionKind::Reject) Lob.RejectInviteFromOverlay(A.Id);
				else Lob.AcceptInviteFromOverlay(A.Id);
				break;
			case OverlaySocialKind::SessionInvite:
				if (A.Action == OverlayActionKind::Reject) Ses.RejectInviteFromOverlay(A.Id);
				else Ses.AcceptInviteFromOverlay(A.Id);
				break;
			case OverlaySocialKind::LobbyJoin:
				Lob.BeginJoinFromOverlay(A.Id);
				break;
			case OverlaySocialKind::SessionJoin:
				Ses.BeginJoinFromOverlay(A.Id);
				break;
			}
		}
	}

	void UIInterface::AcknowledgeEventId(uint64_t UiEventId)
	{
		// The event lives in exactly one interface; ask both, the other no-ops.
		Platform_.Lobby().AcknowledgeUiEvent(UiEventId);
		Platform_.Sessions().AcknowledgeUiEvent(UiEventId);
	}

	void UIInterface::PumpTick()
	{
		class Overlay& Ov = Platform_.Overlay();

		// Invite-accept / join-friend routing runs every Tick regardless of pause
		// (auto hooks must work headless; real clicks can't arrive while hidden).
		PumpSocial();

		// Drain user-driven visibility changes (toggle key, window close). While
		// paused, key/button events are swallowed (header contract).
		bool Requested = false;
		while (Ov.ConsumeUserVisibilityRequest(Requested))
		{
			if (!Paused_) SetVisibleInternal(Requested);
		}

		if (Paused_) return; // notifications are delayed while paused

		const bool Exclusive = Ov.IsExclusiveInput();
		if (PendingInitialNotify_ || Visible_ != LastVisible_ || Exclusive != LastExclusive_)
		{
			FireDisplaySettings(Visible_, Exclusive);
			LastVisible_ = Visible_;
			LastExclusive_ = Exclusive;
			PendingInitialNotify_ = false;
		}
	}
}

using namespace EOSEmu;

// ----------------------------------------------------------------------------
// Friends visibility.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_UI_ShowFriends(EOS_HUI Handle, const EOS_UI_ShowFriendsOptions* Options, void* ClientData, const EOS_UI_OnShowFriendsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<UIInterface>(Handle);
	if (I == nullptr) return;
	I->ShowFriends(Options ? Options->LocalUserId : nullptr, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_UI_HideFriends(EOS_HUI Handle, const EOS_UI_HideFriendsOptions* Options, void* ClientData, const EOS_UI_OnHideFriendsCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<UIInterface>(Handle);
	if (I == nullptr) return;
	I->HideFriends(Options ? Options->LocalUserId : nullptr, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_GetFriendsVisible(EOS_HUI Handle, const EOS_UI_GetFriendsVisibleOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<UIInterface>(Handle);
	return (I != nullptr && I->FriendsVisible()) ? EOS_TRUE : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_GetFriendsExclusiveInput(EOS_HUI Handle, const EOS_UI_GetFriendsExclusiveInputOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<UIInterface>(Handle);
	return (I != nullptr && I->ExclusiveInput()) ? EOS_TRUE : EOS_FALSE;
}

// ----------------------------------------------------------------------------
// Display-settings notifications.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_UI_AddNotifyDisplaySettingsUpdated(EOS_HUI Handle, const EOS_UI_AddNotifyDisplaySettingsUpdatedOptions* Options, void* ClientData, const EOS_UI_OnDisplaySettingsUpdatedCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<UIInterface>(Handle);
	if (I == nullptr) return EOS_INVALID_NOTIFICATIONID;
	const EOS_NotificationId Id = I->DisplayNotifies_.Add(NotificationFn, ClientData);
	if (Id != EOS_INVALID_NOTIFICATIONID) I->PendingInitialNotify_ = true; // fire next tick
	return Id;
}

EOS_DECLARE_FUNC(void) EOS_UI_RemoveNotifyDisplaySettingsUpdated(EOS_HUI Handle, EOS_NotificationId Id)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<UIInterface>(Handle)) I->DisplayNotifies_.Remove(Id);
}

// ----------------------------------------------------------------------------
// Toggle key / button.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_SetToggleFriendsKey(EOS_HUI Handle, const EOS_UI_SetToggleFriendsKeyOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<UIInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->SetToggleKey(Options->KeyCombination);
}

EOS_DECLARE_FUNC(EOS_UI_EKeyCombination) EOS_UI_GetToggleFriendsKey(EOS_HUI Handle, const EOS_UI_GetToggleFriendsKeyOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<UIInterface>(Handle);
	return I ? I->ToggleKey() : EOS_UI_EKeyCombination::EOS_UIK_None;
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_IsValidKeyCombination(EOS_HUI Handle, EOS_UI_EKeyCombination KeyCombination)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	return UIInterface::IsValidKeyCombination(KeyCombination) ? EOS_TRUE : EOS_FALSE;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_SetToggleFriendsButton(EOS_HUI Handle, const EOS_UI_SetToggleFriendsButtonOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<UIInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->SetToggleButton(Options->ButtonCombination);
}

EOS_DECLARE_FUNC(EOS_UI_EInputStateButtonFlags) EOS_UI_GetToggleFriendsButton(EOS_HUI Handle, const EOS_UI_GetToggleFriendsButtonOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<UIInterface>(Handle);
	return I ? I->ToggleButton() : EOS_UI_EInputStateButtonFlags::EOS_UISBF_None;
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_IsValidButtonCombination(EOS_HUI Handle, EOS_UI_EInputStateButtonFlags ButtonCombination)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)ButtonCombination;
	// Any button set (including None, meaning "no toggle button") is accepted.
	return EOS_TRUE;
}

// ----------------------------------------------------------------------------
// Display preference / pause / events.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_SetDisplayPreference(EOS_HUI Handle, const EOS_UI_SetDisplayPreferenceOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<UIInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->SetDisplayPreference(Options->NotificationLocation);
}

EOS_DECLARE_FUNC(EOS_UI_ENotificationLocation) EOS_UI_GetNotificationLocationPreference(EOS_HUI Handle)
{
	EOSEMU_API_TRACE();
	auto* I = As<UIInterface>(Handle);
	return I ? I->NotificationLocation() : EOS_UI_ENotificationLocation::EOS_UNL_BottomRight;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_AcknowledgeEventId(EOS_HUI Handle, const EOS_UI_AcknowledgeEventIdOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<UIInterface>(Handle);
	if (I == nullptr) return EOS_EResult::EOS_InvalidParameters;
	// Releases the join-friend UiEventId the game was handed (JoinLobbyAccepted /
	// JoinSessionAccepted). Unknown/zero ids simply free nothing.
	if (Options != nullptr) I->AcknowledgeEventId(Options->UiEventId);
	return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_ReportInputState(EOS_HUI Handle, const EOS_UI_ReportInputStateOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	// Header: "empty implementation (i.e. returns EOS_NotImplemented) on all
	// non-console platforms." Match that exactly.
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_PrePresent(EOS_HUI Handle, const EOS_UI_PrePresentOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Handle; (void)Options;
	// Console-only; documented to return EOS_NotImplemented on desktop.
	return EOS_EResult::EOS_NotImplemented;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UI_PauseSocialOverlay(EOS_HUI Handle, const EOS_UI_PauseSocialOverlayOptions* Options)
{
	EOSEMU_API_TRACE();
	auto* I = As<UIInterface>(Handle);
	if (I == nullptr || Options == nullptr) return EOS_EResult::EOS_InvalidParameters;
	return I->PauseSocialOverlay(Options->bIsPaused == EOS_TRUE);
}

EOS_DECLARE_FUNC(EOS_Bool) EOS_UI_IsSocialOverlayPaused(EOS_HUI Handle, const EOS_UI_IsSocialOverlayPausedOptions* Options)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<UIInterface>(Handle);
	return (I != nullptr && I->IsPaused()) ? EOS_TRUE : EOS_FALSE;
}

// ----------------------------------------------------------------------------
// Player action flows.
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(void) EOS_UI_ShowBlockPlayer(EOS_HUI Handle, const EOS_UI_ShowBlockPlayerOptions* Options, void* ClientData, const EOS_UI_OnShowBlockPlayerCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<UIInterface>(Handle);
	if (I == nullptr) return;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	EOS_EpicAccountId Target = Options ? Options->TargetUserId : nullptr;
	I->ShowPlayerModal("Block", Target);
	if (CompletionDelegate == nullptr) return;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, Target]
	{
		EOS_UI_OnShowBlockPlayerCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.TargetUserId = Target;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_UI_ShowReportPlayer(EOS_HUI Handle, const EOS_UI_ShowReportPlayerOptions* Options, void* ClientData, const EOS_UI_OnShowReportPlayerCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<UIInterface>(Handle);
	if (I == nullptr) return;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	EOS_EpicAccountId Target = Options ? Options->TargetUserId : nullptr;
	I->ShowPlayerModal("Report", Target);
	if (CompletionDelegate == nullptr) return;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, Target]
	{
		EOS_UI_OnShowReportPlayerCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.TargetUserId = Target;
		CompletionDelegate(&Info);
	});
}

EOS_DECLARE_FUNC(void) EOS_UI_ShowNativeProfile(EOS_HUI Handle, const EOS_UI_ShowNativeProfileOptions* Options, void* ClientData, const EOS_UI_OnShowNativeProfileCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	auto* I = As<UIInterface>(Handle);
	if (I == nullptr) return;
	EOS_EpicAccountId Local = Options ? Options->LocalUserId : nullptr;
	EOS_EpicAccountId Target = Options ? Options->TargetUserId : nullptr;
	I->ShowPlayerModal("Profile", Target);
	if (CompletionDelegate == nullptr) return;
	I->Owner().Dispatch().Post([CompletionDelegate, ClientData, Local, Target]
	{
		EOS_UI_ShowNativeProfileCallbackInfo Info = {};
		Info.ResultCode = EOS_EResult::EOS_Success;
		Info.ClientData = ClientData;
		Info.LocalUserId = Local;
		Info.TargetUserId = Target;
		CompletionDelegate(&Info);
	});
}

// ----------------------------------------------------------------------------
// Memory monitor (registered but never fired -- EOSEmu has no such telemetry).
// ----------------------------------------------------------------------------

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_UI_AddNotifyMemoryMonitor(EOS_HUI Handle, const EOS_UI_AddNotifyMemoryMonitorOptions* Options, void* ClientData, const EOS_UI_OnMemoryMonitorCallback NotificationFn)
{
	EOSEMU_API_TRACE();
	(void)Options;
	auto* I = As<UIInterface>(Handle);
	return I ? I->MemoryNotifies_.Add(NotificationFn, ClientData) : EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_UI_RemoveNotifyMemoryMonitor(EOS_HUI Handle, EOS_NotificationId Id)
{
	EOSEMU_API_TRACE();
	if (auto* I = As<UIInterface>(Handle)) I->MemoryNotifies_.Remove(Id);
}
