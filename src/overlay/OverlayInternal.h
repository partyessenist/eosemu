#pragma once

//
// Internal, ImGui/Win32-aware definition of OverlayImpl. Shared by the two
// backend translation units that render the social overlay:
//
//   * Overlay.cpp       -- the separate-window fallback (its own Win32 window +
//                          D3D11 device on a background thread).
//   * SwapchainHook.cpp -- the in-game backend that hooks the game's
//                          IDXGISwapChain::Present and draws on the game's device.
//
// Only these two TUs (both gated on EOSEMU_ENABLE_OVERLAY) include this header,
// so no ImGui or Win32 type ever reaches an exported signature or a generated
// stub TU -- the same rule the Overlay.h facade documents. The two backends are
// mutually exclusive at runtime: Start() installs the hook and, only if that
// fails, falls back to the window.
//
// The shared state (visibility request, toggle hotkey, toasts, social item /
// action queues) lives on OverlayImpl and is the single source of truth both
// backends read. The panel-drawing members operate on whatever ImGui context is
// current, so each backend can own its own context on its own device.
//

#include "overlay/Overlay.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace EOSEmu
{
	class Platform;

	// In-game swapchain-hook backend state. Defined and driven entirely by
	// SwapchainHook.cpp; OverlayImpl only owns the object and asks it to
	// install / uninstall. Kept out of OverlayImpl's own header footprint so the
	// windowed path never touches DXGI hooking.
	struct HookState;

	struct OverlayImpl
	{
		explicit OverlayImpl(Platform& P) : Plat(P) {}

		Platform& Plat;

		// --- shared state (read by both backends) ------------------------------
		std::atomic<bool> WantVisible{false};
		std::atomic<bool> Exclusive{false};

		// Global toggle shortcut (decoded Win32 vk + modifiers).
		std::atomic<int> Vk{0};
		std::atomic<bool> ModShift{false};
		std::atomic<bool> ModCtrl{false};
		std::atomic<bool> ModAlt{false};
		bool HotkeyWasDown = false;

		// User-driven visibility change, drained by the EOS_UI interface in Tick.
		std::mutex ReqMutex;
		bool HasRequest = false;
		bool RequestVisible = false;

		struct Toast { std::string Text; std::chrono::steady_clock::time_point Expiry; };
		std::mutex ToastMutex;
		std::vector<Toast> Toasts;

		// Social panel: item list pushed by the UI interface each Tick, action
		// queue appended by the active backend when the user clicks a button.
		std::mutex SocialMutex;
		std::vector<OverlaySocialItem> SocialItems;
		std::vector<OverlaySocialAction> SocialActions;

		// --- separate-window backend (Overlay.cpp) -----------------------------
		std::thread Thread;
		std::atomic<bool> Running{false};
		ID3D11Device* Device = nullptr;
		ID3D11DeviceContext* Context = nullptr;
		IDXGISwapChain* Swapchain = nullptr;
		ID3D11RenderTargetView* Rtv = nullptr;
		HWND Hwnd = nullptr;
		UINT ResizeW = 0, ResizeH = 0;

		// --- in-game hook backend (SwapchainHook.cpp) --------------------------
		std::atomic<bool> HookActive{false}; // vtable patched and armed
		HookState* Hook = nullptr;

		// --- shared helpers ----------------------------------------------------
		void RequestToggle()
		{
			const bool Target = !WantVisible.load();
			std::lock_guard<std::mutex> Lock(ReqMutex);
			HasRequest = true;
			RequestVisible = Target;
		}
		void RequestHide()
		{
			{
				std::lock_guard<std::mutex> Lock(ReqMutex);
				HasRequest = true;
				RequestVisible = false;
			}
			WantVisible.store(false);
			if (Hwnd) ShowWindow(Hwnd, SW_HIDE);
		}
		void QueueAction(OverlaySocialKind Kind, OverlayActionKind Action, const std::string& Id)
		{
			std::lock_guard<std::mutex> Lock(SocialMutex);
			SocialActions.push_back({Kind, Action, Id});
		}

		// Rising-edge toggle-hotkey poll. Called by whichever backend has a frame
		// loop (the window thread, or the hooked Present). Safe on any thread.
		void PollHotkey();

		// --- shared panel drawing (operate on the current ImGui context) -------
		// InGame=false: the separate window fills its client area with an opaque
		// panel. InGame=true: draw a translucent, movable panel over the game.
		void DrawFriendsPanel(bool InGame);
		void DrawSocialPanel();
		void DrawToasts();

		// --- separate-window backend entry points (Overlay.cpp) ----------------
		void ThreadMain();
		bool CreateDevice();
		void CleanupDevice();
		void CreateRtv();
		void CleanupRtv();
		void RenderFrame();

		// --- in-game hook backend entry points (SwapchainHook.cpp) -------------
		// Patches the shared IDXGISwapChain vtable so the game's Present is
		// intercepted. Returns false (leaving nothing patched) if a swapchain
		// vtable can't be discovered or another overlay already owns the hook.
		bool TryStartHook();
		// Restores the vtable, tears down the ImGui backend and WndProc hook.
		void StopHook();
	};
}
