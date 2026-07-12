---
status: accepted
---

# Decouple simulation and rendering cadence

Plant simulation commits atomic steps independently of rendering; the renderer may present every completed state or skip states without changing numerical results. The simulation timestep controls accuracy, while execution rate and render cadence control wall-clock playback and cost. Timestep changes are allowed only while paused, avoiding mid-step configuration changes and keeping running execution simple.
