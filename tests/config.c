// Config-driven behaviour test for EOSEmu.
//
// Runs the standard lifecycle with an eosemu.ini supplied via %EOSEMU_CONFIG%
// (run_config.ps1 writes the file and sets the env var), then asserts that the
// configured values surface through the public API:
//   [Identity] DisplayName -> EOS_UserInfo_CopyBestDisplayName
//   [Identity] Language    -> EOS_Platform_GetActiveLocaleCode / GetOverrideLocaleCode
//   [Ecom] OwnedItem       -> EOS_Ecom_QueryOwnership (EOS_OS_Owned)
//   [Ecom] Entitlement     -> EOS_Ecom_QueryEntitlements + CopyEntitlementById
//
// The expected values below must match the ini written by run_config.ps1.

#include <stdio.h>
#include <string.h>

#include "eos_sdk.h"
#include "eos_auth.h"
#include "eos_connect.h"
#include "eos_logging.h"
#include "eos_userinfo.h"
#include "eos_ecom.h"
#include "eos_stats.h"
#include "eos_achievements.h"
#include "eos_sanctions.h"

#define EXPECT_DISPLAYNAME    "ConfiguredHero"
#define EXPECT_LANGUAGE       "fr"
#define EXPECT_ITEM           "cat_item_001"
#define EXPECT_ENT_NAME       "season_pass"
#define EXPECT_ENT_ID         "ent_season"
#define EXPECT_STAT_NAME      "kills"
#define EXPECT_STAT_VALUE     7
#define EXPECT_ACHIEVEMENT    "ach_first_win"
#define EXPECT_SANCTION_ACTION "RESTRICT_GAME_ACCESS"

static int g_AuthDone = 0;
static int g_ConnectDone = 0;
static EOS_ProductUserId g_LocalPuid = NULL;
static int g_OwnershipChecked = 0;   // 1 once the ownership callback ran
static int g_OwnershipOwned = 0;     // 1 if the item came back EOS_OS_Owned
static int g_EntitlementsQueried = 0;
static int g_SanctionsQueried = 0;

static void EOS_CALL LogCb(const EOS_LogMessage* Msg)
{
	printf("  [log %s] %s\n", Msg->Category ? Msg->Category : "?", Msg->Message ? Msg->Message : "");
}

static void EOS_CALL OnAuthLogin(const EOS_Auth_LoginCallbackInfo* Data)
{
	printf("  auth login result=%s\n", EOS_EResult_ToString(Data->ResultCode));
	g_AuthDone = 1;
}

static void EOS_CALL OnQueryOwnership(const EOS_Ecom_QueryOwnershipCallbackInfo* Data)
{
	g_OwnershipChecked = 1;
	printf("  QueryOwnership result=%s count=%u\n", EOS_EResult_ToString(Data->ResultCode), Data->ItemOwnershipCount);
	for (uint32_t i = 0; i < Data->ItemOwnershipCount; ++i)
	{
		printf("    item '%s' status=%d\n", Data->ItemOwnership[i].Id, (int)Data->ItemOwnership[i].OwnershipStatus);
		if (Data->ItemOwnership[i].Id && strcmp(Data->ItemOwnership[i].Id, EXPECT_ITEM) == 0
			&& Data->ItemOwnership[i].OwnershipStatus == EOS_OS_Owned)
		{
			g_OwnershipOwned = 1;
		}
	}
}

static void EOS_CALL OnQueryEntitlements(const EOS_Ecom_QueryEntitlementsCallbackInfo* Data)
{
	printf("  QueryEntitlements result=%s\n", EOS_EResult_ToString(Data->ResultCode));
	g_EntitlementsQueried = 1;
}

static void EOS_CALL OnConnectLogin(const EOS_Connect_LoginCallbackInfo* Data)
{
	g_LocalPuid = Data->LocalUserId;
	g_ConnectDone = 1;
}

static void EOS_CALL OnQuerySanctions(const EOS_Sanctions_QueryActivePlayerSanctionsCallbackInfo* Data)
{
	printf("  QuerySanctions result=%s\n", EOS_EResult_ToString(Data->ResultCode));
	g_SanctionsQueried = 1;
}

int main(void)
{
	EOS_InitializeOptions InitOpts;
	memset(&InitOpts, 0, sizeof(InitOpts));
	InitOpts.ApiVersion = EOS_INITIALIZE_API_LATEST;
	InitOpts.ProductName = "EOSEmuConfig";
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

	EOS_HAuth Auth = EOS_Platform_GetAuthInterface(Platform);
	EOS_HUserInfo UserInfo = EOS_Platform_GetUserInfoInterface(Platform);
	EOS_HEcom Ecom = EOS_Platform_GetEcomInterface(Platform);
	printf("Ecom interface=%p\n", (void*)Ecom);

	// --- locale (available immediately after Create) ---
	char Locale[16]; int32_t LocaleLen = sizeof(Locale);
	int localeOk = 0;
	if (EOS_Platform_GetActiveLocaleCode(Platform, NULL, Locale, &LocaleLen) == EOS_Success)
	{
		printf("ActiveLocaleCode=%s (expect %s)\n", Locale, EXPECT_LANGUAGE);
		localeOk = (strcmp(Locale, EXPECT_LANGUAGE) == 0);
	}
	char OverLocale[16]; int32_t OverLen = sizeof(OverLocale);
	if (EOS_Platform_GetOverrideLocaleCode(Platform, OverLocale, &OverLen) == EOS_Success)
	{
		printf("OverrideLocaleCode=%s\n", OverLocale);
		if (strcmp(OverLocale, EXPECT_LANGUAGE) != 0) localeOk = 0;
	}

	// --- auth login to obtain the local Epic account id ---
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
	for (int i = 0; i < 50 && !g_AuthDone; ++i) { EOS_Platform_Tick(Platform); }

	EOS_EpicAccountId Eaid = EOS_Auth_GetLoggedInAccountByIndex(Auth, 0);

	// Connect login to obtain the local Product User ID (needed for the
	// Stats/Achievements/Sanctions stores, which are keyed by PUID).
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

	// --- display name ---
	int nameOk = 0;
	EOS_UserInfo_CopyBestDisplayNameOptions BdOpts;
	memset(&BdOpts, 0, sizeof(BdOpts));
	BdOpts.ApiVersion = EOS_USERINFO_COPYBESTDISPLAYNAME_API_LATEST;
	BdOpts.LocalUserId = Eaid;
	BdOpts.TargetUserId = Eaid;
	EOS_UserInfo_BestDisplayName* Bd = NULL;
	if (EOS_UserInfo_CopyBestDisplayName(UserInfo, &BdOpts, &Bd) == EOS_Success && Bd)
	{
		printf("BestDisplayName=%s (expect %s)\n", Bd->DisplayName ? Bd->DisplayName : "(null)", EXPECT_DISPLAYNAME);
		nameOk = (Bd->DisplayName && strcmp(Bd->DisplayName, EXPECT_DISPLAYNAME) == 0);
		EOS_UserInfo_BestDisplayName_Release(Bd);
	}

	// --- ownership ---
	const char* ItemIds[1] = { EXPECT_ITEM };
	EOS_Ecom_QueryOwnershipOptions QoOpts;
	memset(&QoOpts, 0, sizeof(QoOpts));
	QoOpts.ApiVersion = EOS_ECOM_QUERYOWNERSHIP_API_LATEST;
	QoOpts.LocalUserId = Eaid;
	QoOpts.CatalogItemIds = (EOS_Ecom_CatalogItemId*)ItemIds;
	QoOpts.CatalogItemIdCount = 1;
	QoOpts.CatalogNamespace = NULL;
	EOS_Ecom_QueryOwnership(Ecom, &QoOpts, NULL, &OnQueryOwnership);
	for (int i = 0; i < 50 && !g_OwnershipChecked; ++i) { EOS_Platform_Tick(Platform); }

	// --- entitlements ---
	EOS_Ecom_QueryEntitlementsOptions QeOpts;
	memset(&QeOpts, 0, sizeof(QeOpts));
	QeOpts.ApiVersion = EOS_ECOM_QUERYENTITLEMENTS_API_LATEST;
	QeOpts.LocalUserId = Eaid;
	QeOpts.EntitlementNames = NULL;
	QeOpts.EntitlementNameCount = 0;
	QeOpts.bIncludeRedeemed = EOS_TRUE;
	EOS_Ecom_QueryEntitlements(Ecom, &QeOpts, NULL, &OnQueryEntitlements);
	for (int i = 0; i < 50 && !g_EntitlementsQueried; ++i) { EOS_Platform_Tick(Platform); }

	// Exactly one entitlement is configured; loading the same file via both
	// %EOSEMU_CONFIG% and the DLL-adjacent search entry must not double it.
	uint32_t entCount = EOS_Ecom_GetEntitlementsCount(Ecom, NULL);
	printf("GetEntitlementsCount=%u (expect 1)\n", entCount);
	int countOk = (entCount == 1);

	int entOk = 0;
	EOS_Ecom_CopyEntitlementByIdOptions CeOpts;
	memset(&CeOpts, 0, sizeof(CeOpts));
	CeOpts.ApiVersion = EOS_ECOM_COPYENTITLEMENTBYID_API_LATEST;
	CeOpts.LocalUserId = Eaid;
	CeOpts.EntitlementId = EXPECT_ENT_ID;
	EOS_Ecom_Entitlement* Ent = NULL;
	if (EOS_Ecom_CopyEntitlementById(Ecom, &CeOpts, &Ent) == EOS_Success && Ent)
	{
		printf("Entitlement name=%s id=%s item=%s\n",
			Ent->EntitlementName, Ent->EntitlementId, Ent->CatalogItemId);
		entOk = Ent->EntitlementName && strcmp(Ent->EntitlementName, EXPECT_ENT_NAME) == 0
			&& Ent->EntitlementId && strcmp(Ent->EntitlementId, EXPECT_ENT_ID) == 0
			&& Ent->CatalogItemId && strcmp(Ent->CatalogItemId, EXPECT_ITEM) == 0;
		EOS_Ecom_Entitlement_Release(Ent);
	}

	// --- seeded stat ([Stats] kills = 7) ---
	int statOk = 0;
	EOS_HStats Stats = EOS_Platform_GetStatsInterface(Platform);
	EOS_Stats_CopyStatByNameOptions CsOpts;
	memset(&CsOpts, 0, sizeof(CsOpts));
	CsOpts.ApiVersion = EOS_STATS_COPYSTATBYNAME_API_LATEST;
	CsOpts.TargetUserId = g_LocalPuid;
	CsOpts.Name = EXPECT_STAT_NAME;
	EOS_Stats_Stat* Stat = NULL;
	if (EOS_Stats_CopyStatByName(Stats, &CsOpts, &Stat) == EOS_Success && Stat)
	{
		printf("Stat %s=%d (expect %d)\n", Stat->Name, Stat->Value, EXPECT_STAT_VALUE);
		statOk = (Stat->Value == EXPECT_STAT_VALUE);
		EOS_Stats_Stat_Release(Stat);
	}

	// --- seeded achievement ([Achievements] Unlocked = ach_first_win) ---
	int achOk = 0;
	EOS_HAchievements Ach = EOS_Platform_GetAchievementsInterface(Platform);
	EOS_Achievements_CopyUnlockedAchievementByAchievementIdOptions CuOpts;
	memset(&CuOpts, 0, sizeof(CuOpts));
	CuOpts.ApiVersion = EOS_ACHIEVEMENTS_COPYUNLOCKEDACHIEVEMENTBYACHIEVEMENTID_API_LATEST;
	CuOpts.UserId = g_LocalPuid;
	CuOpts.AchievementId = EXPECT_ACHIEVEMENT;
	EOS_Achievements_UnlockedAchievement* Unlocked = NULL;
	if (EOS_Achievements_CopyUnlockedAchievementByAchievementId(Ach, &CuOpts, &Unlocked) == EOS_Success && Unlocked)
	{
		printf("Unlocked achievement=%s\n", Unlocked->AchievementId);
		achOk = (Unlocked->AchievementId && strcmp(Unlocked->AchievementId, EXPECT_ACHIEVEMENT) == 0);
		EOS_Achievements_UnlockedAchievement_Release(Unlocked);
	}

	// --- sanction ([Sanctions] Sanctioned = true) ---
	int sanctionOk = 0;
	EOS_HSanctions Sanctions = EOS_Platform_GetSanctionsInterface(Platform);
	EOS_Sanctions_QueryActivePlayerSanctionsOptions QsOpts;
	memset(&QsOpts, 0, sizeof(QsOpts));
	QsOpts.ApiVersion = EOS_SANCTIONS_QUERYACTIVEPLAYERSANCTIONS_API_LATEST;
	QsOpts.TargetUserId = g_LocalPuid;
	QsOpts.LocalUserId = g_LocalPuid;
	EOS_Sanctions_QueryActivePlayerSanctions(Sanctions, &QsOpts, NULL, &OnQuerySanctions);
	for (int i = 0; i < 50 && !g_SanctionsQueried; ++i) { EOS_Platform_Tick(Platform); }

	EOS_Sanctions_GetPlayerSanctionCountOptions ScOpts;
	memset(&ScOpts, 0, sizeof(ScOpts));
	ScOpts.ApiVersion = EOS_SANCTIONS_GETPLAYERSANCTIONCOUNT_API_LATEST;
	ScOpts.TargetUserId = g_LocalPuid;
	uint32_t sanctionCount = EOS_Sanctions_GetPlayerSanctionCount(Sanctions, &ScOpts);
	printf("PlayerSanctionCount=%u (expect 1)\n", sanctionCount);
	if (sanctionCount == 1)
	{
		EOS_Sanctions_CopyPlayerSanctionByIndexOptions CpOpts;
		memset(&CpOpts, 0, sizeof(CpOpts));
		CpOpts.ApiVersion = EOS_SANCTIONS_COPYPLAYERSANCTIONBYINDEX_API_LATEST;
		CpOpts.TargetUserId = g_LocalPuid;
		CpOpts.SanctionIndex = 0;
		EOS_Sanctions_PlayerSanction* Sanction = NULL;
		if (EOS_Sanctions_CopyPlayerSanctionByIndex(Sanctions, &CpOpts, &Sanction) == EOS_Success && Sanction)
		{
			printf("Sanction action=%s ref=%s\n", Sanction->Action, Sanction->ReferenceId);
			sanctionOk = (Sanction->Action && strcmp(Sanction->Action, EXPECT_SANCTION_ACTION) == 0);
			EOS_Sanctions_PlayerSanction_Release(Sanction);
		}
	}

	EOS_Platform_Release(Platform);
	EOS_Shutdown();

	printf("locale=%d name=%d owned=%d entitlement=%d count=%d stat=%d ach=%d sanction=%d\n",
		localeOk, nameOk, g_OwnershipOwned, entOk, countOk, statOk, achOk, sanctionOk);
	int ok = localeOk && nameOk && g_OwnershipOwned && entOk && countOk && statOk && achOk && sanctionOk;
	printf("CONFIG %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 3;
}
