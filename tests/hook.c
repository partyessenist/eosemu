// In-game swapchain-hook test for EOSEmu's overlay.
//
// This test stands in for a D3D11 game: it creates its own window, D3D11 device
// and IDXGISwapChain, then drives the EOS lifecycle while presenting frames. The
// EOSEmu DLL, loaded beside the exe, installs its Present hook during
// EOS_Platform_Create. We assert -- by scanning EOSEmu's own log stream -- that:
//
//   1. the overlay chose the in-game hook path (not the windowed fallback),
//   2. the hook actually captured our swapchain on the first Present,
//   3. EOS_UI_ShowFriends reports the overlay is available (not NotConfigured),
//   4. presenting WHILE the overlay is visible renders ImGui onto our backbuffer
//      without crashing (exercises the real in-hook draw path), and
//   5. presenting AFTER EOS_Platform_Release still works -- the vtable was
//      cleanly unhooked, so our code is no longer on the present path.
//
// It does not assert pixels; the panel drawing is not headlessly checkable. This
// is the hook install / capture / render / unhook smoke test HANDOFF.md asks for.

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

#include <stdio.h>
#include <string.h>

#include "eos_sdk.h"
#include "eos_ui.h"
#include "eos_logging.h"

static int g_HookInstalled = 0; // saw "swapchain hook installed"
static int g_Captured = 0;      // saw "captured game swapchain"
static int g_ShowOk = 0;        // ShowFriends completion was not NotConfigured

static void EOS_CALL LogCb(const EOS_LogMessage* Msg)
{
	const char* M = Msg->Message ? Msg->Message : "";
	if (strstr(M, "swapchain hook installed")) g_HookInstalled = 1;
	if (strstr(M, "captured game swapchain")) g_Captured = 1;
	printf("  [log %s] %s\n", Msg->Category ? Msg->Category : "?", M);
}

static void EOS_CALL OnShow(const EOS_UI_ShowFriendsCallbackInfo* Data)
{
	g_ShowOk = (Data->ResultCode != EOS_NotConfigured);
}

static IDXGISwapChain* g_Sc = NULL;
static ID3D11Device* g_Dev = NULL;
static ID3D11DeviceContext* g_Ctx = NULL;
static HWND g_Wnd = NULL;

static int CreateGameSwapchain(void)
{
	WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"EOSEmuHookTestGame";
	RegisterClassExW(&wc);
	g_Wnd = CreateWindowExW(0, wc.lpszClassName, L"EOSEmu Hook Test", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 320, 240, NULL, NULL, wc.hInstance, NULL);
	if (!g_Wnd) return 0;

	DXGI_SWAP_CHAIN_DESC sd; memset(&sd, 0, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = g_Wnd;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
	D3D_FEATURE_LEVEL got;
	HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
		levels, 2, D3D11_SDK_VERSION, &sd, &g_Sc, &g_Dev, &got, &g_Ctx);
	if (hr == DXGI_ERROR_UNSUPPORTED)
		hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0,
			levels, 2, D3D11_SDK_VERSION, &sd, &g_Sc, &g_Dev, &got, &g_Ctx);
	return SUCCEEDED(hr) && g_Sc != NULL;
}

static void PumpAndPresent(EOS_HPlatform Platform, int Frames, int Tick)
{
	for (int i = 0; i < Frames; ++i)
	{
		MSG msg;
		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
		IDXGISwapChain_Present(g_Sc, 1, 0);
		if (Tick && Platform) EOS_Platform_Tick(Platform);
		Sleep(8);
	}
}

int main(void)
{
	if (!CreateGameSwapchain()) { printf("could not create a D3D11 swapchain; skipping\nHOOK SKIP\n"); return 0; }

	EOS_InitializeOptions InitOpts; memset(&InitOpts, 0, sizeof(InitOpts));
	InitOpts.ApiVersion = EOS_INITIALIZE_API_LATEST;
	InitOpts.ProductName = "EOSEmuHook";
	InitOpts.ProductVersion = "1.0";
	if (EOS_Initialize(&InitOpts) != EOS_Success) { printf("Initialize failed\n"); return 1; }
	EOS_Logging_SetCallback(&LogCb);
	EOS_Logging_SetLogLevel(EOS_LC_ALL_CATEGORIES, EOS_LOG_Verbose);

	EOS_Platform_Options PlatOpts; memset(&PlatOpts, 0, sizeof(PlatOpts));
	PlatOpts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	PlatOpts.ProductId = "prod";
	PlatOpts.SandboxId = "sand";
	PlatOpts.DeploymentId = "deploy";
	PlatOpts.ClientCredentials.ClientId = "cid";
	PlatOpts.ClientCredentials.ClientSecret = "secret";
	// Like the samples: request the overlay. The hook detects the real swapchain.
	PlatOpts.Flags = EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D9 | EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D10 | EOS_PF_WINDOWS_ENABLE_OVERLAY_OPENGL;
	EOS_HPlatform Platform = EOS_Platform_Create(&PlatOpts);
	if (!Platform) { printf("Platform_Create failed\n"); EOS_Shutdown(); return 2; }

	EOS_HUI UI = EOS_Platform_GetUIInterface(Platform);

	// Present frames so the hook adopts our swapchain on its first Present.
	PumpAndPresent(Platform, 20, 1);

	// Show the overlay, then present WHILE visible so the in-hook ImGui draw path
	// actually runs on our device/backbuffer.
	EOS_UI_ShowFriendsOptions SfOpts; memset(&SfOpts, 0, sizeof(SfOpts));
	SfOpts.ApiVersion = EOS_UI_SHOWFRIENDS_API_LATEST;
	EOS_UI_ShowFriends(UI, &SfOpts, NULL, &OnShow);
	PumpAndPresent(Platform, 30, 1);

	int visible = (EOS_UI_GetFriendsVisible(UI, NULL) == EOS_TRUE);

	EOS_Platform_Release(Platform); // unhooks the vtable

	// Present after release: our code must be off the present path. A crash here
	// means the unhook was dirty.
	PumpAndPresent(NULL, 15, 0);

	EOS_Shutdown();

	if (g_Sc) IDXGISwapChain_Release(g_Sc);
	if (g_Ctx) ID3D11DeviceContext_Release(g_Ctx);
	if (g_Dev) ID3D11Device_Release(g_Dev);
	if (g_Wnd) DestroyWindow(g_Wnd);

	printf("hookInstalled=%d captured=%d showOk=%d visible=%d\n", g_HookInstalled, g_Captured, g_ShowOk, visible);
	int ok = g_HookInstalled && g_Captured && g_ShowOk && visible;
	printf("HOOK %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 3;
}
