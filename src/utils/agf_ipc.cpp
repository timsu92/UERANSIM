//
// AGF control-plane IPC message builders.
//

#include "agf_ipc.hpp"

#include <utils/json.hpp>

namespace agf
{

std::string BuildSessionSetupJson(int ranUeId, int psi, const std::string &upfN3Ip, uint32_t ulTeid, uint32_t dlTeid,
                                  int qfi, uint64_t gfbrUl, uint64_t gfbrDl, uint64_t mfbrUl, uint64_t mfbrDl)
{
    Json j = Json::Obj({
        {"type", "session_setup"},
        {"ran_ue_id", ranUeId},
        {"psi", psi},
        {"upf_n3_ip", upfN3Ip},
        {"ul_teid", static_cast<int64_t>(ulTeid)},
        {"dl_teid", static_cast<int64_t>(dlTeid)},
        {"qfi", qfi},
        {"gfbr_ul", static_cast<int64_t>(gfbrUl)},
        {"gfbr_dl", static_cast<int64_t>(gfbrDl)},
        {"mfbr_ul", static_cast<int64_t>(mfbrUl)},
        {"mfbr_dl", static_cast<int64_t>(mfbrDl)},
    });
    return j.dumpJson();
}

std::string BuildSessionReleaseJson(int ranUeId, int psi)
{
    Json j = Json::Obj({
        {"type", "session_release"},
        {"ran_ue_id", ranUeId},
        {"psi", psi},
    });
    return j.dumpJson();
}

std::string BuildSessionEstablishedJson(const std::string &supi, int psi, const std::string &ueIp, uint64_t ambrUl,
                                        uint64_t ambrDl)
{
    Json j = Json::Obj({
        {"type", "session_established"},
        {"supi", supi},
        {"psi", psi},
        {"ue_ip", ueIp},
        {"session_ambr_ul", static_cast<int64_t>(ambrUl)},
        {"session_ambr_dl", static_cast<int64_t>(ambrDl)},
    });
    return j.dumpJson();
}

} // namespace agf
