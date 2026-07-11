// Mods test for EOSEmu.
//
// run_mods.ps1 supplies an eosemu.ini declaring two mods via %EOSEMU_CONFIG%;
// this asserts the enumerate/copy/install/uninstall/update surface:
//   - CopyModInfo before any EnumerateMods -> EOS_NotFound (documented)
//   - EnumerateMods(INSTALLED) -> Success; CopyModInfo -> both config mods
//   - UninstallMod removes one; InstallMod adds a new one; UpdateMod of an
//     unknown ItemId -> NotFound.
// The expected values must match the ini written by run_mods.ps1.

#include <stdio.h>
#include <string.h>

#include "eos_sdk.h"
#include "eos_mods.h"

#define EXPECT_NAMESPACE "testns"
#define EXPECT_MOD1_ID   "coolmap"
#define EXPECT_MOD1_TITLE "Cool Map Pack"
#define EXPECT_MOD1_VER  "1.2.0"
#define EXPECT_MOD2_ID   "skins"
#define EXPECT_MOD2_ART  "art_skins"

static int g_EnumDone = 0, g_EnumOk = 0;
static int g_UninstallDone = 0, g_UninstallOk = 0;
static int g_InstallDone = 0, g_InstallOk = 0;
static int g_UpdateDone = 0, g_UpdateOk = 0;

static void EOS_CALL OnEnumerate(const EOS_Mods_EnumerateModsCallbackInfo* Data)
{
	printf("  EnumerateMods result=%s type=%d\n", EOS_EResult_ToString(Data->ResultCode), (int)Data->Type);
	g_EnumDone = 1;
	g_EnumOk = (Data->ResultCode == EOS_Success);
}

static void EOS_CALL OnUninstall(const EOS_Mods_UninstallModCallbackInfo* Data)
{
	printf("  UninstallMod result=%s mod=%s\n", EOS_EResult_ToString(Data->ResultCode),
		Data->Mod && Data->Mod->ItemId ? Data->Mod->ItemId : "(null)");
	g_UninstallDone = 1;
	g_UninstallOk = (Data->ResultCode == EOS_Success
		&& Data->Mod && Data->Mod->ItemId && strcmp(Data->Mod->ItemId, EXPECT_MOD1_ID) == 0);
}

static void EOS_CALL OnInstall(const EOS_Mods_InstallModCallbackInfo* Data)
{
	printf("  InstallMod result=%s\n", EOS_EResult_ToString(Data->ResultCode));
	g_InstallDone = 1;
	g_InstallOk = (Data->ResultCode == EOS_Success);
}

static void EOS_CALL OnUpdateUnknown(const EOS_Mods_UpdateModCallbackInfo* Data)
{
	printf("  UpdateMod(unknown) result=%s\n", EOS_EResult_ToString(Data->ResultCode));
	g_UpdateDone = 1;
	g_UpdateOk = (Data->ResultCode == EOS_NotFound);
}

static int CountInstalled(EOS_HMods Mods, const char* Phase, int* FoundMod1, int* FoundMod2)
{
	EOS_Mods_CopyModInfoOptions Opts;
	memset(&Opts, 0, sizeof(Opts));
	Opts.ApiVersion = EOS_MODS_COPYMODINFO_API_LATEST;
	Opts.Type = EOS_MET_INSTALLED;
	EOS_Mods_ModInfo* Info = NULL;
	if (FoundMod1) *FoundMod1 = 0;
	if (FoundMod2) *FoundMod2 = 0;
	if (EOS_Mods_CopyModInfo(Mods, &Opts, &Info) != EOS_Success || !Info)
	{
		printf("  CopyModInfo(%s) failed\n", Phase);
		return -1;
	}
	int Count = Info->ModsCount;
	printf("  CopyModInfo(%s): %d mod(s)\n", Phase, Count);
	for (int i = 0; i < Count; ++i)
	{
		const EOS_Mod_Identifier* M = &Info->Mods[i];
		printf("    ns=%s id=%s art=%s title=%s ver=%s\n",
			M->NamespaceId, M->ItemId, M->ArtifactId, M->Title, M->Version);
		if (FoundMod1 && M->ItemId && strcmp(M->ItemId, EXPECT_MOD1_ID) == 0
			&& M->NamespaceId && strcmp(M->NamespaceId, EXPECT_NAMESPACE) == 0
			&& M->Title && strcmp(M->Title, EXPECT_MOD1_TITLE) == 0
			&& M->Version && strcmp(M->Version, EXPECT_MOD1_VER) == 0)
		{
			*FoundMod1 = 1;
		}
		if (FoundMod2 && M->ItemId && strcmp(M->ItemId, EXPECT_MOD2_ID) == 0
			&& M->ArtifactId && strcmp(M->ArtifactId, EXPECT_MOD2_ART) == 0)
		{
			*FoundMod2 = 1;
		}
	}
	EOS_Mods_ModInfo_Release(Info);
	return Count;
}

int main(void)
{
	EOS_InitializeOptions InitOpts;
	memset(&InitOpts, 0, sizeof(InitOpts));
	InitOpts.ApiVersion = EOS_INITIALIZE_API_LATEST;
	InitOpts.ProductName = "EOSEmuMods";
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

	EOS_HMods Mods = EOS_Platform_GetModsInterface(Platform);
	printf("Mods interface=%p\n", (void*)Mods);

	// Copy before enumerate is documented NotFound.
	EOS_Mods_CopyModInfoOptions PreOpts;
	memset(&PreOpts, 0, sizeof(PreOpts));
	PreOpts.ApiVersion = EOS_MODS_COPYMODINFO_API_LATEST;
	PreOpts.Type = EOS_MET_INSTALLED;
	EOS_Mods_ModInfo* Pre = NULL;
	int preOk = (EOS_Mods_CopyModInfo(Mods, &PreOpts, &Pre) == EOS_NotFound && Pre == NULL);
	printf("Copy-before-enumerate NotFound=%d\n", preOk);

	// Enumerate installed mods.
	EOS_Mods_EnumerateModsOptions EnumOpts;
	memset(&EnumOpts, 0, sizeof(EnumOpts));
	EnumOpts.ApiVersion = EOS_MODS_ENUMERATEMODS_API_LATEST;
	EnumOpts.Type = EOS_MET_INSTALLED;
	EOS_Mods_EnumerateMods(Mods, &EnumOpts, NULL, &OnEnumerate);
	for (int i = 0; i < 50 && !g_EnumDone; ++i) { EOS_Platform_Tick(Platform); }

	int found1 = 0, found2 = 0;
	int baseOk = (CountInstalled(Mods, "config", &found1, &found2) == 2) && found1 && found2;

	// Uninstall mod 1 -> only mod 2 remains installed.
	EOS_Mod_Identifier Mod1;
	memset(&Mod1, 0, sizeof(Mod1));
	Mod1.ApiVersion = EOS_MOD_IDENTIFIER_API_LATEST;
	Mod1.NamespaceId = EXPECT_NAMESPACE;
	Mod1.ItemId = EXPECT_MOD1_ID;
	Mod1.Title = EXPECT_MOD1_TITLE;
	Mod1.Version = EXPECT_MOD1_VER;
	EOS_Mods_UninstallModOptions UnOpts;
	memset(&UnOpts, 0, sizeof(UnOpts));
	UnOpts.ApiVersion = EOS_MODS_UNINSTALLMOD_API_LATEST;
	UnOpts.Mod = &Mod1;
	EOS_Mods_UninstallMod(Mods, &UnOpts, NULL, &OnUninstall);
	for (int i = 0; i < 50 && !g_UninstallDone; ++i) { EOS_Platform_Tick(Platform); }
	int afterUninstallOk = (CountInstalled(Mods, "after-uninstall", &found1, NULL) == 1) && !found1;

	// Install a brand-new mod -> back to two installed.
	EOS_Mod_Identifier NewMod;
	memset(&NewMod, 0, sizeof(NewMod));
	NewMod.ApiVersion = EOS_MOD_IDENTIFIER_API_LATEST;
	NewMod.NamespaceId = EXPECT_NAMESPACE;
	NewMod.ItemId = "extra";
	NewMod.ArtifactId = "art_extra";
	NewMod.Title = "Extra Pack";
	NewMod.Version = "0.9";
	EOS_Mods_InstallModOptions InOpts;
	memset(&InOpts, 0, sizeof(InOpts));
	InOpts.ApiVersion = EOS_MODS_INSTALLMOD_API_LATEST;
	InOpts.Mod = &NewMod;
	InOpts.bRemoveAfterExit = EOS_FALSE;
	EOS_Mods_InstallMod(Mods, &InOpts, NULL, &OnInstall);
	for (int i = 0; i < 50 && !g_InstallDone; ++i) { EOS_Platform_Tick(Platform); }
	int afterInstallOk = (CountInstalled(Mods, "after-install", NULL, NULL) == 2);

	// Update of a mod that does not exist anywhere -> NotFound.
	EOS_Mod_Identifier Ghost;
	memset(&Ghost, 0, sizeof(Ghost));
	Ghost.ApiVersion = EOS_MOD_IDENTIFIER_API_LATEST;
	Ghost.ItemId = "no_such_mod";
	EOS_Mods_UpdateModOptions UpOpts;
	memset(&UpOpts, 0, sizeof(UpOpts));
	UpOpts.ApiVersion = EOS_MODS_UPDATEMOD_API_LATEST;
	UpOpts.Mod = &Ghost;
	EOS_Mods_UpdateMod(Mods, &UpOpts, NULL, &OnUpdateUnknown);
	for (int i = 0; i < 50 && !g_UpdateDone; ++i) { EOS_Platform_Tick(Platform); }

	EOS_Platform_Release(Platform);
	EOS_Shutdown();

	printf("pre=%d enum=%d base=%d uninstall=%d(%d) install=%d(%d) update=%d\n",
		preOk, g_EnumOk, baseOk, g_UninstallOk, afterUninstallOk, g_InstallOk, afterInstallOk, g_UpdateOk);
	int ok = preOk && g_EnumOk && baseOk && g_UninstallOk && afterUninstallOk
		&& g_InstallOk && afterInstallOk && g_UpdateOk;
	printf("MODS %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 3;
}
