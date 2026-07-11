# Repository context

Trees of Insanity is one Git repository with two owned products:

- `packages/growth`: standalone deterministic C++ plant-growth library.
- `desktop`: Qt/CUDA/Vulkan desktop application and Solid UI.

## Boundaries

Growth never depends on desktop code. Desktop model may depend on growth. Graphics consumes model snapshots through growth-owned data types. Shell coordinates model, graphics, Qt, and WebChannel. UI depends only on the WebChannel action contract.

Read the nearest `CONTEXT.md` before changing a product. Repository-wide build and dependency rules live in `docs/architecture.md`.
