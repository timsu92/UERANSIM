//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include "utils.hpp"

#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <gnb/nts.hpp>
#include <lib/udp/server.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>

namespace nr::gnb
{

class GtpTask : public NtsTask
{
  private:
    TaskBase *m_base;
    std::unique_ptr<Logger> m_logger;

    std::unique_ptr<udp::UdpServer> m_agfSender;
    std::string m_agfIp;
    uint16_t m_agfPort{};
    std::unordered_map<int, std::unique_ptr<GtpUeContext>> m_ueContexts;
    std::unique_ptr<IRateLimiter> m_rateLimiter;
    std::unordered_map<uint64_t, std::unique_ptr<PduSessionResource>> m_pduSessions;
    PduSessionTree m_sessionTree;

    friend class GnbCmdHandler;

  public:
    explicit GtpTask(TaskBase *base);
    ~GtpTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private:
    void sendToAgf(const std::string &json);
    void handleUeContextUpdate(const GtpUeContextUpdate &msg);
    void handleSessionCreate(PduSessionResource *session);
    void handleSessionRelease(int ueId, int psi);
    void handleUeContextDelete(int ueId);

    void updateAmbrForUe(int ueId);
    void updateAmbrForSession(uint64_t pduSession);
};

} // namespace nr::gnb
