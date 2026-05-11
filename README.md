# Hybrid DCN ns-3 Simulation

This repository contains the thesis simulation project for:

基于图聚类与跨尺度协同的光电混合数据中心网络路由调度机制

## Workspace Layout

    ~/ns-3.47
        ns-3.47 source and build directory

    ~/sim
        thesis simulation project directory

## Project Layout

    src/
        main/
            hybrid-dcn-main.cc
        model/
        helper/
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

    build-meta/

## ns-3 Entry

The ns-3 scratch entry is:

    ~/ns-3.47/scratch/hybrid-dcn-main.cc

It is a symbolic link to:

    ~/sim/src/main/hybrid-dcn-main.cc

## Run Command

Run from the ns-3 root:

    cd ~/ns-3.47
    ./ns3 run "hybrid-dcn-main --simTime=1.0 --experimentName=stage-0c-check"

## Development Baseline

- ns-3 version: 3.47
- Build profile: optimized
- Main implementation language: C++
- Experiment automation: Bash and Python
