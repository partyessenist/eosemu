// Reports test for EOSEmu.
//
// A game's "report player" button must not visibly fail: a valid
// EOS_Reports_SendPlayerBehaviorReport completes with EOS_Success (accepted
// locally, logged), while an invalid one (no reported user) completes with
// EOS_InvalidParameters. Run by run_reports.ps1.

#include <stdio.h>
#include <string.h>

#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_reports.h"

static int g_ConnectDone = 0;
static EOS_ProductUserId g_LocalPuid = NULL;
static int g_GoodDone = 0, g_GoodOk = 0;
static int g_BadDone = 0, g_BadOk = 0;

static void EOS_CALL OnConnectLogin(const EOS_Connect_LoginCallbackInfo* Data)
{
	g_LocalPuid = Data->LocalUserId;
	g_ConnectDone = 1;
}

static void EOS_CALL OnGoodReport(const EOS_Reports_SendPlayerBehaviorReportCompleteCallbackInfo* Data)
{
	printf("  good report result=%s\n", EOS_EResult_ToString(Data->ResultCode));
	g_GoodDone = 1;
	g_GoodOk = (Data->ResultCode == EOS_Success);
}

static void EOS_CALL OnBadReport(const EOS_Reports_SendPlayerBehaviorReportCompleteCallbackInfo* Data)
{
	printf("  bad report result=%s\n", EOS_EResult_ToString(Data->ResultCode));
	g_BadDone = 1;
	g_BadOk = (Data->ResultCode == EOS_InvalidParameters);
}

int main(void)
{
	EOS_InitializeOptions InitOpts;
	memset(&InitOpts, 0, sizeof(InitOpts));
	InitOpts.ApiVersion = EOS_INITIALIZE_API_LATEST;
	InitOpts.ProductName = "EOSEmuReports";
	InitOpts.ProductVersion = "1.0";
	if (EOS_Initialize(&InitOpts) != EOS_Success) { printf("Initialize failed\n"); return 1; }

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

	// Connect login for a real local Product User ID.
	EOS_HConnect Connect = EOS_Platform_GetConnectInterface(Platform);
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
	for (int i = 0; i < 50 && !g_ConnectDone; ++i) { EOS_Platform_Tick(Platform); }
	if (!g_LocalPuid) { printf("connect login failed\n"); return 3; }

	EOS_HReports Reports = EOS_Platform_GetReportsInterface(Platform);
	printf("Reports interface=%p\n", (void*)Reports);

	// A valid report (self-report keeps the test single-process).
	EOS_Reports_SendPlayerBehaviorReportOptions Good;
	memset(&Good, 0, sizeof(Good));
	Good.ApiVersion = EOS_REPORTS_SENDPLAYERBEHAVIORREPORT_API_LATEST;
	Good.ReporterUserId = g_LocalPuid;
	Good.ReportedUserId = g_LocalPuid;
	Good.Category = EOS_PRC_Cheating;
	Good.Message = "speed hacking";
	Good.Context = "{\"match\":\"42\"}";
	EOS_Reports_SendPlayerBehaviorReport(Reports, &Good, NULL, &OnGoodReport);

	// Missing reported user -> InvalidParameters.
	EOS_Reports_SendPlayerBehaviorReportOptions Bad;
	memset(&Bad, 0, sizeof(Bad));
	Bad.ApiVersion = EOS_REPORTS_SENDPLAYERBEHAVIORREPORT_API_LATEST;
	Bad.ReporterUserId = g_LocalPuid;
	Bad.ReportedUserId = NULL;
	Bad.Category = EOS_PRC_VerbalAbuse;
	EOS_Reports_SendPlayerBehaviorReport(Reports, &Bad, NULL, &OnBadReport);

	for (int i = 0; i < 50 && (!g_GoodDone || !g_BadDone); ++i) { EOS_Platform_Tick(Platform); }

	EOS_Platform_Release(Platform);
	EOS_Shutdown();

	printf("good=%d bad=%d\n", g_GoodOk, g_BadOk);
	int ok = g_GoodOk && g_BadOk;
	printf("REPORTS %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 4;
}
