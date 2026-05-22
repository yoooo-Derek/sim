# Hybrid DCN ns-3 Simulation

This repository contains the thesis simulation project for:

基于图聚类与跨尺度协同的光电混合数据中心网络路由调度机制

## Workspace Layout

    ~/ns-3.47
        ns-3.47 source and build directory

    ~/sim
        thesis simulation project directory

## Current Project Layout

    src/
        main/
            hybrid-dcn-main.cc
        traffic/
            traffic-matrix.h
        model/
            louvain.h
        ocs/
            ocs-state.h
        eps/
            eps-wecmp-state.h
        controller/
            .gitkeep
        result/
            .gitkeep
        helper/
            .gitkeep
        utils/

    experiments/
        configs/
        runs/

    scripts/

    results/
        raw/
        figures/
        tables/

    docs/
        architecture.md

    build-meta/

## ns-3 Entry

The ns-3 scratch entry remains:

    ~/ns-3.47/scratch/hybrid-dcn-main.cc

It is a symbolic link to:

    ~/sim/src/main/hybrid-dcn-main.cc

The scratch-compatible entry is preserved so the existing run command continues to work.

## Current Module Responsibilities

- `src/main/hybrid-dcn-main.cc`: ns-3 scratch entry, command-line parsing, preset application, topology construction, routing installation, application installation, result logging, validation, and invariant orchestration.
- `src/traffic/traffic-matrix.h`: `WeightedMatrix`, synthetic undirected traffic matrix generation, node degree, total traffic, and EWMA update helpers.
- `src/model/louvain.h`: community preview, modularity Q, local moving, graph coarsening, single-level Louvain, and multi-level Louvain helpers.
- `src/ocs/ocs-state.h`: OCS candidate edge data and OCS edge age/state helper functions.
- `src/eps/eps-wecmp-state.h`: EPS-WECMP data structures only.
- `src/controller/`, `src/result/`, and `src/helper/`: reserved module locations for later refactoring.

## Minimal Run Command

Run from the ns-3 root:

    cd ~/ns-3.47
    ./ns3 run "hybrid-dcn-main --simTime=1.0 --experimentName=refactor-smoke"

## Experiment Status

The current code still uses command-line presets and built-in synthetic traffic modes inside the simulation entry. Formal experiment modules have not been developed in this repository yet.

`experiments/configs/` and `experiments/runs/` are reserved for future configuration files and run manifests. They do not currently define official experiment groups, traffic patterns, or performance metrics.

## Development Baseline

- ns-3 version: 3.47
- Build profile: optimized
- Main implementation language: C++
- Current module style: scratch-compatible C++ entry with header-only helper modules
