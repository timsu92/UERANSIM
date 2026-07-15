//
// AGF control-plane IPC message builders (UDP+JSON to the AGF Control App).
// Pure functions of scalar data — see docs/superpowers/specs §2 for the contract.
//

#pragma once

#include <cstdint>
#include <string>

namespace agf
{

// gNB/N2 module → App. Bit rates in bits/s as received over NGAP (0 for non-GBR).
std::string BuildSessionSetupJson(int ranUeId, int psi, const std::string &upfN3Ip, uint32_t ulTeid, uint32_t dlTeid,
                                  int qfi, uint64_t gfbrUl, uint64_t gfbrDl, uint64_t mfbrUl, uint64_t mfbrDl);

// gNB → App.
std::string BuildSessionReleaseJson(int ranUeId, int psi);

// UE/Proxy UE → App. AMBR in bits/s.
std::string BuildSessionEstablishedJson(const std::string &supi, int psi, const std::string &ueIp, uint64_t ambrUl,
                                        uint64_t ambrDl);

} // namespace agf
