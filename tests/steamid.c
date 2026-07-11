// Steam external-account id test for EOSEmu.
//
// The local user's SteamID64 reported through EOS_Connect's
// ExternalAccountInfo must be the *real* one, not a synthesised value.
// Resolution order (Platform::LocalSteamId):
//   [Identity] SteamId config pin > live steam_api module query > the Steam
//   session ticket passed at login > synthesised fallback.
// run_steamid.ps1 drives three scenarios through this one harness:
//   1. ticket only              -> the id embedded in the session ticket
//   2. ticket + config pin      -> the configured id wins
//   3. ticket + fake steam_api  -> the module's id wins
//
// Usage: steamid.exe <expected_id> <hex_session_ticket> <load_fake_module:0|1>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_logging.h"

static int g_ConnectDone = 0;
static EOS_ProductUserId g_LocalPuid = NULL;

static void EOS_CALL LogCb(const EOS_LogMessage* Msg)
{
	printf("  [log %s] %s\n", Msg->Category ? Msg->Category : "?", Msg->Message ? Msg->Message : "");
}

static void EOS_CALL OnConnectLogin(const EOS_Connect_LoginCallbackInfo* Data)
{
	printf("  connect login result=%s\n", EOS_EResult_ToString(Data->ResultCode));
	g_LocalPuid = Data->LocalUserId;
	g_ConnectDone = 1;
}

int main(int argc, char** argv)
{
	if (argc < 4)
	{
		printf("usage: steamid.exe <expected_id> <hex_session_ticket> <load_fake_module:0|1>\n");
		return 1;
	}
	const char* Expected = argv[1];
	const char* Ticket = argv[2];
	const int LoadFake = atoi(argv[3]);

	if (LoadFake)
	{
		// The loader searches the exe directory first, where run_steamid.ps1
		// placed the fake. EOSEmu must find it via GetModuleHandle by name.
		if (!LoadLibraryA("steam_api64.dll"))
		{
			printf("LoadLibrary(steam_api64.dll) failed\n");
			return 1;
		}
	}

	EOS_InitializeOptions InitOpts;
	memset(&InitOpts, 0, sizeof(InitOpts));
	InitOpts.ApiVersion = EOS_INITIALIZE_API_LATEST;
	InitOpts.ProductName = "EOSEmuSteamId";
	InitOpts.ProductVersion = "1.0";
	if (EOS_Initialize(&InitOpts) != EOS_Success) { printf("Initialize failed\n"); return 1; }

	EOS_Logging_SetCallback(&LogCb);
	EOS_Logging_SetLogLevel(EOS_LC_ALL_CATEGORIES, EOS_LOG_Warning);

	EOS_Platform_Options PlatOpts;
	memset(&PlatOpts, 0, sizeof(PlatOpts));
	PlatOpts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	PlatOpts.ProductId = "prod";
	PlatOpts.SandboxId = "sand";
	PlatOpts.DeploymentId = "deploy";
	PlatOpts.ClientCredentials.ClientId = "cid";
	PlatOpts.ClientCredentials.ClientSecret = "secret";
	EOS_HPlatform Platform = EOS_Platform_Create(&PlatOpts);
	if (!Platform) { printf("Platform_Create failed\n"); EOS_Shutdown(); return 2; }

	// Login the way a Steam game does: a Connect login carrying the session
	// ticket. (Games may also route one through EOS_Auth ExternalAuth; both
	// paths feed the same Platform::NoteSteamSessionTicket.)
	EOS_HConnect Connect = EOS_Platform_GetConnectInterface(Platform);
	EOS_Connect_Credentials Creds;
	memset(&Creds, 0, sizeof(Creds));
	Creds.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
	Creds.Token = Ticket;
	Creds.Type = EOS_ECT_STEAM_SESSION_TICKET;
	EOS_Connect_LoginOptions Login;
	memset(&Login, 0, sizeof(Login));
	Login.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
	Login.Credentials = &Creds;
	EOS_Connect_Login(Connect, &Login, NULL, &OnConnectLogin);
	for (int i = 0; i < 50 && !g_ConnectDone; ++i) { EOS_Platform_Tick(Platform); }
	if (!g_ConnectDone || !g_LocalPuid) { printf("connect login did not complete\n"); return 2; }

	// The id must surface through every copy that reports the Steam account.
	int typeOk = 0, byTypeOk = 0, byIndexOk = 0;

	EOS_Connect_CopyProductUserExternalAccountByAccountTypeOptions ByType;
	memset(&ByType, 0, sizeof(ByType));
	ByType.ApiVersion = EOS_CONNECT_COPYPRODUCTUSEREXTERNALACCOUNTBYACCOUNTTYPE_API_LATEST;
	ByType.TargetUserId = g_LocalPuid;
	ByType.AccountIdType = EOS_EAT_STEAM;
	EOS_Connect_ExternalAccountInfo* Info = NULL;
	if (EOS_Connect_CopyProductUserExternalAccountByAccountType(Connect, &ByType, &Info) == EOS_Success && Info)
	{
		printf("ByAccountType AccountId=%s type=%d (expect %s, type %d)\n",
			Info->AccountId ? Info->AccountId : "(null)", (int)Info->AccountIdType,
			Expected, (int)EOS_EAT_STEAM);
		typeOk = (Info->AccountIdType == EOS_EAT_STEAM);
		byTypeOk = (Info->AccountId && strcmp(Info->AccountId, Expected) == 0);
		EOS_Connect_ExternalAccountInfo_Release(Info);
	}
	else
	{
		printf("CopyProductUserExternalAccountByAccountType(STEAM) failed\n");
	}

	EOS_Connect_CopyProductUserExternalAccountByIndexOptions ByIndex;
	memset(&ByIndex, 0, sizeof(ByIndex));
	ByIndex.ApiVersion = EOS_CONNECT_COPYPRODUCTUSEREXTERNALACCOUNTBYINDEX_API_LATEST;
	ByIndex.TargetUserId = g_LocalPuid;
	ByIndex.ExternalAccountInfoIndex = 0;
	Info = NULL;
	if (EOS_Connect_CopyProductUserExternalAccountByIndex(Connect, &ByIndex, &Info) == EOS_Success && Info)
	{
		printf("ByIndex AccountId=%s (expect %s)\n",
			Info->AccountId ? Info->AccountId : "(null)", Expected);
		byIndexOk = (Info->AccountId && strcmp(Info->AccountId, Expected) == 0);
		EOS_Connect_ExternalAccountInfo_Release(Info);
	}
	else
	{
		printf("CopyProductUserExternalAccountByIndex(0) failed\n");
	}

	EOS_Platform_Release(Platform);
	EOS_Shutdown();

	const int ok = typeOk && byTypeOk && byIndexOk;
	printf("type=%d byType=%d byIndex=%d\n", typeOk, byTypeOk, byIndexOk);
	printf("STEAMID %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 3;
}
