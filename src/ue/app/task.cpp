//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"
#include "cmd_handler.hpp"
#include <cctype>
#include <lib/nas/utils.hpp>
#include <lib/udp/server.hpp>
#include <ue/nas/task.hpp>
#include <ue/rls/task.hpp>
#include <unistd.h>
#include <utils/agf_ipc.hpp>
#include <utils/common.hpp>
#include <utils/constants.hpp>
#include <utils/network.hpp>

static constexpr const int SWITCH_OFF_TIMER_ID = 1;
static constexpr const int SWITCH_OFF_DELAY = 500;

// Decode a NAS SessionAMBR IE (raw 2-octet value + unit multiplier) into bits/s.
// Mirrors the unit-decoding formula used by nas::ToJson(const IESessionAmbr &) in
// lib/nas/ie4.cpp, but returns a machine-usable uint64_t instead of a display string.
static uint64_t SessionAmbrToBps(nas::EUnitForSessionAmbr unit, const octet2 &rawValue)
{
    int unitValue = static_cast<int>(unit);
    if (unitValue <= 0)
        return 0;

    auto value = static_cast<uint64_t>(static_cast<int>(rawValue));
    uint64_t factor = 1ull << (2 * ((unitValue - 1) % 5));
    int magnitude = (unitValue - 1) / 5; // 0=Kb/s, 1=Mb/s, 2=Gb/s, 3=Tb/s, 4=Pb/s

    uint64_t bps = value * factor * 1000ull;
    for (int i = 0; i < magnitude; i++)
        bps *= 1000ull;
    return bps;
}

namespace nr::ue
{

UeAppTask::UeAppTask(TaskBase *base) : m_base{base}
{
    m_logger = m_base->logBase->makeUniqueLogger(m_base->config->getLoggerPrefix() + "app");
}

void UeAppTask::onStart()
{
    m_agfIp = m_base->config->agfControlAppIp;
    m_agfPort = m_base->config->agfControlAppPort;
    m_agfSender = std::make_unique<udp::UdpServer>();
}

void UeAppTask::onQuit()
{
    m_agfSender.reset();
}

void UeAppTask::onLoop()
{
    auto msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::UE_NAS_TO_APP: {
        auto &w = dynamic_cast<NmUeNasToApp &>(*msg);
        switch (w.present)
        {
        case NmUeNasToApp::PERFORM_SWITCH_OFF: {
            setTimer(SWITCH_OFF_TIMER_ID, SWITCH_OFF_DELAY);
            break;
        }
        }
        break;
    }
    case NtsMessageType::UE_STATUS_UPDATE: {
        receiveStatusUpdate(dynamic_cast<NmUeStatusUpdate &>(*msg));
        break;
    }
    case NtsMessageType::UE_CLI_COMMAND: {
        auto &w = dynamic_cast<NmUeCliCommand &>(*msg);
        UeCmdHandler handler{m_base};
        handler.handleCmd(w);
        break;
    }
    case NtsMessageType::TIMER_EXPIRED: {
        auto &w = dynamic_cast<NmTimerExpired &>(*msg);
        if (w.timerId == SWITCH_OFF_TIMER_ID)
        {
            m_logger->info("UE device is switching off");
            m_base->ueController->performSwitchOff(m_base->ue);
        }
        break;
    }
    default:
        m_logger->unhandledNts(*msg);
        break;
    }
}

void UeAppTask::receiveStatusUpdate(NmUeStatusUpdate &msg)
{
    if (msg.what == NmUeStatusUpdate::SESSION_ESTABLISHMENT)
    {
        notifyControlApp(msg.pduSession);
        return;
    }

    if (msg.what == NmUeStatusUpdate::SESSION_RELEASE)
    {
        // No TUN task bookkeeping to release anymore. The gNB emits the authoritative
        // session_release IPC to the App; the App keys teardown off that message.
        return;
    }

    if (msg.what == NmUeStatusUpdate::CM_STATE)
    {
        m_cmState = msg.cmState;
        return;
    }
}

void UeAppTask::notifyControlApp(const PduSession *pduSession)
{
    if (pduSession == nullptr || !pduSession->pduAddress.has_value())
    {
        m_logger->err("Cannot notify AGF: PDU address missing");
        return;
    }

    std::string supi = m_base->config->supi.has_value() ? m_base->config->supi->value : m_base->config->getNodeName();
    std::string ueIp = utils::OctetStringToIp(pduSession->pduAddress->pduAddressInformation);

    uint64_t ambrUl = 0, ambrDl = 0;
    if (pduSession->sessionAmbr.has_value())
    {
        ambrUl = SessionAmbrToBps(pduSession->sessionAmbr->unitForSessionAmbrForUplink,
                                  pduSession->sessionAmbr->sessionAmbrForUplink);
        ambrDl = SessionAmbrToBps(pduSession->sessionAmbr->unitForSessionAmbrForDownlink,
                                  pduSession->sessionAmbr->sessionAmbrForDownlink);
    }

    std::string json = agf::BuildSessionEstablishedJson(supi, pduSession->psi, ueIp, ambrUl, ambrDl);

    if (m_agfSender)
        m_agfSender->Send(InetAddress(m_agfIp, m_agfPort), reinterpret_cast<const uint8_t *>(json.data()), json.size());
    m_logger->info("session_established IPC sent for PSI[%d], UE IP %s", pduSession->psi, ueIp.c_str());
}

} // namespace nr::ue
