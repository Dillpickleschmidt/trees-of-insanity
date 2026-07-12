# Persist one fresh typed Project schema

Persist one local JSON Project containing project-wide content, active workspace, and complete typed state for Module, Plant, and Ecosystem workspaces. Shared plant types do not own workspace selections; each workspace stores its own active choices and viewport state without fallback inheritance. Application preferences remain outside the Project. Replace the unreleased schema directly, remove the separate viewport-preferences file, and provide no migration or legacy parsing.
