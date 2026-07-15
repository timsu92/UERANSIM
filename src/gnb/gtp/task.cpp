//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "task.hpp"

#include <asn/ngap/ASN_NGAP_GBR-QosInformation.h>
#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestItem.h>
#include <lib/asn/utils.hpp>
#include <utils/agf_ipc.hpp>
#include <utils/common.hpp>

namespace nr::gnb
{

GtpTask::GtpTask(TaskBase *base)
    : m_base{base}, m_ueContexts{}, m_rateLimiter(std::make_unique<RateLimiter>()), m_pduSessions{}, m_sessionTree{}
{
    m_logger = m_base->logBase->makeUniqueLogger("gtp");
    m_agfIp = m_base->config->agfControlAppIp;
    m_agfPort = m_base->config->agfControlAppPort;
}

void GtpTask::onStart()
{
    // FORWARDER: no GTP-U socket. Open a fire-and-forget UDP sender to the AGF Control App.
    m_agfSender = std::make_unique<udp::UdpServer>();
    m_logger->info("AGF FORWARDER active, IPC target %s:%d", m_agfIp.c_str(), (int)m_agfPort);
}

void GtpTask::onQuit()
{
    m_agfSender.reset();
    m_ueContexts.clear();
}

void GtpTask::onLoop()
{
    auto msg = take();
    if (!msg)
        return;

    switch (msg->msgType)
    {
    case NtsMessageType::GNB_NGAP_TO_GTP: {
        auto &w = dynamic_cast<NmGnbNgapToGtp &>(*msg);
        switch (w.present)
        {
        case NmGnbNgapToGtp::UE_CONTEXT_UPDATE: {
            handleUeContextUpdate(*w.update);
            break;
        }
        case NmGnbNgapToGtp::UE_CONTEXT_RELEASE: {
            handleUeContextDelete(w.ueId);
            break;
        }
        case NmGnbNgapToGtp::SESSION_CREATE: {
            handleSessionCreate(w.resource);
            break;
        }
        case NmGnbNgapToGtp::SESSION_RELEASE: {
            handleSessionRelease(w.ueId, w.psi);
            break;
        }
        }
        break;
    }
    default:
        m_logger->unhandledNts(*msg);
        break;
    }
}

void GtpTask::sendToAgf(const std::string &json)
{
    if (!m_agfSender)
        return;
    InetAddress to(m_agfIp, m_agfPort);
    m_agfSender->Send(to, reinterpret_cast<const uint8_t *>(json.data()), json.size());
}

void GtpTask::handleUeContextUpdate(const GtpUeContextUpdate &msg)
{
    if (!m_ueContexts.count(msg.ueId))
        m_ueContexts[msg.ueId] = std::make_unique<GtpUeContext>(msg.ueId);

    auto &ue = m_ueContexts[msg.ueId];
    ue->ueAmbr = msg.ueAmbr;

    updateAmbrForUe(ue->ueId);
}

void GtpTask::handleSessionCreate(PduSessionResource *session)
{
    if (!m_ueContexts.count(session->ueId))
    {
        m_logger->err("PDU session resource could not be created, UE context with ID[%d] not found", session->ueId);
        return;
    }

    uint64_t sessionInd = MakeSessionResInd(session->ueId, session->psi);
    m_pduSessions[sessionInd] = std::unique_ptr<PduSessionResource>(session);

    m_sessionTree.insert(sessionInd, session->downTunnel.teid);

    updateAmbrForUe(session->ueId);
    updateAmbrForSession(sessionInd);

    // Extract QFI + GFBR/MFBR from the first QoS flow (PoC: single flow per session).
    int qfi = 0;
    uint64_t gfbrUl = 0, gfbrDl = 0, mfbrUl = 0, mfbrDl = 0;
    auto &sess = m_pduSessions[sessionInd];
    if (sess->qosFlows && sess->qosFlows->list.count > 0)
    {
        auto *item = sess->qosFlows->list.array[0];
        qfi = static_cast<int>(item->qosFlowIdentifier);
        auto *gbr = item->qosFlowLevelQosParameters.gBR_QosInformation;
        if (gbr != nullptr)
        {
            gfbrUl = asn::GetUnsigned64(gbr->guaranteedFlowBitRateUL);
            gfbrDl = asn::GetUnsigned64(gbr->guaranteedFlowBitRateDL);
            mfbrUl = asn::GetUnsigned64(gbr->maximumFlowBitRateUL);
            mfbrDl = asn::GetUnsigned64(gbr->maximumFlowBitRateDL);
        }
    }

    std::string upfN3Ip = utils::OctetStringToIp(sess->upTunnel.address);
    std::string json = agf::BuildSessionSetupJson(session->ueId, session->psi, upfN3Ip, sess->upTunnel.teid,
                                                   sess->downTunnel.teid, qfi, gfbrUl, gfbrDl, mfbrUl, mfbrDl);
    sendToAgf(json);
    m_logger->info("session_setup IPC sent for UE[%d] PSI[%d]", session->ueId, session->psi);
}

void GtpTask::handleSessionRelease(int ueId, int psi)
{
    if (!m_ueContexts.count(ueId))
    {
        m_logger->err("PDU session resource could not be released, UE context with ID[%d] not found", ueId);
        return;
    }

    uint64_t sessionInd = MakeSessionResInd(ueId, psi);

    // Remove all session information from rate limiter
    m_rateLimiter->updateSessionUplinkLimit(sessionInd, 0);
    m_rateLimiter->updateUeDownlinkLimit(ueId, 0);

    sendToAgf(agf::BuildSessionReleaseJson(ueId, psi));

    // And remove from PDU session table
    if (m_pduSessions.count(sessionInd))
    {
        uint32_t teid = m_pduSessions[sessionInd]->downTunnel.teid;
        m_pduSessions.erase(sessionInd);

        // And remove from the tree
        m_sessionTree.remove(sessionInd, teid);
    }
}

void GtpTask::handleUeContextDelete(int ueId)
{
    // Find PDU sessions of the UE
    std::vector<uint64_t> sessions{};
    m_sessionTree.enumerateByUe(ueId, sessions);

    for (auto &session : sessions)
    {
        sendToAgf(agf::BuildSessionReleaseJson(ueId, GetPsi(session)));

        // Remove all session information from rate limiter
        m_rateLimiter->updateSessionUplinkLimit(session, 0);
        m_rateLimiter->updateUeDownlinkLimit(ueId, 0);

        // And remove from PDU session table
        uint32_t teid = m_pduSessions[session]->downTunnel.teid;
        m_pduSessions.erase(session);

        // And remove from the tree
        m_sessionTree.remove(session, teid);
    }

    // Remove all user information from rate limiter
    m_rateLimiter->updateUeUplinkLimit(ueId, 0);
    m_rateLimiter->updateUeDownlinkLimit(ueId, 0);

    // Remove UE context
    m_ueContexts.erase(ueId);
}

void GtpTask::updateAmbrForUe(int ueId)
{
    if (!m_ueContexts.count(ueId))
        return;

    auto &ue = m_ueContexts[ueId];
    m_rateLimiter->updateUeUplinkLimit(ueId, ue->ueAmbr.ulAmbr);
    m_rateLimiter->updateUeDownlinkLimit(ueId, ue->ueAmbr.dlAmbr);
}

void GtpTask::updateAmbrForSession(uint64_t pduSession)
{
    if (!m_pduSessions.count(pduSession))
        return;

    auto &sess = m_pduSessions[pduSession];
    m_rateLimiter->updateSessionUplinkLimit(pduSession, sess->sessionAmbr.ulAmbr);
    m_rateLimiter->updateSessionDownlinkLimit(pduSession, sess->sessionAmbr.dlAmbr);
}

} // namespace nr::gnb
