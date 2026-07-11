// Standalone smoke test for EOSEmu.
//
// Exercises the same lifecycle the headless samples do: Initialize -> Create ->
// Auth login -> Connect login -> Tick loop draining callbacks -> Release ->
// Shutdown. Links the SDK import library by name, so the EOSEmu DLL placed
// beside the exe is what actually runs.
//
// Build (from EOSEmu/): see tests/run_smoke.ps1

#include <stdio.h>
#include <string.h>

#include "eos_version.h"
#include "eos_sdk.h"
#include "eos_auth.h"
#include "eos_connect.h"
#include "eos_logging.h"
#include "eos_p2p.h"

static int g_AuthDone = 0;
static int g_ConnectDone = 0;
static EOS_ProductUserId g_LocalPuid = NULL;

static void EOS_CALL LogCb(const EOS_LogMessage* Msg)
{
	printf("  [log %s] %s\n", Msg->Category ? Msg->Category : "?", Msg->Message ? Msg->Message : "");
}

static void EOS_CALL OnAuthLogin(const EOS_Auth_LoginCallbackInfo* Data)
{
	printf("  auth login result=%s\n", EOS_EResult_ToString(Data->ResultCode));
	g_AuthDone = 1;
}

static void EOS_CALL OnConnectLogin(const EOS_Connect_LoginCallbackInfo* Data)
{
	printf("  connect login result=%s\n", EOS_EResult_ToString(Data->ResultCode));
	g_LocalPuid = Data->LocalUserId;
	g_ConnectDone = 1;
}

int main(void)
{
	printf("EOSEmu version: %s\n", EOS_GetVersion());

	EOS_InitializeOptions InitOpts;
	memset(&InitOpts, 0, sizeof(InitOpts));
	InitOpts.ApiVersion = EOS_INITIALIZE_API_LATEST;
	InitOpts.ProductName = "EOSEmuSmoke";
	InitOpts.ProductVersion = "1.0";
	EOS_EResult R = EOS_Initialize(&InitOpts);
	printf("Initialize: %s\n", EOS_EResult_ToString(R));
	if (R != EOS_Success) return 1;

	EOS_Logging_SetCallback(&LogCb);
	EOS_Logging_SetLogLevel(EOS_LC_ALL_CATEGORIES, EOS_LOG_Verbose);

	EOS_Platform_Options PlatOpts;
	memset(&PlatOpts, 0, sizeof(PlatOpts));
	PlatOpts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	PlatOpts.ProductId = "prod";
	PlatOpts.SandboxId = "sand";
	PlatOpts.DeploymentId = "deploy";
	PlatOpts.ClientCredentials.ClientId = "cid";
	PlatOpts.ClientCredentials.ClientSecret = "secret";
	EOS_HPlatform Platform = EOS_Platform_Create(&PlatOpts);
	printf("Platform_Create: %p\n", (void*)Platform);
	if (!Platform) { EOS_Shutdown(); return 2; }

	EOS_HAuth Auth = EOS_Platform_GetAuthInterface(Platform);
	EOS_HConnect Connect = EOS_Platform_GetConnectInterface(Platform);
	EOS_HP2P P2P = EOS_Platform_GetP2PInterface(Platform);
	printf("Auth=%p Connect=%p P2P=%p\n", (void*)Auth, (void*)Connect, (void*)P2P);

	EOS_Auth_Credentials Creds;
	memset(&Creds, 0, sizeof(Creds));
	Creds.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
	Creds.Id = "localhost:6300";
	Creds.Token = "devuser";
	Creds.Type = EOS_LCT_Developer;

	EOS_Auth_LoginOptions LoginOpts;
	memset(&LoginOpts, 0, sizeof(LoginOpts));
	LoginOpts.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
	LoginOpts.Credentials = &Creds;
	EOS_Auth_Login(Auth, &LoginOpts, NULL, &OnAuthLogin);

	int i;
	for (i = 0; i < 50 && !g_AuthDone; ++i) { EOS_Platform_Tick(Platform); }

	EOS_EpicAccountId Eaid = EOS_Auth_GetLoggedInAccountByIndex(Auth, 0);
	char Buf[64]; int32_t Len = sizeof(Buf);
	if (Eaid && EOS_EpicAccountId_ToString(Eaid, Buf, &Len) == EOS_Success)
		printf("  logged in EpicAccountId=%s\n", Buf);

	EOS_Connect_Credentials CCreds;
	memset(&CCreds, 0, sizeof(CCreds));
	CCreds.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
	CCreds.Token = "eosemu-access-token";
	CCreds.Type = EOS_ECT_EPIC;

	EOS_Connect_LoginOptions CLogin;
	memset(&CLogin, 0, sizeof(CLogin));
	CLogin.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
	CLogin.Credentials = &CCreds;
	EOS_Connect_Login(Connect, &CLogin, NULL, &OnConnectLogin);

	for (i = 0; i < 50 && !g_ConnectDone; ++i) { EOS_Platform_Tick(Platform); }

	if (g_LocalPuid)
	{
		char PBuf[64]; int32_t PLen = sizeof(PBuf);
		if (EOS_ProductUserId_ToString(g_LocalPuid, PBuf, &PLen) == EOS_Success)
			printf("  local ProductUserId=%s\n", PBuf);
	}

	// A couple more ticks let a discovery Hello go out without crashing.
	for (i = 0; i < 5; ++i) { EOS_Platform_Tick(Platform); }

	printf("auth done=%d connect done=%d\n", g_AuthDone, g_ConnectDone);

	EOS_Platform_Release(Platform);
	R = EOS_Shutdown();
	printf("Shutdown: %s\n", EOS_EResult_ToString(R));

	int ok = g_AuthDone && g_ConnectDone && g_LocalPuid != NULL;
	printf("SMOKE %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 3;
}
