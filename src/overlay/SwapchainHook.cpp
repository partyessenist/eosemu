//
// In-game overlay backend: hooks the game's IDXGISwapChain::Present and draws
// the ImGui social panel over the game, on the game's own device and render
// thread. See OverlayInternal.h for how this shares state with the windowed
// fallback in Overlay.cpp, and HANDOFF.md "in-game swapchain hook".
//
// Mechanism (the classic vtable-patch injection, as the real overlay does):
//   1. Stand up a throwaway swapchain to read the process-wide CDXGISwapChain
//      vtable, which every swapchain in the process shares.
//   2. Patch the Present (slot 8) and ResizeBuffers (slot 13) entries so the
//      game's calls land in our hooks. One patch covers every swapchain.
//   3. On the first real Present, adopt that swapchain's device/window, stand up
//      the ImGui DX11 backend on it, and hook its WndProc for input.
//   4. Each visible frame: build our draw data and render it onto the game's
//      backbuffer, then call the original Present (we never present ourselves).
//
// Rendering runs on the game's render thread inside the hook. Consumer callbacks
// do NOT -- they stay on EOS_Platform_Tick (the Tick-thread rule). This backend
// only reads OverlayImpl's thread-safe state (visibility, toasts, social items)
// and appends social actions through the same locked queue the windowed backend
// uses.
//

#include "core/Logging.h"

#ifdef EOSEMU_ENABLE_OVERLAY

#include "overlay/OverlayInternal.h"

#include <chrono>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace EOSEmu
{
	// COM method signatures we intercept. STDMETHODCALLTYPE pins the calling
	// convention so the hooks are ABI-identical to the originals.
	using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
	using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

	// Full hook state, forward-declared in OverlayInternal.h so the windowed path
	// never sees DXGI hooking details.
	struct HookState
	{
		OverlayImpl* Impl = nullptr;
		void** Vtable = nullptr;         // the patched CDXGISwapChain vtable
		PresentFn OrigPresent = nullptr;
		ResizeBuffersFn OrigResize = nullptr;

		std::atomic<bool> Shutting{false};
		bool Initialized = false;        // set once we've adopted a swapchain
		IDXGISwapChain* BoundSwapchain = nullptr;
		ID3D11Device* Device = nullptr;
		ID3D11DeviceContext* Context = nullptr;
		ID3D11RenderTargetView* Rtv = nullptr;
		HWND Hwnd = nullptr;
		WNDPROC PrevWndProc = nullptr;
		ImGuiContext* ImCtx = nullptr;

		// Last cursor position pushed to ImGui by the per-frame poll, so we only
		// emit AddMousePosEvent on actual movement.
		POINT LastMouse = {};
		bool HasLastMouse = false;
	};

	namespace
	{
		// IDXGISwapChain vtable indices (IUnknown 0-2, IDXGIObject 3-6,
		// IDXGIDeviceSubObject 7=GetDevice, IDXGISwapChain 8=Present ... 13=ResizeBuffers).
		constexpr int kPresentIndex = 8;
		constexpr int kResizeBuffersIndex = 13;

		// Only one overlay hook can own the shared vtable at a time. These globals
		// let the free hook functions reach the active state; the originals stay
		// here too so an in-flight Present can still forward while StopHook runs.
		std::atomic<HookState*> g_Hook{nullptr};
		PresentFn g_OrigPresent = nullptr;
		ResizeBuffersFn g_OrigResize = nullptr;

		bool PatchSlot(void** Vtable, int Index, void* Hook, void** OutOrig)
		{
			DWORD Old = 0;
			if (!VirtualProtect(&Vtable[Index], sizeof(void*), PAGE_EXECUTE_READWRITE, &Old))
				return false;
			if (OutOrig) *OutOrig = Vtable[Index];
			Vtable[Index] = Hook;
			VirtualProtect(&Vtable[Index], sizeof(void*), Old, &Old);
			return true;
		}

		void RestoreSlot(void** Vtable, int Index, void* Orig)
		{
			DWORD Old = 0;
			if (VirtualProtect(&Vtable[Index], sizeof(void*), PAGE_EXECUTE_READWRITE, &Old))
			{
				Vtable[Index] = Orig;
				VirtualProtect(&Vtable[Index], sizeof(void*), Old, &Old);
			}
		}

		// Stand up a hidden 1x1 swapchain purely to read its vtable, then release
		// everything. The vtable memory belongs to the DXGI module and stays valid
		// after release, so the pointer we return is patchable later.
		bool DiscoverVtable(void*** OutVtable)
		{
			WNDCLASSEXW Wc = {};
			Wc.cbSize = sizeof(Wc);
			Wc.lpfnWndProc = DefWindowProcW;
			Wc.hInstance = GetModuleHandleW(nullptr);
			Wc.lpszClassName = L"EOSEmuHookProbe";
			RegisterClassExW(&Wc);
			HWND Wnd = CreateWindowExW(0, Wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
				0, 0, 1, 1, nullptr, nullptr, Wc.hInstance, nullptr);

			DXGI_SWAP_CHAIN_DESC Sd = {};
			Sd.BufferCount = 1;
			Sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			Sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			Sd.OutputWindow = Wnd;
			Sd.SampleDesc.Count = 1;
			Sd.Windowed = TRUE;
			Sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

			const D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
			IDXGISwapChain* Sc = nullptr;
			ID3D11Device* Dev = nullptr;
			ID3D11DeviceContext* Ctx = nullptr;
			D3D_FEATURE_LEVEL Got;
			HRESULT Hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
				Levels, 2, D3D11_SDK_VERSION, &Sd, &Sc, &Dev, &Got, &Ctx);
			if (Hr == DXGI_ERROR_UNSUPPORTED)
				Hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
					Levels, 2, D3D11_SDK_VERSION, &Sd, &Sc, &Dev, &Got, &Ctx);

			bool Ok = false;
			if (SUCCEEDED(Hr) && Sc != nullptr)
			{
				*OutVtable = *reinterpret_cast<void***>(Sc);
				Ok = true;
			}
			if (Sc) Sc->Release();
			if (Ctx) Ctx->Release();
			if (Dev) Dev->Release();
			if (Wnd) DestroyWindow(Wnd);
			UnregisterClassW(Wc.lpszClassName, Wc.hInstance);
			return Ok;
		}

		void RebuildRtv(HookState* H, IDXGISwapChain* Sc)
		{
			ID3D11Texture2D* Back = nullptr;
			if (SUCCEEDED(Sc->GetBuffer(0, IID_PPV_ARGS(&Back))) && Back != nullptr)
			{
				H->Device->CreateRenderTargetView(Back, nullptr, &H->Rtv);
				Back->Release();
			}
		}

		LRESULT CALLBACK HookWndProc(HWND Wnd, UINT Msg, WPARAM W, LPARAM L)
		{
			HookState* H = g_Hook.load();
			if (H != nullptr && H->Initialized && H->ImCtx != nullptr && H->Impl->WantVisible.load())
			{
				ImGui::SetCurrentContext(H->ImCtx);
				ImGui_ImplWin32_WndProcHandler(Wnd, Msg, W, L);
				// While the overlay is up it is MODAL: the game must not see any
				// mouse or keyboard input, whether or not the cursor is currently
				// over a panel. This matches a real game overlay -- input is fully
				// grabbed the instant it opens -- and is what makes the game stop
				// steering underneath it. (The toggle hotkey still works: it is
				// polled via GetAsyncKeyState in PollHotkey, not through here.)
				switch (Msg)
				{
				case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
				case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
				case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
				case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
				case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_MOUSEMOVE:
					return 1; // swallow: game must not see it
				case WM_INPUT:
					// Raw-input games read the mouse through WM_INPUT, bypassing the
					// WM_MOUSE* swallows above -- without this the game keeps steering
					// its camera underneath the overlay. Divert to DefWindowProc
					// (required for raw-input cleanup) instead of the game.
					return DefWindowProcW(Wnd, Msg, W, L);
				case WM_SETCURSOR:
					// The overlay draws a software cursor (MouseDrawCursor); the ImGui
					// handler has already hidden the hardware one. Don't let the game
					// re-set it underneath ours.
					return 1;
				case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
				case WM_CHAR: case WM_SYSCHAR:
					return 1;
				}
			}
			WNDPROC Prev = (H != nullptr) ? H->PrevWndProc : nullptr;
			if (Prev != nullptr) return CallWindowProcW(Prev, Wnd, Msg, W, L);
			return DefWindowProcW(Wnd, Msg, W, L);
		}

		// First-Present adoption of the game's swapchain.
		bool InitFromSwapchain(HookState* H, IDXGISwapChain* Sc)
		{
			ID3D11Device* Dev = nullptr;
			if (FAILED(Sc->GetDevice(IID_PPV_ARGS(&Dev))) || Dev == nullptr)
				return false; // not a D3D11 swapchain (D3D12/other) -- leave it alone
			ID3D11DeviceContext* Ctx = nullptr;
			Dev->GetImmediateContext(&Ctx);

			DXGI_SWAP_CHAIN_DESC Desc = {};
			Sc->GetDesc(&Desc);
			HWND Wnd = Desc.OutputWindow;
			if (Wnd == nullptr || Ctx == nullptr)
			{
				if (Ctx) Ctx->Release();
				Dev->Release();
				return false;
			}

			H->Device = Dev;
			H->Context = Ctx;
			H->Hwnd = Wnd;
			H->BoundSwapchain = Sc;
			RebuildRtv(H, Sc);
			if (H->Rtv == nullptr)
			{
				H->Device = nullptr; H->Context = nullptr; H->Hwnd = nullptr; H->BoundSwapchain = nullptr;
				Ctx->Release();
				Dev->Release();
				return false;
			}

			IMGUI_CHECKVERSION();
			H->ImCtx = ImGui::CreateContext();
			ImGui::SetCurrentContext(H->ImCtx);
			ImGuiIO& Io = ImGui::GetIO();
			Io.IniFilename = nullptr; // don't litter the game's directory
			ImGui::StyleColorsDark();
			ImGui_ImplWin32_Init(Wnd);
			ImGui_ImplDX11_Init(Dev, Ctx);

			H->PrevWndProc = reinterpret_cast<WNDPROC>(
				SetWindowLongPtrW(Wnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookWndProc)));
			H->Initialized = true;
			EOSEMU_INFO(Overlay, "overlay: captured game swapchain (hwnd %p), rendering in-game", (void*)Wnd);
			return true;
		}

		void RenderOverlay(HookState* H, IDXGISwapChain* Sc)
		{
			ImGui::SetCurrentContext(H->ImCtx);
			OverlayImpl* Impl = H->Impl;
			Impl->PollHotkey(); // rising-edge toggle, on the render thread

			const bool Visible = Impl->WantVisible.load();
			ImGuiIO& Io = ImGui::GetIO();
			// Games hide the hardware cursor and draw their own; render an ImGui
			// software cursor while the overlay is up so the user has something to
			// point with regardless of what the game did to the OS cursor.
			Io.MouseDrawCursor = Visible;
			// The overlay is modal: while visible the WndProc hook grabs ALL
			// mouse/keyboard input, so we hold exclusive input the whole time it is
			// up (not just when the cursor is over a panel). This is what drives
			// EOS_UI_GetFriendsExclusiveInput and the bIsExclusiveInput flag in the
			// DisplaySettingsUpdated notification that games pause/mute on.
			Impl->Exclusive.store(Visible);
			if (!Visible)
			{
				H->HasLastMouse = false;
				return;
			}

			// Raw-input games often never generate WM_MOUSEMOVE, so the WndProc hook
			// alone can leave ImGui's mouse position stale. Poll the OS cursor each
			// frame; the game can't hide *position*, only the visible cursor.
			POINT Cur = {};
			if (GetCursorPos(&Cur) && ScreenToClient(H->Hwnd, &Cur))
			{
				if (!H->HasLastMouse || Cur.x != H->LastMouse.x || Cur.y != H->LastMouse.y)
				{
					Io.AddMousePosEvent(static_cast<float>(Cur.x), static_cast<float>(Cur.y));
					H->LastMouse = Cur;
					H->HasLastMouse = true;
				}
			}
			// Release any cursor confinement the game holds, every frame -- games
			// re-assert their clip. On hide we stop interfering and the game's next
			// re-assert restores its own clip.
			ClipCursor(nullptr);

			if (H->Rtv == nullptr) RebuildRtv(H, Sc); // lost after a resize
			if (H->Rtv == nullptr) return;

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			Impl->DrawFriendsPanel(true);
			Impl->DrawToasts();
			ImGui::Render();

			// Draw onto the game's backbuffer without clearing -- we compose over
			// the already-rendered frame, then let the game present it.
			H->Context->OMSetRenderTargets(1, &H->Rtv, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}

		HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* Sc, UINT SyncInterval, UINT Flags)
		{
			HookState* H = g_Hook.load();
			PresentFn Orig = (H != nullptr) ? H->OrigPresent : g_OrigPresent;
			// DXGI_PRESENT_TEST presents nothing; never render into it.
			if (H != nullptr && !H->Shutting.load() && (Flags & DXGI_PRESENT_TEST) == 0)
			{
				if (!H->Initialized) InitFromSwapchain(H, Sc);
				if (H->Initialized && Sc == H->BoundSwapchain) RenderOverlay(H, Sc);
			}
			if (Orig == nullptr) return S_OK; // fully torn down
			return Orig(Sc, SyncInterval, Flags);
		}

		HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* Sc, UINT BufferCount,
			UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
		{
			HookState* H = g_Hook.load();
			if (H != nullptr && H->Initialized && Sc == H->BoundSwapchain && H->Rtv != nullptr)
			{
				H->Rtv->Release(); // release our view of the old backbuffer first
				H->Rtv = nullptr;  // RenderOverlay rebuilds it lazily next frame
			}
			ResizeBuffersFn Orig = (H != nullptr) ? H->OrigResize : g_OrigResize;
			if (Orig == nullptr) return S_OK;
			return Orig(Sc, BufferCount, Width, Height, NewFormat, SwapChainFlags);
		}
	}

	bool OverlayImpl::TryStartHook()
	{
		if (g_Hook.load() != nullptr)
		{
			EOSEMU_WARN(Overlay, "overlay: swapchain hook already owned by another platform");
			return false;
		}
		void** Vtable = nullptr;
		if (!DiscoverVtable(&Vtable) || Vtable == nullptr)
		{
			EOSEMU_WARN(Overlay, "overlay: could not discover swapchain vtable; using windowed fallback");
			return false;
		}

		HookState* H = new HookState();
		H->Impl = this;
		H->Vtable = Vtable;
		g_OrigPresent = reinterpret_cast<PresentFn>(Vtable[kPresentIndex]);
		g_OrigResize = reinterpret_cast<ResizeBuffersFn>(Vtable[kResizeBuffersIndex]);
		H->OrigPresent = g_OrigPresent;
		H->OrigResize = g_OrigResize;

		if (!PatchSlot(Vtable, kPresentIndex, reinterpret_cast<void*>(&HookedPresent), nullptr))
		{
			EOSEMU_WARN(Overlay, "overlay: failed to patch Present slot; using windowed fallback");
			g_OrigPresent = nullptr;
			g_OrigResize = nullptr;
			delete H;
			return false;
		}
		PatchSlot(Vtable, kResizeBuffersIndex, reinterpret_cast<void*>(&HookedResizeBuffers), nullptr);

		Hook = H;
		g_Hook.store(H);
		HookActive.store(true);
		return true;
	}

	void OverlayImpl::StopHook()
	{
		HookState* H = Hook;
		if (H == nullptr) { HookActive.store(false); return; }

		// Stop rendering, then restore the vtable so new Present calls bypass us.
		H->Shutting.store(true);
		g_Hook.store(nullptr);
		if (H->Vtable != nullptr)
		{
			RestoreSlot(H->Vtable, kPresentIndex, reinterpret_cast<void*>(g_OrigPresent));
			RestoreSlot(H->Vtable, kResizeBuffersIndex, reinterpret_cast<void*>(g_OrigResize));
		}
		// Let any Present already inside our hook on the render thread unwind
		// before we free the ImGui/D3D resources it may still be touching.
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		if (H->Initialized)
		{
			if (H->Hwnd != nullptr && H->PrevWndProc != nullptr)
				SetWindowLongPtrW(H->Hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(H->PrevWndProc));
			ImGui::SetCurrentContext(H->ImCtx);
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext(H->ImCtx);
			if (H->Rtv) H->Rtv->Release();
			if (H->Context) H->Context->Release();
			if (H->Device) H->Device->Release();
		}

		HookActive.store(false);
		g_OrigPresent = nullptr;
		g_OrigResize = nullptr;
		delete H;
		Hook = nullptr;
		EOSEMU_INFO(Overlay, "overlay: swapchain hook removed");
	}
}

#endif // EOSEMU_ENABLE_OVERLAY
