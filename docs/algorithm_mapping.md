# Algorithm Mapping

## EPS-WECMP Telemetry Semantics

The current EPS-WECMP implementation is a control-plane estimate, not a closed-loop
ns-3 measured telemetry implementation.

Current data flow:

1. OCS admission computes planned and real residual demand for matrix flows.
2. EPS-WECMP uses `wecmpResidualDemand` as the residual input.
3. `accumulateEpsResidualTraffic` distributes residual demand over candidate spines
   according to the current WECMP probabilities.
4. `EpsPhysicalLinkState::observedTraffic` stores that estimated residual load.
5. `utilization` is computed as estimated residual load divided by
   `epsWecmpCapacity`.
6. `smoothedUtilization` is updated by EWMA.
7. WECMP probabilities are updated from path attractiveness derived from the
   smoothed utilization.

This means `observedTraffic` should be read as control-plane estimated residual
load. It should not be described as ns-3 measured per-link byte count or measured
link utilization.

## Paper Mapping

The paper-level algorithm can describe the current implementation as using
estimated EPS link load from the controller's residual-demand model. If the paper
requires measured link utilization, the implementation needs a new telemetry
module.

Required work for measured utilization:

- attach per leaf-spine link telemetry identities to ns-3 devices;
- collect per-link bytes over a defined measurement window;
- convert bytes to utilization using the actual link data rate and window length;
- schedule WECMP updates after the telemetry window closes;
- define how feedback delay affects newly installed and already frozen flows.

That change would alter the experiment boundary and should not be treated as a
documentation-only or helper-only refactor.

## Residual Demand Semantics

Single-period matrix flows currently distinguish:

- `plannedResidualDemand`: matrix-based residual after planned OCS capacity;
- `realResidualDemand`: per-flow admission outcome, either zero or
  `matrixFlowDemand`;
- `wecmpResidualDemand`: the WECMP control input, computed as the maximum of the
  planned and real residual demand.

Multi-period WECMP currently has no data-plane admission or measured feedback
loop. Its `totalRealResidualDemand` field is a planned-residual placeholder and
should not be interpreted as observed residual traffic.
