# ADR 0001: Qt owns one composited window

Status: accepted

Use Qt Quick with a transparent Qt WebEngine overlay above a scene-graph viewport texture. This gives Linux and Windows one composition model and allows normal translucent/rounded DOM controls. Do not create a second child window or platform fallback.
