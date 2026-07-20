# AGF Fork — Changes from upstream UERANSIM v3.3.0

This fork (branch `agf`) turns `nr-gnb`/`nr-ue` into AGF control-plane-only
binaries: full NGAP/NAS signaling, zero user-plane data (no GTP-U tunnel
endpoint in the gNB, no TUN interface in the UE). Both binaries instead
report session lifecycle events over UDP+JSON to an external "AGF Control
App", which owns the actual user-plane (OVS/OpenFlow). See the superproject
(`timsu92/agf`) `docs/superpowers/specs/2026-07-14-agf-ovs-architecture-design.md`
§2 for the full IPC contract this implements.

## New: shared IPC module

- `src/utils/agf_ipc.hpp` / `.cpp` — three pure JSON builders
  (`BuildSessionSetupJson`, `BuildSessionReleaseJson`,
  `BuildSessionEstablishedJson`), unit-tested in `tests/agf_ipc_test.cpp`
  (target `agf-ipc-test`, wired in the top-level `CMakeLists.txt`). No
  dependency beyond the bundled `Json` type.

## gNB (`nr-gnb`) — N2 module

- `src/gnb/types.hpp` — `GnbConfig` gained `agfControlAppIp`/`agfControlAppPort`
  (default `127.0.0.1:9999`), parsed from the `agfControlApp` YAML block in
  `src/gnb.cpp`.
- `src/gnb/gtp/task.hpp` / `task.cpp` — `GtpTask` repurposed from a GTP-U
  tunnel endpoint into an IPC **FORWARDER**:
  - **Removed:** the UDP:2152 `udp::UdpServerTask` (no GTP-U socket is bound
    at all — `onStart` only opens a fire-and-forget `udp::UdpServer` sender
    toward the AGF Control App). `handleUplinkData` and `handleUdpReceive`
    (which also answered GTP Echo) are deleted — there is no uplink user
    data and no GTP-U socket to receive echoes on.
  - **Changed:** `handleSessionCreate` now parses QFI and, if present,
    `ASN_NGAP_GBR_QosInformation` (`guaranteedFlowBitRateUL/DL`,
    `maximumFlowBitRateUL/DL`), and sends `session_setup` JSON (UPF N3 IP,
    UL/DL TEID, QFI, GFBR/MFBR) instead of programming a tunnel.
    `handleSessionRelease` and `handleUeContextDelete` now send
    `session_release` JSON in addition to their existing bookkeeping
    cleanup.
  - **Fixed (2026-07-20):** free5GC's SMF returns *two*
    `QosFlowSetupRequestItem`s for a GBR PDU session — a first one (lowest
    QFI) with the requested 5QI but no `gBR_QosInformation` IE, and a
    second one that carries the real GFBR/MFBR values. The original "PoC:
    single flow per session" code always took `array[0]`, so every GBR
    session silently degraded to the AMBR/link ceiling instead of its
    guaranteed rate. `handleSessionCreate` now scans all flows and prefers
    whichever one has `gBR_QosInformation != nullptr`, falling back to
    `array[0]` (old behavior) when none do — the normal non-GBR case.
    Found while running the AGF superproject's Plan 6 Task 8 QoS smoke
    test end-to-end against a real free5GC GBR subscriber. This is a
    heuristic scoped to sessions with at most one dedicated GBR flow — it
    does not generalize to 3GPP's default+multiple-dedicated-flow QoS
    model, since this IPC message still carries only one qfi/rate tuple
    per session regardless of flow count (see superproject spec §19 N2).
  - **Kept, still load-bearing:** `handleSessionCreate` still calls
    `m_sessionTree.insert(...)` and populates `m_pduSessions[...]`, and
    `handleUeContextUpdate`/`handleSessionCreate` still call
    `updateAmbrForUe`/`updateAmbrForSession`. This is *not* dead code — the
    session bookkeeping (`m_sessionTree`, `m_pduSessions`) is what
    `handleUeContextDelete` walks (`m_sessionTree.enumerateByUe`) to know
    which PDU sessions of a UE to emit `session_release` IPC for when the UE
    context is torn down. Removing it would break UE-context-release
    cleanup.
  - **Deliberately kept, now dead:** `m_rateLimiter`
    (`std::unique_ptr<IRateLimiter>`) and the `updateAmbrForUe`/
    `updateAmbrForSession` calls *into it* (i.e. the rate-limiter side
    effect, not the bookkeeping around it) have no packets to rate-limit —
    the gNB never sees user-plane traffic anymore. Left in place rather than
    removed to avoid touching `handleUeContextUpdate` and
    `handleSessionCreate`'s bookkeeping paths for a change with zero
    behavioral benefit. **Known removable dead code** — a future cleanup can
    delete `RateLimiter`/`m_rateLimiter` construction, the
    `updateAmbrForUe`/`updateAmbrForSession` functions themselves, and their
    call sites, with no functional impact (the bookkeeping calls described
    above would stay).
  - NGAP itself (`src/gnb/ngap/session.cpp`) is untouched: it still parses
    the UL tunnel info and QoS flows into `PduSessionResource` and pushes
    `SESSION_CREATE`/`SESSION_RELEASE` to `GtpTask` exactly as upstream —
    only what `GtpTask` *does* with that message changed.
  - **Operational note (not a code change):** NGAP writes
    `resource->downTunnel.address = gtpAdvertiseIp.value_or(gtpIp)` into the
    NGAP Setup Response — this is the downlink GTP endpoint the core will
    target. In the AGF, that endpoint must be the AGF/OVS N3 IP, not the
    gNB's own IP — set `gtpAdvertiseIp` in the gNB config accordingly.

## UE (`nr-ue`) — Proxy UE

- `src/ue/types.hpp` — `UeConfig` gained `agfControlAppIp`/
  `agfControlAppPort` (default `127.0.0.1:9999`), parsed from the
  `agfControlApp` YAML block in `src/ue.cpp`.
- `src/ue/app/task.hpp` / `task.cpp` — `UeAppTask`:
  - **Removed:** `setupTunInterface` and all TUN task wiring
    (`UE_TUN_TO_APP` case, `DOWNLINK_DATA_DELIVERY` case body, the TUN task
    array). `nr-ue` no longer requires root.
  - **Changed:** `receiveStatusUpdate`'s `SESSION_ESTABLISHMENT` branch now
    calls `notifyControlApp`, which sends `session_established` JSON (SUPI,
    PSI, UE IP, Session-AMBR UL/DL in bits/s) instead of allocating a TUN
    device. The `SESSION_RELEASE` branch is now a no-op comment — there is
    no TUN task bookkeeping to release, and the gNB (not the UE) emits the
    authoritative `session_release` IPC on teardown.
  - **AMBR decode helper:** on the UE side, `PduSession::sessionAmbr` is
    `std::optional<nas::IESessionAmbr>` — the compact 3GPP unit+value NAS
    encoding (`unitForSessionAmbrFor{Up,Down}link` +
    `sessionAmbrFor{Up,Down}link`), not a plain bits/s pair. Since the IPC
    JSON needs a plain `uint64_t` bits/s value, `task.cpp` adds a static
    helper `SessionAmbrToBps(nas::EUnitForSessionAmbr unit, const octet2
    &rawValue)` that decodes one AMBR field to bits/s, mirroring the same
    unit-scaling formula used by `nas::ToJson(const IESessionAmbr &)` in
    `src/lib/nas/ie4.cpp` (kept as a display-string formatter there; the new
    helper returns a machine-usable integer instead). `notifyControlApp`
    calls it once per direction before building the JSON.
  - **Deliberately kept, now dead:** `src/ue/tun/` (`tun.hpp/.cpp`,
    `task.hpp/.cpp`) is still compiled into the `ue` library
    (`file(GLOB_RECURSE ...)` in `src/ue/CMakeLists.txt`) but is never called
    after this change. Left in the tree rather than deleted to avoid
    touching build-glob boundaries for a change with no runtime effect.
    **Known removable dead code** — a future cleanup can delete the
    directory and confirm the `ue` library still links (nothing else
    references `TunTask`/`TunAllocate`/`TunConfigure`).

## New: AGF sample configs

- `config/agf-gnb.yaml` / `config/agf-ue.yaml` — derived from the shipped
  `config/free5gc-{gnb,ue}.yaml`, adding the `agfControlApp` block (and,
  for the gNB, `gtpAdvertiseIp`). Used by `scripts/smoke-ueransim.sh` in the
  superproject.

## What did NOT change

- NAS state machine, RRC, RLS/SCTP transport, authentication (AKA) — all
  upstream behavior, unmodified.
- The UE never sees TEIDs; the gNB never sees the UE's IP — that join still
  has to happen in the external AGF Control App (spec §2 "Join 機制").
