# They Still Hunger project layout

The authoritative workspace is `D:\Modding\theyhunger\They Still Hunger`.

Paths, component branches, API versions, required runtime artifacts, exclusions, dependencies, and the production location are defined by the root `project.json`. The user and agent workflow is documented in the root `WORKFLOW.md`.

Use `tools\StillHunger.ps1` from the project root for feature worktrees, builds, isolated runtime assembly, smoke tests, integration, releases, promotion, and rollback. Component builds never deploy directly to production.

This repository is the Sultim `ref_gl`/RayTracedGL1 engine. It is unrelated to the separate FWGS `ref_vk` project.
