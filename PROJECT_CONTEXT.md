# Hybrid Optical-Electrical DCN ns-3 Simulation Context

## Thesis Title

基于图聚类与跨尺度协同的光电混合数据中心网络路由调度机制

## Directory Convention

ns-3 engine root:

    ~/ns-3.47

Thesis simulation project root:

    ~/sim

The ns-3 source tree is used as the simulation engine. The thesis project is maintained in ~/sim.

Only one symbolic scratch entry is created inside ns-3:

    ~/ns-3.47/scratch/hybrid-dcn-main.cc

The scratch entry points to:

    ~/sim/src/main/hybrid-dcn-main.cc

## Simulation Target

This project builds an ns-3 based simulation platform for a hybrid optical-electrical data center network.

The simulated architecture contains:

- EPS: electrical packet switching substrate for baseline connectivity and residual traffic forwarding.
- OCS: optical circuit switching substrate for high-capacity dynamic direct links.
- Controller: cross-scale scheduling logic that updates OCS lightpaths at a slow time scale and EPS-WECMP weights at a faster time scale.

## Core Scheduling Pipeline

1. Build directed traffic matrix W(t).
2. Convert W(t) into undirected communication intensity A(t).
3. Apply EWMA smoothing and obtain A_bar(t).
4. Build weighted traffic graph G_f(t).
5. Compute modularity matrix B(t).
6. Run Louvain community detection.
7. Select OCS lightpaths under optical port constraints.
8. Admit new flows to OCS when lightpath and capacity conditions hold.
9. Route residual traffic through EPS.
10. Update EPS-WECMP weights according to measured link utilization.


