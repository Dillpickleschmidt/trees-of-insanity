# ADR 0004: One WebChannel adapter

Status: accepted

Solid communicates only through `DesktopBridge`: action dispatch, viewport rectangle, camera input, and viewport status. Actions are coarse and typed in the frontend catalog. Qt and model objects are not exposed directly.
