#include <cassert>
#include <cstdio>
#include <string>
#include <utils/agf_ipc.hpp>

static bool contains(const std::string &hay, const std::string &needle)
{
    return hay.find(needle) != std::string::npos;
}

int main()
{
    // session_setup: all identifying fields present, bit rates as bits/s (no ÷8 here)
    std::string s = agf::BuildSessionSetupJson(3, 1, "10.100.200.3", 305419896u, 1u, 1, 50000000ull, 100000000ull,
                                               100000000ull, 200000000ull);
    assert(contains(s, "\"type\":\"session_setup\""));
    assert(contains(s, "\"ran_ue_id\":3"));
    assert(contains(s, "\"psi\":1"));
    assert(contains(s, "\"upf_n3_ip\":\"10.100.200.3\""));
    assert(contains(s, "\"ul_teid\":305419896"));
    assert(contains(s, "\"dl_teid\":1"));
    assert(contains(s, "\"qfi\":1"));
    assert(contains(s, "\"gfbr_ul\":50000000"));
    assert(contains(s, "\"gfbr_dl\":100000000"));
    assert(contains(s, "\"mfbr_ul\":100000000"));
    assert(contains(s, "\"mfbr_dl\":200000000"));

    // session_release
    std::string r = agf::BuildSessionReleaseJson(3, 1);
    assert(contains(r, "\"type\":\"session_release\""));
    assert(contains(r, "\"ran_ue_id\":3"));
    assert(contains(r, "\"psi\":1"));

    // session_established
    std::string e = agf::BuildSessionEstablishedJson("imsi-208930291840533", 1, "10.60.0.5", 200000000ull, 500000000ull);
    assert(contains(e, "\"type\":\"session_established\""));
    assert(contains(e, "\"supi\":\"imsi-208930291840533\""));
    assert(contains(e, "\"psi\":1"));
    assert(contains(e, "\"ue_ip\":\"10.60.0.5\""));
    assert(contains(e, "\"session_ambr_ul\":200000000"));
    assert(contains(e, "\"session_ambr_dl\":500000000"));

    std::printf("agf_ipc_test: PASS\n");
    return 0;
}
