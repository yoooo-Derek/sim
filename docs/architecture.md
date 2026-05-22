# Architecture

## Current Boundary

The project remains an ns-3 scratch-compatible simulation. The executable entry is still `src/main/hybrid-dcn-main.cc`, and `~/ns-3.47/scratch/hybrid-dcn-main.cc` should remain a symbolic link to that file.

The first refactor phase moved pure data structures and pure algorithm helpers out of `main.cc`. The follow-up algorithm-alignment pass added the explicit paper traffic pipeline `W(t) -> A(t) -> G_f(t) -> A_bar(t)` while preserving default synthetic matrix values. It did not rewrite Louvain logic, OCS scheduling behavior, OCS admission behavior, EPS-WECMP behavior, ns-3 topology construction, routing installation, flow installation, result semantics, or experiment presets.

## `main.cc`

`src/main/hybrid-dcn-main.cc` still owns the simulation workflow:

- command-line argument parsing
- preset application and validation
- orchestration of traffic matrix, Louvain, OCS selection, config gate, hold-time gate, admission, and EPS-WECMP steps
- ns-3 node, device, address, route, and application installation
- NetAnim output setup
- result, validation, export, and invariant log orchestration

It is intentionally kept as the scratch entry so the existing ns-3 run path remains stable.

## Traffic

`src/traffic/traffic-matrix.h` owns the first extracted traffic helpers:

- `WeightedMatrix`
- synthetic directed ToR-level traffic matrix `W(t)` generation
- conversion from directed `W(t)` to undirected communication intensity `A(t)`
- traffic graph sparsification using `trafficGraphThreshold` / `theta_f`
- EWMA matrix update
- node degree calculation
- total traffic calculation

It does not define experiment scenarios or formal traffic workloads.

## Louvain

`src/model/louvain.h` owns the first extracted graph clustering helpers:

- `CommunityPreview`
- `LouvainLevelSummary`
- `LouvainResult`
- community preview labels
- modularity Q calculation
- local moving
- graph coarsening
- single-level Louvain
- multi-level Louvain

The implementation is a direct lift from the previous `main.cc` logic.

## OCS

`src/ocs/ocs-state.h` owns OCS candidate and edge-age state helpers:

- `OcsCandidateEdge`
- `OcsEdgeAgeMatrix`
- edge normalization
- zero age matrix construction
- edge age get/set
- selected-edge age update
- edge age range calculation

OCS candidate scoring, greedy port-constrained selection, config-gate decisions, hold-time gate decisions, lightpath installation, and OCS admission are still orchestrated in `main.cc`.

## EPS

`src/eps/eps-wecmp-state.h` currently contains EPS-WECMP data structures:

- `EpsWecmpLinkState`
- `EpsWecmpDecision`
- `EpsWecmpPairState`
- `EpsPhysicalLinkState`
- `EpsWecmpEpochSummary`

The EPS-WECMP update algorithm and route binding logic remain in `main.cc`.

Current telemetry semantics:

- `observedTraffic` in EPS-WECMP state means control-plane estimated residual load.
- It is computed from residual demand and current WECMP probabilities.
- It is not ns-3 measured per-link byte or utilization telemetry.
- `utilization` is computed as estimated residual load divided by `epsWecmpCapacity`.
- Multi-period WECMP summaries currently copy planned residual demand into the real-residual field as a placeholder; they are not data-plane observed residual measurements.

Current admission fallback semantics:

- If OCS admission rejects a selected OCS pair, `main.cc` creates a same-pair EPS fallback matrix flow.
- That fallback flow is marked as requiring an EPS residual path and is eligible for EPS-WECMP decision and route binding.
- Synthetic residual flows remain supplemental coverage for residual-path validation. They do not stand in for an admission-rejected selected OCS pair.

## Result

`src/result/` is reserved for later extraction of result summaries, CSV export, validation, and invariant checks. These are still in `main.cc` in the current phase.

## Helper

`src/helper/` is reserved for later ns-3 helper code such as topology construction, route installation, and application installation. These remain in `main.cc` because they are coupled to ns-3 objects and are higher risk to move.

## Controller

`src/controller/` is reserved for later extraction of controller orchestration, config-gate scoring, hold-time gate handling, and admission-control decisions. The current phase did not split this logic because it shares substantial state with the scratch entry.

## Still In `main.cc`

The following logic intentionally remains in `main.cc` after this first low-risk pass:

- command-line defaults and preset mutation
- OCS candidate construction and sorting
- OCS port-constrained greedy selection
- config score and reconfiguration penalty logic
- hold-time gate orchestration
- OCS admission control
- EPS-WECMP probability update behavior
- EPS-WECMP utilization telemetry, which currently uses control-plane residual demand rather than ns-3 measured link utilization
- ns-3 topology, route, and application installation
- structured result export
- result validation and invariant checks

## Follow-Up Plan

Recommended next refactor steps:

1. Move config and preset data into an `ExperimentConfig` header without changing defaults.
2. Move OCS candidate scoring and config-gate helpers into `src/controller/`.
3. Move EPS-WECMP update helpers into `src/eps/` while preserving current state updates and log fields.
4. Add an EPS telemetry module only after the measurement window, event schedule, and per-link utilization definition are fixed.
5. Move result export and invariant checks into `src/result/`.
6. Move ns-3 topology and routing installation into `src/helper/` only after algorithm-level modules are stable.
