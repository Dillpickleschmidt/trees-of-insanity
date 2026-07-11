# ADR 0005: DesktopSession owns application state

Status: accepted

`DesktopSession` is the model facade for Project, active workspace, physiological ages, and viewport preferences. It returns growth snapshots, never renderer or Qt objects. Shell coordinates projections and rendering.
