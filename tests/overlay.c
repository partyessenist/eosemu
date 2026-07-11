// Overlay / EOS_UI lifecycle test for EOSEmu.
//
// Exercises the EOS_UI surface against the separate-window overlay without a
// human at the keyboard: show/hide friends and confirm visibility +
// DisplaySettingsUpdated notifications track it; validate key combinations,
// toggle key round-trip, display preference, pause, and the block/report/profile
// completion callbacks; confirm ReportInputState/PrePresent report
// NotImplemented. Then create a second platform with the overlay DISABLED by
// flag and confirm the lifecycle still runs cleanly (a game that opts out of the
// overlay must be unaffected).
//
// This is a lifecycle/no-crash test: it does not assert anything is drawn on
// screen (the window is briefly shown then hidden).

#include <stdio.h>
#include <string.h>

#include "eos_sdk.h"
#include "eos_ui.h"
#include "eos_logging.h"

static int g_DisplayCount = 0;
static int g_DisplayVisible = -1; // last bIsVisible seen
static int g_ShowDone = 0, g_HideDone = 0;
static int g_BlockDone = 0, g_ReportDone = 0, g_ProfileDone = 0;

static void EOS_CALL OnDisplay(const EOS_UI_OnDisplaySettingsUpdatedCallbackInfo* Data)
{
	g_DisplayCount++;
	g_DisplayVisible = (Data->bIsVisible == EOS_TRUE) ? 1 : 0;
}
static void EOS_CALL OnShow(const EOS_UI_ShowFriendsCallbackInfo* Data) { (void)Data; g_ShowDone = 1; }
static int g_ShowNotConfigured = 0;
static void EOS_CALL OnShowDisabled(const EOS_UI_ShowFriendsCallbackInfo* Data) { g_ShowNotConfigured = (Data->ResultCode == EOS_NotConfigured); }
static void EOS_CALL OnHide(const EOS_UI_HideFriendsCallbackInfo* Data) { (void)Data; g_HideDone = 1; }
static void EOS_CALL OnBlock(const EOS_UI_OnShowBlockPlayerCallbackInfo* Data) { (void)Data; g_BlockDone = 1; }
static void EOS_CALL OnReport(const EOS_UI_OnShowReportPlayerCallbackInfo* Data) { (void)Data; g_ReportDone = 1; }
static void EOS_CALL OnProfile(const EOS_UI_ShowNativeProfileCallbackInfo* Data) { (void)Data; g_ProfileDone = 1; }

static void Tick(EOS_HPlatform P, int N) { for (int i = 0; i < N; ++i) EOS_Platform_Tick(P); }

int main(void)
{
	EOS_InitializeOptions InitOpts;
	memset(&InitOpts, 0, sizeof(InitOpts));
	InitOpts.ApiVersion = EOS_INITIALIZE_API_LATEST;
	InitOpts.ProductName = "EOSEmuOverlay";
	InitOpts.ProductVersion = "1.0";
	if (EOS_Initialize(&InitOpts) != EOS_Success) { printf("Initialize failed\n"); return 1; }
	EOS_Logging_SetCallback(NULL);

	EOS_Platform_Options PlatOpts;
	memset(&PlatOpts, 0, sizeof(PlatOpts));
	PlatOpts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	PlatOpts.ProductId = "prod";
	PlatOpts.SandboxId = "sand";
	PlatOpts.DeploymentId = "deploy";
	PlatOpts.ClientCredentials.ClientId = "cid";
	PlatOpts.ClientCredentials.ClientSecret = "secret";
	// Like the samples: request the overlay (the enable flags are a hint).
	PlatOpts.Flags = EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D9 | EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D10 | EOS_PF_WINDOWS_ENABLE_OVERLAY_OPENGL;
	EOS_HPlatform Platform = EOS_Platform_Create(&PlatOpts);
	if (!Platform) { printf("Platform_Create failed\n"); EOS_Shutdown(); return 2; }

	EOS_HUI UI = EOS_Platform_GetUIInterface(Platform);
	printf("UI interface=%p\n", (void*)UI);

	EOS_UI_AddNotifyDisplaySettingsUpdatedOptions NOpts;
	memset(&NOpts, 0, sizeof(NOpts));
	NOpts.ApiVersion = EOS_UI_ADDNOTIFYDISPLAYSETTINGSUPDATED_API_LATEST;
	EOS_NotificationId Notify = EOS_UI_AddNotifyDisplaySettingsUpdated(UI, &NOpts, NULL, &OnDisplay);
	Tick(Platform, 5); // initial fire -> not visible

	EOS_UI_GetFriendsVisibleOptions GvOpts;
	memset(&GvOpts, 0, sizeof(GvOpts));
	GvOpts.ApiVersion = EOS_UI_GETFRIENDSVISIBLE_API_LATEST;
	int visibleBefore = (EOS_UI_GetFriendsVisible(UI, &GvOpts) == EOS_TRUE);

	// --- show ---
	EOS_UI_ShowFriendsOptions SfOpts;
	memset(&SfOpts, 0, sizeof(SfOpts));
	SfOpts.ApiVersion = EOS_UI_SHOWFRIENDS_API_LATEST;
	EOS_UI_ShowFriends(UI, &SfOpts, NULL, &OnShow);
	Tick(Platform, 10);
	int visibleAfterShow = (EOS_UI_GetFriendsVisible(UI, &GvOpts) == EOS_TRUE);
	int notifyShow = (g_DisplayVisible == 1);

	// --- hide ---
	EOS_UI_HideFriendsOptions HfOpts;
	memset(&HfOpts, 0, sizeof(HfOpts));
	HfOpts.ApiVersion = EOS_UI_HIDEFRIENDS_API_LATEST;
	EOS_UI_HideFriends(UI, &HfOpts, NULL, &OnHide);
	Tick(Platform, 10);
	int visibleAfterHide = (EOS_UI_GetFriendsVisible(UI, &GvOpts) == EOS_TRUE);
	int notifyHide = (g_DisplayVisible == 0);

	EOS_UI_RemoveNotifyDisplaySettingsUpdated(UI, Notify);

	// --- key combinations ---
	int validCombo = (EOS_UI_IsValidKeyCombination(UI, (EOS_UI_EKeyCombination)(EOS_UIK_Shift | EOS_UIK_F3)) == EOS_TRUE);
	int invalidCombo = (EOS_UI_IsValidKeyCombination(UI, (EOS_UI_EKeyCombination)(EOS_UIK_F3)) == EOS_FALSE); // no modifier

	EOS_UI_SetToggleFriendsKeyOptions SkOpts;
	memset(&SkOpts, 0, sizeof(SkOpts));
	SkOpts.ApiVersion = EOS_UI_SETTOGGLEFRIENDSKEY_API_LATEST;
	SkOpts.KeyCombination = (EOS_UI_EKeyCombination)(EOS_UIK_Shift | EOS_UIK_F4);
	int setKeyOk = (EOS_UI_SetToggleFriendsKey(UI, &SkOpts) == EOS_Success);
	EOS_UI_GetToggleFriendsKeyOptions GkOpts;
	memset(&GkOpts, 0, sizeof(GkOpts));
	GkOpts.ApiVersion = EOS_UI_GETTOGGLEFRIENDSKEY_API_LATEST;
	int getKeyOk = ((int)EOS_UI_GetToggleFriendsKey(UI, &GkOpts) == (int)(EOS_UIK_Shift | EOS_UIK_F4));

	// --- display preference ---
	EOS_UI_SetDisplayPreferenceOptions DpOpts;
	memset(&DpOpts, 0, sizeof(DpOpts));
	DpOpts.ApiVersion = EOS_UI_SETDISPLAYPREFERENCE_API_LATEST;
	DpOpts.NotificationLocation = EOS_UNL_TopLeft;
	int setPrefOk = (EOS_UI_SetDisplayPreference(UI, &DpOpts) == EOS_Success);
	int getPrefOk = (EOS_UI_GetNotificationLocationPreference(UI) == EOS_UNL_TopLeft);

	// --- report input / prepresent are NotImplemented on desktop ---
	EOS_UI_ReportInputStateOptions RiOpts;
	memset(&RiOpts, 0, sizeof(RiOpts));
	RiOpts.ApiVersion = EOS_UI_REPORTINPUTSTATE_API_LATEST;
	int reportNI = (EOS_UI_ReportInputState(UI, &RiOpts) == EOS_NotImplemented);
	EOS_UI_PrePresentOptions PpOpts;
	memset(&PpOpts, 0, sizeof(PpOpts));
	PpOpts.ApiVersion = EOS_UI_PREPRESENT_API_LATEST;
	int prePresentNI = (EOS_UI_PrePresent(UI, &PpOpts) == EOS_NotImplemented);

	// --- pause ---
	EOS_UI_PauseSocialOverlayOptions PauseOpts;
	memset(&PauseOpts, 0, sizeof(PauseOpts));
	PauseOpts.ApiVersion = EOS_UI_PAUSESOCIALOVERLAY_API_LATEST;
	PauseOpts.bIsPaused = EOS_TRUE;
	int pauseSet = (EOS_UI_PauseSocialOverlay(UI, &PauseOpts) == EOS_Success);
	EOS_UI_IsSocialOverlayPausedOptions IsPausedOpts;
	memset(&IsPausedOpts, 0, sizeof(IsPausedOpts));
	IsPausedOpts.ApiVersion = EOS_UI_ISSOCIALOVERLAYPAUSED_API_LATEST;
	int isPaused = (EOS_UI_IsSocialOverlayPaused(UI, &IsPausedOpts) == EOS_TRUE);
	PauseOpts.bIsPaused = EOS_FALSE;
	EOS_UI_PauseSocialOverlay(UI, &PauseOpts);

	// --- block / report / profile ---
	EOS_UI_ShowBlockPlayerOptions BlkOpts;
	memset(&BlkOpts, 0, sizeof(BlkOpts));
	BlkOpts.ApiVersion = EOS_UI_SHOWBLOCKPLAYER_API_LATEST;
	EOS_UI_ShowBlockPlayer(UI, &BlkOpts, NULL, &OnBlock);
	EOS_UI_ShowReportPlayerOptions RepOpts;
	memset(&RepOpts, 0, sizeof(RepOpts));
	RepOpts.ApiVersion = EOS_UI_SHOWREPORTPLAYER_API_LATEST;
	EOS_UI_ShowReportPlayer(UI, &RepOpts, NULL, &OnReport);
	EOS_UI_ShowNativeProfileOptions ProfOpts;
	memset(&ProfOpts, 0, sizeof(ProfOpts));
	ProfOpts.ApiVersion = EOS_UI_SHOWNATIVEPROFILE_API_LATEST;
	EOS_UI_ShowNativeProfile(UI, &ProfOpts, NULL, &OnProfile);
	Tick(Platform, 10);

	EOS_Platform_Release(Platform); // joins the overlay thread

	// --- overlay disabled by flag: lifecycle must still be clean ---
	memset(&PlatOpts.ClientCredentials, 0, sizeof(PlatOpts.ClientCredentials));
	PlatOpts.ClientCredentials.ClientId = "cid";
	PlatOpts.ClientCredentials.ClientSecret = "secret";
	PlatOpts.Flags = EOS_PF_DISABLE_OVERLAY;
	EOS_HPlatform Platform2 = EOS_Platform_Create(&PlatOpts);
	int disabledOk = 0;
	if (Platform2)
	{
		EOS_HUI UI2 = EOS_Platform_GetUIInterface(Platform2);
		g_ShowNotConfigured = 0;
		EOS_UI_ShowFriends(UI2, &SfOpts, NULL, &OnShowDisabled);
		Tick(Platform2, 10);
		EOS_UI_GetFriendsExclusiveInputOptions ExOpts;
		memset(&ExOpts, 0, sizeof(ExOpts));
		ExOpts.ApiVersion = EOS_UI_GETFRIENDSEXCLUSIVEINPUT_API_LATEST;
		int exclusive = (EOS_UI_GetFriendsExclusiveInput(UI2, &ExOpts) == EOS_TRUE);
		// No overlay window exists: ShowFriends must say NotConfigured rather
		// than claim a visibility it cannot deliver, GetFriendsVisible must
		// stay false, and input is never exclusive.
		int visibleDisabled = (EOS_UI_GetFriendsVisible(UI2, &GvOpts) == EOS_TRUE);
		disabledOk = (UI2 != NULL) && !exclusive && g_ShowNotConfigured && !visibleDisabled;
		EOS_Platform_Release(Platform2);
	}

	EOS_Shutdown();

	printf("visibleBefore=%d show{done=%d vis=%d notify=%d} hide{done=%d vis=%d notify=%d} displayCount=%d\n",
		visibleBefore, g_ShowDone, visibleAfterShow, notifyShow, g_HideDone, visibleAfterHide, notifyHide, g_DisplayCount);
	printf("keys{valid=%d invalid=%d set=%d get=%d} pref{set=%d get=%d} ni{report=%d prepresent=%d} pause{set=%d is=%d}\n",
		validCombo, invalidCombo, setKeyOk, getKeyOk, setPrefOk, getPrefOk, reportNI, prePresentNI, pauseSet, isPaused);
	printf("actions{block=%d report=%d profile=%d} disabledOk=%d\n", g_BlockDone, g_ReportDone, g_ProfileDone, disabledOk);

	int ok = !visibleBefore
		&& g_ShowDone && visibleAfterShow && notifyShow
		&& g_HideDone && !visibleAfterHide && notifyHide && g_DisplayCount >= 2
		&& validCombo && invalidCombo && setKeyOk && getKeyOk
		&& setPrefOk && getPrefOk && reportNI && prePresentNI && pauseSet && isPaused
		&& g_BlockDone && g_ReportDone && g_ProfileDone && disabledOk;
	printf("OVERLAY %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 3;
}
