#pragma once

//
// UI interface -- the social overlay's public surface (eos_ui.h).
//
// Holds the overlay's logical state (visibility, toggle key/button, display
// preference, pause) and drives the separate-window Dear ImGui overlay
// (src/overlay/Overlay). All completion + notification callbacks obey the
// Tick-thread rule: PumpTick() runs once per EOS_Platform_Tick (before the
// dispatcher drains) to reconcile user-driven overlay state and queue
// notifications; nothing here calls back into the game from the render thread.
//

#include <set>
#include <string>

#include "interfaces/Common.h"
#include "interfaces/Interfaces.h"

#include "eos_ui_types.h"

namespace EOSEmu
{
	class UIInterface : public InterfaceBase
	{
	public:
		explicit UIInterface(Platform& Owner);

		void ShowFriends(EOS_EpicAccountId Local, void* ClientData, EOS_UI_OnShowFriendsCallback Cb);
		void HideFriends(EOS_EpicAccountId Local, void* ClientData, EOS_UI_OnHideFriendsCallback Cb);
		bool FriendsVisible() const { return Visible_; }
		bool ExclusiveInput() const;

		EOS_EResult SetToggleKey(EOS_UI_EKeyCombination Key);
		EOS_UI_EKeyCombination ToggleKey() const { return ToggleKey_; }
		static bool IsValidKeyCombination(EOS_UI_EKeyCombination Key);

		EOS_EResult SetToggleButton(EOS_UI_EInputStateButtonFlags Button);
		EOS_UI_EInputStateButtonFlags ToggleButton() const { return ToggleButton_; }

		EOS_EResult SetDisplayPreference(EOS_UI_ENotificationLocation Location);
		EOS_UI_ENotificationLocation NotificationLocation() const { return NotifyLocation_; }

		EOS_EResult PauseSocialOverlay(bool Paused);
		bool IsPaused() const { return Paused_; }

		/// Shared entry for ShowBlockPlayer/ShowReportPlayer/ShowNativeProfile:
		/// surfaces a banner in the overlay and shows it. Kind is a short label.
		void ShowPlayerModal(const char* Kind, EOS_EpicAccountId Target);

		/// Reconciles user-driven overlay state (toggle key / window close) and
		/// queues display-settings notifications. Called from EOS_Platform_Tick.
		void PumpTick();

		/// Releases a UiEventId minted for a join-friend flow (routes to both
		/// Lobby and Sessions; only the minting interface holds it).
		void AcknowledgeEventId(uint64_t UiEventId);

		NotifyRegistry<EOS_UI_OnDisplaySettingsUpdatedCallback> DisplayNotifies_;
		NotifyRegistry<EOS_UI_OnMemoryMonitorCallback> MemoryNotifies_;

		// Set when a display-settings handler registers so the next PumpTick fires
		// it with the current state (per the header's contract).
		bool PendingInitialNotify_ = false;

	private:
		void ApplyToggleShortcut();
		void SetVisibleInternal(bool Visible);
		void FireDisplaySettings(bool Visible, bool Exclusive);

		/// Collects pending invites + joinable games from Lobby/Sessions, pushes
		/// them to the overlay for display, drains the overlay's Accept/Reject/Join
		/// actions (plus any auto actions from EOSEMU_OVERLAY_AUTO{ACCEPT,JOIN}),
		/// and routes each to the owning interface. Runs every Tick, pause or not.
		void PumpSocial();

		/// Best-effort display name for a product-id string via the peer directory
		/// (falls back to a short id prefix). Used to label overlay invite rows.
		std::string NameForPid(const std::string& Pid) const;

		bool Visible_ = false;
		bool Paused_ = false;
		bool WasVisibleBeforePause_ = false;
		bool LastVisible_ = false;
		bool LastExclusive_ = false;

		// Test/automation hooks: auto-accept every received invite, and/or
		// auto-join every discovered game, without a human at the overlay. Read
		// once from EOSEMU_OVERLAY_AUTOACCEPT / EOSEMU_OVERLAY_AUTOJOIN. The
		// Handled_ sets dedupe so an invite/game is actioned once, not every Tick.
		bool AutoAccept_ = false;
		bool AutoJoin_ = false;
		std::set<std::string> HandledInvites_;
		std::set<std::string> HandledJoins_;
		EOS_UI_EKeyCombination ToggleKey_;
		EOS_UI_EInputStateButtonFlags ToggleButton_ = EOS_UI_EInputStateButtonFlags::EOS_UISBF_None;
		EOS_UI_ENotificationLocation NotifyLocation_ = EOS_UI_ENotificationLocation::EOS_UNL_BottomRight;
	};
}
