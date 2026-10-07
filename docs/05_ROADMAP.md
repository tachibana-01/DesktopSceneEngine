# Desktop Scene Engine Roadmap

Version: 1.1.0

Status: Historical planning/checkpoint reference

Last Updated: 2026-09-14

> Publication note: this document preserves earlier planning and the Slice 4
> checkpoint, including its then-current deferred work. It is not the current
> completion state or next-task authority. See the [public README](../README.md#development-status)
> for the prepared source's current capabilities; later sections remain historical.

---

# 1. Purpose

This document defines the long-term development roadmap of Desktop Scene Engine.

The roadmap describes the planned evolution of the project from its first working version to future expansion.

The roadmap is intended to guide development priorities rather than provide fixed release dates.

Specific implementation schedules are intentionally omitted.

---

# 2. Roadmap Principles

The project follows the principles below.

- Architecture before features.
- Stability before optimization.
- Simplicity before complexity.
- Incremental development.
- Backward compatibility whenever practical.
- Documentation evolves together with implementation.

Every completed milestone should leave the project in a usable state.

---

# 3. Current Project Status

This section records the historical Slice 4 checkpoint. Its phase descriptions,
deferred lists and release progression are historical/strategic references, not
current completion states or immediate-next-task requirements.

Most recently completed milestone:

**L6 — Windows Desktop Deployment, through Slice 4.**

| Slice | Status |
| --- | --- |
| Slice 1 — DirectComposition Presentation Transport | CLOSED |
| Slice 2 — Centered Canvas Fit | CLOSED |
| Slice 3 — Owned Raised-Desktop Presentation Host | CLOSED |
| Slice 4 — Fixed-Session Desktop Deployment Lifecycle | CLOSED |

Committed implementation checkpoint (branch `main`):

```text
3b8bf6b24cdfdbd3b2835e7510e88fc83f05698e
feat(player): deploy fixed display session to desktop
```

Slice 4 closure evidence:

- Implementation and reversible Preview-only → Deploy → Undeploy → Redeploy →
  normal Session close lifecycle completed. Undeploy removes only the Desktop
  presentation endpoint; Preview and the shared Engine/runtime session remain alive.
- Automated validation passed: 33 affected Debug targets built, 32/32 relevant
  tests, six focused GREEN invocations, and 10/10 consecutive focused lifecycle runs.
- Human Gate #1 A–E passed in one continuous real fixed Display Session, including
  Desktop/Preview synchronization, bound-display placement, desktop ordering/input,
  state preservation, and clean Desktop teardown on normal close.
- Final read-only adversarial review: P0: 0, P1: 0, P2: 0, P3: 0, Nitpick: 0;
  verdict `READY FOR NARROW SLICE 4 COMMIT`. The narrow shipping commit above is complete.

Deferred work:

- Explorer restart Human Gate / automatic Explorer recovery: **DEFERRED**. It is
  not unfinished Slice 4 work, a required Human Gate, or a Slice 4 shipping blocker.
  Explorer/Shell lifecycle hardening requires a separate future product requirement.
- L6 Slice 5 — Coordinated Shared Graphics-Generation Recovery: **NOT STARTED**;
  **DEFERRED FROM THE IMMEDIATE NEXT TASK**. It remains future reliability work,
  not the mandatory next milestone because of its numeric order.
- L7 — cross-process / same-display occupancy: **DEFERRED**. Mutex,
  single-instance-per-display policy, IPC, registry, daemon, lock files, and
  multi-session scheduling/occupancy mechanisms are possible future work, not
  current requirements.

Next planned activity (not started): a separate read-only/product-validation phase
to audit existing authored-state, persistence, display ownership, and
application-preference behavior before selecting the next feature milestone. This P0 checkpoint
synchronization does not begin that audit.

---

# Phase 1 — Foundation (MVP)

Goal:

Build the smallest fully functional Desktop Scene Engine.

Core objectives:

- Engine project initialization
- Editor project initialization
- CMake build system
- Qt integration
- JSON loading and saving
- Logging system
- Configuration system
- Basic media loading
- Static image wallpaper support

Deliverable:

The application can load a single image and set it as the desktop wallpaper.

---

# Phase 2 — Scene System

Goal:

Implement the core Scene architecture.

Features:

- Scene creation
- Scene editing
- Scene serialization
- Scene switching
- Scene preview
- Scene persistence

Deliverable:

Users can create, edit, save, and switch between multiple scenes.

---

# Phase 3 — Crop & Layout

Goal:

Implement advanced desktop layout capabilities.

Features:

- Free crop region
- Fixed aspect ratio crop
- Arbitrary crop region
- Position adjustment
- Scaling
- Alignment
- Background modes
- Multiple display modes

Deliverable:

Images can be cropped and positioned independently of the original resolution.

---

# Phase 4 — Playlist System

Goal:

Implement automatic scene management.

Features:

- Playlist
- Sequential playback
- Reverse playback
- Random playback
- Manual switching
- Scene interval configuration

Deliverable:

Users can organize scenes into reusable playlists.

---

# Phase 5 — Media Library

Goal:

Implement centralized media management.

Features:

- Media registration
- Media scanning
- Metadata
- Favorites
- History
- Search
- Tags

Deliverable:

Media resources become reusable across multiple scenes.

---

# Phase 6 — Video Wallpaper

Goal:

Support dynamic wallpapers.

Features:

- Video playback
- GIF playback
- Individual volume
- Loop mode
- Playback speed
- Frame rate control
- Resume playback
- Automatic switching

Deliverable:

Video scenes become first-class citizens alongside image scenes.

---

# Phase 7 — Editor Enhancement

Goal:

Improve editing efficiency.

Features:

- Crop editor
- Layout editor
- Preview window
- Drag-and-drop
- Multi-selection
- Undo / Redo
- Keyboard shortcuts
- Property inspector

Deliverable:

The Editor provides a complete graphical workflow.

---

# Phase 8 — Performance Optimization

Goal:

Improve runtime efficiency.

Features:

- Image cache
- Video cache
- Lazy loading
- Background loading
- Memory optimization
- Rendering optimization
- Startup optimization

Deliverable:

Large media libraries remain responsive.

---

# Phase 9 — Advanced Features

Potential future features include:

- Multi-monitor support
- Scene transitions
- Animation system
- Plugin system
- Theme support
- Script API
- Wallpaper scheduling
- Automatic wallpaper rules
- Import / Export
- Backup and Restore

These features are optional and should not compromise architectural consistency.

---

# Phase 10 — Long-Term Evolution

The architecture is designed to support future expansion without major redesign.

Possible future directions include:

- SQLite storage
- Plugin SDK
- Cross-platform support
- Additional media formats
- Hardware acceleration improvements
- Rendering backend replacement
- Cloud synchronization (optional)
- Package management (optional)

Implementation depends on future project requirements.

---

# Phase 83 — Canvas / Media Presentation / Display Target Editor Foundation

Status: Complete.

Phase 83 delivered:

- a logical Desktop Canvas distinct from the freely resizable Preview Viewport;
- per-Item Native, Auto Fill, and Custom Media Presentation through the active
  Scene → Engine → Renderer path;
- a fixed-aspect Preview CanvasFrame and a Canvas-boundary editor aid;
- Windows display discovery with platform-neutral descriptors and stable-ish
  `DisplayId` selection identity;
- Player Display Target selection and temporary Platform-owned Identify overlays; and
- current V1 resolved-Display → Canvas-aspect synchronization.

The phase does not claim that Desktop Canvas and Display Target are the same model
concept. Display selection currently changes the session Canvas aspect only; it does
not create real physical-display output.

## Explicitly Deferred After Phase 83

The following are future work, not open Phase 83 sub-phases:

- Canvas-to-Display Mapping policies: Fill, Fit, Stretch, and Exact;
- real Desktop Output: wallpaper deployment, WorkerW/Progman integration, physical
  presentation windows, multi-monitor output, and lifecycle/recovery;
- JSON vNext persistence and migration for Canvas, Display Target, and presentation;
- presentation-aware Timeline Preview composition;
- representative source-frame cache/preload; and
- generalized offscreen, multi-target, or multi-layer compositor architecture.

These later efforts require their own product and architecture decisions. JSON v1.0
remains unchanged until a separate versioning/migration scope is approved.

---

# 4. Development Workflow

Each new feature should follow the workflow below.

Proposal

↓

Architecture Review

↓

Implementation

↓

Testing

↓

Documentation Update

↓

Release

Architectural changes require an ADR before implementation.

---

# 5. Release Strategy

Development should prioritize stable incremental releases.

Recommended version progression:

v0.1

Project initialization

↓

v0.2

Static wallpaper

↓

v0.3

Scene system

↓

v0.4

Crop & Layout

↓

v0.5

Playlist

↓

v0.6

Media Library

↓

v0.7

Video Wallpaper

↓

v0.8

Editor Enhancement

↓

v0.9

Optimization

↓

v1.0

Stable Release

Version numbers represent project maturity rather than feature count.

---

# 6. Success Criteria

Desktop Scene Engine is considered successful when it satisfies the following goals.

Architecture

- Modular
- Maintainable
- Extensible

Engine

- Independent of Editor
- Stable public interfaces
- Clear module boundaries

Editor

- Complete editing workflow
- Responsive UI
- Easy to use

Documentation

- Up-to-date
- Consistent
- Sufficient for AI-assisted development

Codebase

- Low coupling
- High cohesion
- Minimal technical debt

---

# 7. Long-Term Vision

Desktop Scene Engine aims to become a robust desktop scene management platform rather than a simple wallpaper application.

The project prioritizes:

- Clean architecture
- Long-term maintainability
- Stable specifications
- AI-assisted development
- Sustainable evolution

Every new feature should strengthen the platform rather than increase unnecessary complexity.

---

# Related Documents

- README.md
- CONSTITUTION.md
- 01_ARCHITECTURE.md
- 02_DATA_MODEL.md
- 03_DEVELOPMENT_GUIDE.md
- 04_JSON_SPECIFICATION.md
- GLOSSARY.md
- ADR/

---

# Status

This document is **Frozen (v1.0)**.

Roadmap updates should reflect completed milestones or significant strategic changes.

---

End of Document
