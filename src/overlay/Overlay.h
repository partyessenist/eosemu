#pragma once

//
// Social overlay -- separate-window Dear ImGui implementation.
//
// The real EOS overlay injects into the game's swapchain. As a low-risk first
// cut (CLAUDE.md: "A windowed-only or separate-window fallback is an acceptable
// first cut"), EOSEmu instead stands up its OWN top-level window with its own
// D3D11 device on a background thread and renders the friends panel there. It
// never touches the game's present path, so a game whose overlay never appears
// still runs unaffected.
//
// This facade is deliberately free of any ImGui or Win32 type: those live only
// in Overlay.cpp (behind EOSEMU_ENABLE_OVERLAY), so no ImGui header ever reaches
// an exported signature or a generated stub TU. When the overlay is compiled out
// (headless/server build) every method is a no-op and Available() is false.
//
// Threading: the window + render loop own a dedicated thread. This class only
// exchanges small atomic state with it. Consumer callbacks are NOT fired from
// here -- the EOS_UI interface drains user-driven state during EOS_Platform_Tick
// and dispatches callbacks on the caller's thread (the Tick-thread rule).
//

#include <string>
#include <vector>

namespace EOSEmu
{
	class Platform;
	struct OverlayImpl; // defined in Overlay.cpp (Win32/D3D11/ImGui state)

	// --- social panel model (invite-accept / join-friend) -------------------
	//
	// Plain, ImGui-free descriptions of the rows the overlay's "Invitations &
	// friends" panel shows, and the actions the user takes on them. The UI
	// interface produces the item list once per Tick and drains the action
	// queue; the render thread only reads items and appends actions. Everything
	// crossing the thread boundary is a std::string so no interned pointer or
	// interface state is shared with the render thread.

	enum class OverlaySocialKind
	{
		LobbyInvite,   // a received lobby invite; Id is the invite id
		SessionInvite, // a received session invite; Id is the invite id
		LobbyJoin,     // a discovered joinable lobby; Id is the lobby id
		SessionJoin,   // a discovered joinable session; Id is the session id
	};

	enum class OverlayActionKind { Accept, Reject, Join };

	struct OverlaySocialItem
	{
		OverlaySocialKind Kind;
		std::string Id;    // invite id (invites) or lobby/session id (joins)
		std::string Label; // human-readable row text
	};

	struct OverlaySocialAction
	{
		OverlaySocialKind Kind;
		OverlayActionKind Action;
		std::string Id;
	};

	class Overlay
	{
	public:
		explicit Overlay(Platform& Owner);
		~Overlay();

		Overlay(const Overlay&) = delete;
		Overlay& operator=(const Overlay&) = delete;

		/// Brings up the window + render thread. Does nothing if the overlay was
		/// compiled out or the game opted out via platform flags (EnabledByFlags
		/// false).
		void Start(bool EnabledByFlags);
		void Stop();

		/// True when built with overlay support and the render thread is live.
		bool Available() const;

		/// Requests the render thread show or hide its window. The authoritative
		/// visibility state is owned by the EOS_UI interface; this only reflects
		/// it onto the window.
		void SetVisible(bool Visible);

		/// True only while the overlay window is visible AND the foreground
		/// window -- i.e. it is actually consuming keyboard/mouse input.
		bool IsExclusiveInput() const;

		/// Sets the global toggle shortcut as a decoded Win32 virtual-key plus
		/// modifier flags. VkKey 0 disables the shortcut.
		void SetToggleShortcut(int VkKey, bool Shift, bool Ctrl, bool Alt);

		/// If the user changed the desired visibility since the last call (via the
		/// toggle shortcut or by closing the window), returns true and writes the
		/// requested state to OutVisible. Drained from EOS_Platform_Tick.
		bool ConsumeUserVisibilityRequest(bool& OutVisible);

		/// Shows a transient message in the overlay (invite toast, or a
		/// block/report/profile action banner).
		void PushToast(const std::string& Text);

		/// Replaces the social panel's row list (pending invites + joinable
		/// peers). Called once per Tick by the UI interface. No-op without an
		/// overlay window.
		void SetSocialItems(std::vector<OverlaySocialItem> Items);

		/// Moves any Accept/Reject/Join actions the user triggered since the last
		/// call into Out and returns true if there were any. Drained from
		/// EOS_Platform_Tick; the UI interface routes each to Lobby/Sessions.
		bool ConsumeSocialActions(std::vector<OverlaySocialAction>& Out);

	private:
		OverlayImpl* Impl_ = nullptr; // null when compiled without overlay support
		Platform& Platform_;
	};
}
