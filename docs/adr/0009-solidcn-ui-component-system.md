---
status: accepted
---

# Use a solidcn component layer for the control panel UI

## Context

The control panel is a Solid interface rendered in the shell's webview beside the native viewport. It needs form controls — selects, sliders, a dialog, a popover — that are accessible (keyboard, focus, ARIA), visually consistent, and themeable across light/dark and several color palettes.

Hand-rolled native form controls do not render consistently across system webviews (for example, native `<select>` ignores custom background styling under some Linux webviews) and lack the interaction affordances these controls need.

## Decision

Adopt a solidcn-style component layer.

- Unstyled, accessible primitives come from Kobalte (`@kobalte/core`).
- Each primitive is wrapped in a project-owned `components/ui/*` file that applies Tailwind classes; the wrappers are the app's styling contract, not the primitives directly.
- `cn()` (clsx + tailwind-merge) composes and de-conflicts class lists; `class-variance-authority` expresses button variants.
- Tailwind v4 is loaded through `@tailwindcss/vite`, with `tw-animate-css` for enter/exit transitions.
- Icons come from `lucide-solid`.
- Theming is data-attribute driven: `data-ui-theme` (system/light/dark) crossed with `data-color-theme`, resolved through CSS variables. A single accent variable (`--grow`) marks the living/growth affordances (growth axis, active status).

## Consequences

- The webview bundle carries the primitive library and its styles.
- New controls should be added as `components/ui/*` wrappers over a primitive rather than as ad-hoc elements, keeping styling and accessibility consistent.
- Theme values live in one stylesheet as CSS variables; adding a palette means adding one variable block.
- Panel controls that map to native concepts (viewport, camera) stay in this panel rather than as native overlay chrome.

## Acceptance checks

- Selects, sliders, dialogs, and popovers are keyboard operable and render identically in light and dark modes.
- Switching color theme restyles the whole panel without reload.
- The growth accent color appears only on growth/status affordances.

## Notes

The primitive wrappers are copied into the repository (owned source), so they can be adjusted per design need without depending on an external component registry at build time.
