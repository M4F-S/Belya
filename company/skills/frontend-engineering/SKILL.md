---
name: frontend-engineering
description: High-performance, accessible frontend development, modern component design, DOM optimization, and state synchronization.
triggers: [frontend, ui, css, html, javascript, typescript, components, dom, rendering]
category: engineering
---

# Frontend Engineering Protocol

1. **Lightweight & Dependency-Lean:**
   - Prefer semantic HTML5 elements (`<main>`, `<nav>`, `<article>`, `<section>`, `<dialog>`).
   - Use CSS custom properties for theming, CSS Grid/Flexbox for layout, and modern CSS features (`:has()`, container queries).
   - Keep JavaScript bundles minimal; eliminate heavy runtimes unless strictly required.

2. **State & Network Resilience:**
   - Implement optimistic UI updates with deterministic rollback on network failure.
   - Always render graceful loading, error, and empty states.
   - Debounce search and user input events to minimize backend API queries.

3. **Accessibility & Responsive Standards:**
   - Ensure complete keyboard navigability (focus indicators, tab order).
   - Maintain color contrast ratios compliant with WCAG 2.1 AA (4.5:1 for body text).
   - Test responsive breakpoints across mobile (375px), tablet (768px), and desktop (1280px+).
