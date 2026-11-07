---
name: ui-ux-design-systems
description: Ergonomic UI/UX design tokens, interaction states, accessibility standards, and motion specifications.
triggers: [design, ui/ux, wireframe, tokens, layout, accessibility, colors, typography]
category: design
---

# UI/UX Design Systems Protocol

1. **Semantic Design Tokens:**
   - Define color tokens semantically (e.g. `--color-surface-primary`, `--color-text-muted`, `--color-accent-brand`, `--color-status-danger`).
   - Use a consistent 4px or 8px baseline spatial grid (`--spacing-1` through `--spacing-12`).
   - Standardize typography scales with calibrated line heights and optical weights.

2. **State & Interaction Ergonomics:**
   - Design for all interaction states: default, hover, focus-visible, active, disabled, loading, empty, and error.
   - Use subtle, physical spring transitions (150-250ms cubic-bezier) rather than jarring instant snaps.
   - Support dark mode and high-contrast preferences automatically via `prefers-color-scheme`.

3. **Accessibility Invariants:**
   - Check all color combinations for WCAG 2.1 AA compliance (>= 4.5:1 for normal text).
   - Ensure all interactive elements have minimum 44x44px touch/click hit targets.
   - Design visible, accessible keyboard focus rings (`outline: 2px solid var(--color-focus)`).
