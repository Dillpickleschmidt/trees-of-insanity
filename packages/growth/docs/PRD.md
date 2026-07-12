# Growth library requirements

## Product

A standalone deterministic C++23 library for branch-module development and single-plant simulation from Synthetic Silviculture.

## Requirements

- Equal inputs produce equal results.
- Metric conversion preserves module visual proportions and fully-grown physiological ages while expressing geometry in meters.
- The complete Table 4 parameter catalog and presets remain available.
- One deep public interface owns deterministic plant creation, atomic stepping, and snapshots.
- Accepted morphospace, raw collision-volume, light, vigor, attachment, orientation, and shedding policies remain explicit.
- Z-up coordinates at the public boundary.
- Explicit validation errors for module parameters and prototypes.
- Standalone configure, build, test, and install.
- Deterministic all-pairs collision evaluation is the initial single-plant correctness oracle; spatial acceleration is required before Ecosystem implementation without changing the public interface.

## Exclusions

Ecosystem/population orchestration, files, OBJ, JSON, projects, workspaces, cameras, render meshes, USD, ovrtx, CUDA, Vulkan, Qt, browser UI, and persistence.
