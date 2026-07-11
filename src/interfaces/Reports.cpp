//
// Reports interface -- player behaviour reports, accepted locally.
//
// There is no moderation backend on the LAN, so a report cannot go anywhere;
// but a stubbed EOS_NotImplemented completion makes a game's "report player"
// button visibly fail. Instead the report is validated, logged at Warning
// (so it shows up in any log sink), appended to <CacheDirectory>/
// eosemu_reports.log when a cache directory exists, and completed with
// Success. See HANDOFF.md, "P0 -- Reports".
//
// Follows the Sanctions pattern: the handle is a platform sentinel and is
// ignored; shared services come from CurrentPlatform().
//

#include "interfaces/Common.h"

#include "core/Ids.h"
#include "core/Logging.h"
#include "core/Platform.h"

#include "eos_reports.h"

#include <cstring>
#include <fstream>
#include <string>

namespace EOSEmu
{
	namespace
	{
		const char* CategoryName(EOS_EPlayerReportsCategory Category)
		{
			switch (Category)
			{
			case EOS_EPlayerReportsCategory::EOS_PRC_Cheating: return "Cheating";
			case EOS_EPlayerReportsCategory::EOS_PRC_Exploiting: return "Exploiting";
			case EOS_EPlayerReportsCategory::EOS_PRC_OffensiveProfile: return "OffensiveProfile";
			case EOS_EPlayerReportsCategory::EOS_PRC_VerbalAbuse: return "VerbalAbuse";
			case EOS_EPlayerReportsCategory::EOS_PRC_Scamming: return "Scamming";
			case EOS_EPlayerReportsCategory::EOS_PRC_Spamming: return "Spamming";
			case EOS_EPlayerReportsCategory::EOS_PRC_Other: return "Other";
			default: return "Invalid";
			}
		}

		void AppendToReportLog(const std::string& Line)
		{
			Platform* P = CurrentPlatform();
			if (P == nullptr || P->CacheDirectory().empty()) return;
			std::string Path = P->CacheDirectory();
			if (Path.back() != '\\' && Path.back() != '/') Path += '/';
			Path += "eosemu_reports.log";
			std::ofstream Out(Path, std::ios::binary | std::ios::app);
			if (Out)
			{
				Out << Line << '\n';
			}
		}
	}
}

using namespace EOSEmu;

EOS_DECLARE_FUNC(void) EOS_Reports_SendPlayerBehaviorReport(EOS_HReports Handle, const EOS_Reports_SendPlayerBehaviorReportOptions* Options, void* ClientData, const EOS_Reports_OnSendPlayerBehaviorReportCompleteCallback CompletionDelegate)
{
	EOSEMU_API_TRACE();
	(void)Handle;
	if (CompletionDelegate == nullptr) return;
	Platform* P = CurrentPlatform();
	if (P == nullptr) return;

	auto Complete = [&](EOS_EResult Result)
	{
		P->Dispatch().Post([CompletionDelegate, ClientData, Result]
		{
			EOS_Reports_SendPlayerBehaviorReportCompleteCallbackInfo Info = {};
			Info.ResultCode = Result;
			Info.ClientData = ClientData;
			CompletionDelegate(&Info);
		});
	};

	if (Options == nullptr || Options->ReporterUserId == nullptr || Options->ReportedUserId == nullptr
		|| Options->Category == EOS_EPlayerReportsCategory::EOS_PRC_Invalid)
	{
		Complete(EOS_EResult::EOS_InvalidParameters);
		return;
	}
	if (!VersionInRange(Options->ApiVersion, EOS_REPORTS_SENDPLAYERBEHAVIORREPORT_API_LATEST))
	{
		Complete(EOS_EResult::EOS_IncompatibleVersion);
		return;
	}
	// The context must fit the documented cap ("report will fail otherwise");
	// an over-long message is merely truncated, per the same header.
	if (Options->Context != nullptr && std::strlen(Options->Context) > EOS_REPORTS_REPORTCONTEXT_MAX_LENGTH)
	{
		Complete(EOS_EResult::EOS_InvalidParameters);
		return;
	}

	std::string Message = Options->Message ? Options->Message : "";
	if (Message.size() > EOS_REPORTS_REPORTMESSAGE_MAX_LENGTH)
	{
		Message.resize(EOS_REPORTS_REPORTMESSAGE_MAX_LENGTH);
	}

	const std::string& Reporter = Ids::ProductString(Options->ReporterUserId);
	const std::string& Reported = Ids::ProductString(Options->ReportedUserId);
	EOSEMU_WARN(Reports, "player report: %s reported %s for %s%s%s (accepted locally; no backend)",
		Reporter.c_str(), Reported.c_str(), CategoryName(Options->Category),
		Message.empty() ? "" : ": ", Message.c_str());

	AppendToReportLog("reporter=" + Reporter + " reported=" + Reported
		+ " category=" + CategoryName(Options->Category)
		+ (Message.empty() ? "" : " message=" + Message)
		+ (Options->Context ? std::string(" context=") + Options->Context : ""));

	Complete(EOS_EResult::EOS_Success);
}
