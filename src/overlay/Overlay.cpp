//
// Social overlay implementation. See Overlay.h for the design rationale and
// OverlayInternal.h for the shared OverlayImpl definition.
//
// This TU owns the separate-window fallback backend and the public facade. The
// in-game swapchain-hook backend lives in SwapchainHook.cpp; both share the
// panel-drawing members and the visibility/toast/social state on OverlayImpl.
//
// EOSEMU_ENABLE_OVERLAY selects between the real Win32 + D3D11 + Dear ImGui
// implementation and a headless no-op. All ImGui/Win32 usage is confined to the
// overlay TUs.
//

#include "overlay/Overlay.h"

#include "core/Logging.h"
#include "core/Platform.h"

#ifdef EOSEMU_ENABLE_OVERLAY

#include "overlay/OverlayInternal.h"

#include <algorithm>
#include <cstdlib>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "core/Identity.h"
#include "core/Peers.h"

// Declared by imgui_impl_win32.h; forwards raw window messages into ImGui.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace EOSEmu
{
	namespace
	{
		// Reads a boolean-ish env var ("1"/nonzero-first-char => true). Mirrors the
		// getenv handling used in the interfaces so the overlay's env overrides
		// match the existing convention.
		bool OverlayEnvFlag(const char* Name)
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

		LRESULT WINAPI OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
			auto* Self = reinterpret_cast<OverlayImpl*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
			switch (msg)
			{
			case WM_SIZE:
				if (Self != nullptr && wParam != SIZE_MINIMIZED)
				{
					Self->ResizeW = LOWORD(lParam);
					Self->ResizeH = HIWORD(lParam);
				}
				return 0;
			case WM_CLOSE:
				// Closing the window hides the overlay rather than tearing it down;
				// the interface learns of the change on the next Tick.
				if (Self != nullptr) Self->RequestHide();
				return 0;
			case WM_SYSCOMMAND:
				if ((wParam & 0xFFF0) == SC_KEYMENU) return 0; // swallow the Alt menu
				break;
			}
			return DefWindowProcW(hWnd, msg, wParam, lParam);
		}
	}

	// --- separate-window device management ------------------------------------

	bool OverlayImpl::CreateDevice()
	{
		DXGI_SWAP_CHAIN_DESC Sd = {};
		Sd.BufferCount = 2;
		Sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Sd.BufferDesc.RefreshRate.Numerator = 60;
		Sd.BufferDesc.RefreshRate.Denominator = 1;
		Sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		Sd.OutputWindow = Hwnd;
		Sd.SampleDesc.Count = 1;
		Sd.Windowed = TRUE;
		Sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		const D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
		D3D_FEATURE_LEVEL Got;
		HRESULT Hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			Levels, 2, D3D11_SDK_VERSION, &Sd, &Swapchain, &Device, &Got, &Context);
		if (Hr == DXGI_ERROR_UNSUPPORTED)
		{
			Hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
				Levels, 2, D3D11_SDK_VERSION, &Sd, &Swapchain, &Device, &Got, &Context);
		}
		if (FAILED(Hr)) return false;
		CreateRtv();
		return true;
	}

	void OverlayImpl::CreateRtv()
	{
		ID3D11Texture2D* Back = nullptr;
		if (SUCCEEDED(Swapchain->GetBuffer(0, IID_PPV_ARGS(&Back))) && Back != nullptr)
		{
			Device->CreateRenderTargetView(Back, nullptr, &Rtv);
			Back->Release();
		}
	}

	void OverlayImpl::CleanupRtv()
	{
		if (Rtv) { Rtv->Release(); Rtv = nullptr; }
	}

	void OverlayImpl::CleanupDevice()
	{
		CleanupRtv();
		if (Swapchain) { Swapchain->Release(); Swapchain = nullptr; }
		if (Context) { Context->Release(); Context = nullptr; }
		if (Device) { Device->Release(); Device = nullptr; }
	}

	// --- shared helpers -------------------------------------------------------

	void OverlayImpl::PollHotkey()
	{
		const int VkKey = Vk.load();
		if (VkKey == 0) { HotkeyWasDown = false; return; }
		auto Down = [](int V) { return (GetAsyncKeyState(V) & 0x8000) != 0; };
		bool Ok = Down(VkKey);
		if (ModShift.load()) Ok = Ok && Down(VK_SHIFT);
		if (ModCtrl.load()) Ok = Ok && Down(VK_CONTROL);
		if (ModAlt.load()) Ok = Ok && Down(VK_MENU);
		if (Ok && !HotkeyWasDown) RequestToggle(); // rising edge only
		HotkeyWasDown = Ok;
	}

	// --- shared panel drawing -------------------------------------------------

	void OverlayImpl::DrawFriendsPanel(bool InGame)
	{
		const ImGuiIO& Io = ImGui::GetIO();
		if (InGame)
		{
			// Draw a translucent, movable panel over the game rather than covering
			// the whole frame.
			ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowBgAlpha(0.92f);
			ImGui::Begin("EOSEmu Social Overlay", nullptr, ImGuiWindowFlags_NoCollapse);
		}
		else
		{
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(Io.DisplaySize);
			ImGui::Begin("EOSEmu Social Overlay", nullptr,
				ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus);
		}

		ImGui::TextUnformatted("EOSEmu - Social Overlay");
		ImGui::Separator();
		ImGui::Text("You: %s", Plat.LocalIdentity().DisplayName().c_str());
		ImGui::Spacing();
		ImGui::TextUnformatted("Players on the LAN:");

		const std::vector<PeerInfo> Peers = Plat.Peers().Snapshot();
		if (Peers.empty())
		{
			ImGui::TextDisabled("   (no one discovered yet)");
		}
		for (const PeerInfo& P : Peers)
		{
			ImGui::BulletText("%s", P.DisplayName.empty() ? "(unknown)" : P.DisplayName.c_str());
			if (!P.Presence.RichText.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("- %s", P.Presence.RichText.c_str());
			}
		}

		DrawSocialPanel();

		ImGui::End();
	}

	void OverlayImpl::DrawSocialPanel()
	{
		std::vector<OverlaySocialItem> Items;
		{
			std::lock_guard<std::mutex> Lock(SocialMutex);
			Items = SocialItems;
		}
		if (Items.empty()) return;

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextUnformatted("Invitations & joinable games:");

		// Distinct ImGui ids per row (the labels repeat) via the ##index suffix.
		int Index = 0;
		for (const OverlaySocialItem& It : Items)
		{
			ImGui::PushID(Index++);
			ImGui::BulletText("%s", It.Label.c_str());
			const bool IsInvite = It.Kind == OverlaySocialKind::LobbyInvite || It.Kind == OverlaySocialKind::SessionInvite;
			ImGui::SameLine();
			if (IsInvite)
			{
				if (ImGui::SmallButton("Accept")) QueueAction(It.Kind, OverlayActionKind::Accept, It.Id);
				ImGui::SameLine();
				if (ImGui::SmallButton("Reject")) QueueAction(It.Kind, OverlayActionKind::Reject, It.Id);
			}
			else
			{
				if (ImGui::SmallButton("Join")) QueueAction(It.Kind, OverlayActionKind::Join, It.Id);
			}
			ImGui::PopID();
		}
	}

	void OverlayImpl::DrawToasts()
	{
		std::vector<std::string> Active;
		{
			const auto Now = std::chrono::steady_clock::now();
			std::lock_guard<std::mutex> Lock(ToastMutex);
			Toasts.erase(std::remove_if(Toasts.begin(), Toasts.end(),
				[&](const Toast& T) { return T.Expiry <= Now; }), Toasts.end());
			for (const Toast& T : Toasts) Active.push_back(T.Text);
		}
		if (Active.empty()) return;

		const ImGuiIO& Io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(Io.DisplaySize.x - 12.0f, Io.DisplaySize.y - 12.0f),
			ImGuiCond_Always, ImVec2(1.0f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.85f);
		ImGui::Begin("##toasts", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
		for (const std::string& T : Active) ImGui::TextUnformatted(T.c_str());
		ImGui::End();
	}

	// --- separate-window render loop ------------------------------------------

	void OverlayImpl::RenderFrame()
	{
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		DrawFriendsPanel(false);
		DrawToasts();

		ImGui::Render();
		const float Clear[4] = { 0.06f, 0.07f, 0.09f, 1.0f };
		Context->OMSetRenderTargets(1, &Rtv, nullptr);
		Context->ClearRenderTargetView(Rtv, Clear);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		Swapchain->Present(1, 0); // vsync -> ~60fps while visible
	}

	void OverlayImpl::ThreadMain()
	{
		const wchar_t* ClassName = L"EOSEmuOverlayWindow";
		WNDCLASSEXW Wc = {};
		Wc.cbSize = sizeof(Wc);
		Wc.style = CS_HREDRAW | CS_VREDRAW;
		Wc.lpfnWndProc = OverlayWndProc;
		Wc.hInstance = GetModuleHandleW(nullptr);
		Wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
		Wc.lpszClassName = ClassName;
		RegisterClassExW(&Wc);

		Hwnd = CreateWindowExW(0, ClassName, L"EOSEmu Social Overlay", WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, 460, 580, nullptr, nullptr, Wc.hInstance, nullptr);
		if (Hwnd == nullptr)
		{
			EOSEMU_ERROR(Overlay, "overlay: CreateWindow failed");
			UnregisterClassW(ClassName, Wc.hInstance);
			Running.store(false);
			return;
		}
		SetWindowLongPtrW(Hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

		if (!CreateDevice())
		{
			EOSEMU_ERROR(Overlay, "overlay: D3D11 device creation failed; overlay disabled");
			CleanupDevice();
			DestroyWindow(Hwnd);
			Hwnd = nullptr;
			UnregisterClassW(ClassName, Wc.hInstance);
			Running.store(false);
			return;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& Io = ImGui::GetIO();
		Io.IniFilename = nullptr; // don't litter the game's directory with imgui.ini
		Io.MouseDrawCursor = true; // same software cursor as the in-game backend
		ImGui::StyleColorsDark();
		ImGui_ImplWin32_Init(Hwnd);
		ImGui_ImplDX11_Init(Device, Context);

		ShowWindow(Hwnd, SW_HIDE);
		bool Shown = false;

		while (Running.load())
		{
			MSG Msg;
			while (PeekMessageW(&Msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&Msg);
				DispatchMessageW(&Msg);
			}

			PollHotkey();

			const bool Visible = WantVisible.load();
			if (Visible != Shown)
			{
				ShowWindow(Hwnd, Visible ? SW_SHOW : SW_HIDE);
				if (Visible) SetForegroundWindow(Hwnd);
				Shown = Visible;
			}
			Exclusive.store(Visible && GetForegroundWindow() == Hwnd);

			if (!Visible)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(30));
				continue;
			}

			if (ResizeW != 0 && ResizeH != 0)
			{
				CleanupRtv();
				Swapchain->ResizeBuffers(0, ResizeW, ResizeH, DXGI_FORMAT_UNKNOWN, 0);
				ResizeW = ResizeH = 0;
				CreateRtv();
			}

			RenderFrame();
		}

		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		CleanupDevice();
		if (Hwnd) { DestroyWindow(Hwnd); Hwnd = nullptr; }
		UnregisterClassW(ClassName, Wc.hInstance);
	}

	// --- facade ---------------------------------------------------------------

	Overlay::Overlay(Platform& Owner) : Platform_(Owner)
	{
		Impl_ = new OverlayImpl(Owner);
	}

	Overlay::~Overlay()
	{
		Stop();
		delete Impl_;
		Impl_ = nullptr;
	}

	void Overlay::Start(bool EnabledByFlags)
	{
		if (Impl_ == nullptr || !EnabledByFlags) return;
		if (Impl_->HookActive.load() || Impl_->Running.load()) return; // already up
		// A prior windowed run whose init failed cleared Running itself but left
		// the thread joinable; reap it before assigning a new one.
		if (Impl_->Thread.joinable()) Impl_->Thread.join();

		// Prefer the in-game swapchain hook so the overlay draws over the game.
		// EOSEMU_OVERLAY_WINDOW forces the separate-window fallback, which is handy
		// for headless LAN testing where no game swapchain exists to draw into.
		if (!OverlayEnvFlag("EOSEMU_OVERLAY_WINDOW") && Impl_->TryStartHook())
		{
			EOSEMU_INFO(Overlay, "overlay: in-game swapchain hook installed");
			return;
		}

		Impl_->Running.store(true);
		Impl_->Thread = std::thread([this] { Impl_->ThreadMain(); });
		EOSEMU_INFO(Overlay, "overlay: separate-window social overlay started");
	}

	void Overlay::Stop()
	{
		if (Impl_ == nullptr) return;
		// Unhook first so the game's Present stops entering our code before we free
		// the ImGui backend it renders through.
		if (Impl_->HookActive.load()) Impl_->StopHook();
		Impl_->Running.store(false);
		// Join unconditionally, not only when Running was still set: if ThreadMain
		// failed its window/device init it cleared Running itself, and deleting a
		// still-joinable std::thread is std::terminate -- inside EOS_Platform_Release.
		if (Impl_->Thread.joinable()) Impl_->Thread.join();
	}

	bool Overlay::Available() const
	{
		return Impl_ != nullptr && (Impl_->HookActive.load() || Impl_->Running.load());
	}

	void Overlay::SetVisible(bool Visible)
	{
		if (Impl_) Impl_->WantVisible.store(Visible);
	}

	bool Overlay::IsExclusiveInput() const
	{
		return Impl_ != nullptr && Impl_->Exclusive.load();
	}

	void Overlay::SetToggleShortcut(int VkKey, bool Shift, bool Ctrl, bool Alt)
	{
		if (Impl_ == nullptr) return;
		Impl_->Vk.store(VkKey);
		Impl_->ModShift.store(Shift);
		Impl_->ModCtrl.store(Ctrl);
		Impl_->ModAlt.store(Alt);
	}

	bool Overlay::ConsumeUserVisibilityRequest(bool& OutVisible)
	{
		if (Impl_ == nullptr) return false;
		std::lock_guard<std::mutex> Lock(Impl_->ReqMutex);
		if (!Impl_->HasRequest) return false;
		OutVisible = Impl_->RequestVisible;
		Impl_->HasRequest = false;
		return true;
	}

	void Overlay::PushToast(const std::string& Text)
	{
		if (Impl_ == nullptr) return;
		std::lock_guard<std::mutex> Lock(Impl_->ToastMutex);
		Impl_->Toasts.push_back({ Text, std::chrono::steady_clock::now() + std::chrono::seconds(4) });
	}

	void Overlay::SetSocialItems(std::vector<OverlaySocialItem> Items)
	{
		if (Impl_ == nullptr) return;
		std::lock_guard<std::mutex> Lock(Impl_->SocialMutex);
		Impl_->SocialItems = std::move(Items);
	}

	bool Overlay::ConsumeSocialActions(std::vector<OverlaySocialAction>& Out)
	{
		if (Impl_ == nullptr) return false;
		std::lock_guard<std::mutex> Lock(Impl_->SocialMutex);
		if (Impl_->SocialActions.empty()) return false;
		Out.insert(Out.end(), Impl_->SocialActions.begin(), Impl_->SocialActions.end());
		Impl_->SocialActions.clear();
		return true;
	}
}

#else // !EOSEMU_ENABLE_OVERLAY -- headless build: every method is inert.

namespace EOSEmu
{
	struct OverlayImpl {};

	Overlay::Overlay(Platform& Owner) : Platform_(Owner) {}
	Overlay::~Overlay() {}
	void Overlay::Start(bool) {}
	void Overlay::Stop() {}
	bool Overlay::Available() const { return false; }
	void Overlay::SetVisible(bool) {}
	bool Overlay::IsExclusiveInput() const { return false; }
	void Overlay::SetToggleShortcut(int, bool, bool, bool) {}
	bool Overlay::ConsumeUserVisibilityRequest(bool&) { return false; }
	void Overlay::PushToast(const std::string&) {}
	void Overlay::SetSocialItems(std::vector<OverlaySocialItem>) {}
	bool Overlay::ConsumeSocialActions(std::vector<OverlaySocialAction>&) { return false; }
}

#endif
