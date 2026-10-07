# Desktop Scene Engine Architecture

Version: 1.1.0

Status: Stable

Last Updated: 2026-08-20

---

# 1. Purpose

This document defines the system architecture of Desktop Scene Engine.

It describes how the system is organized, how its major components interact, and the dependency rules that govern the entire project.

This document does not describe implementation details, data structures, file formats, or source code.

---

# 2. Architectural Goals

Desktop Scene Engine is designed as a long-term, modular desktop scene management platform.

The architecture is designed to achieve the following goals:

- High maintainability
- High modularity
- Clear separation of responsibilities
- Low coupling
- High cohesion
- Easy extensibility
- AI-friendly project structure
- Long-term architectural stability

Implementation convenience shall never take precedence over architectural consistency.

---

# 3. System Overview

Desktop Scene Engine manages desktop scenes.

A desktop scene represents the complete visual state presented on the user's desktop.

The system is responsible for:

- Managing scene resources
- Controlling playback
- Computing scene state
- Rendering desktop output
- Persisting user configuration
- Providing editing capabilities

The architecture separates these responsibilities into independent modules.

---

# 4. Product Composition

The project consists of two major products.

## Engine

The Engine is the core runtime.

It contains all business logic required to evaluate and execute desktop scenes.

The Engine has no graphical user interface.

It may be used independently as a library or SDK.

---

## Editor

The Editor is the graphical management application.

It provides tools for:

- Scene editing
- Media management
- Crop editing
- Playlist management
- Settings
- Preview
- Project management

The Editor communicates with the Engine through public interfaces only.

The Editor must not implement business logic already provided by the Engine.

---

# 5. Layered Architecture

The Engine follows a layered architecture.

Presentation Layer

(Not applicable to Engine)

↓

Application Layer

Coordinates application workflows.

Responsible for:

- Commands
- Use Cases
- Runtime orchestration

↓

Domain Layer

Contains the core business model.

Defines:

- Domain concepts
- Business rules
- Runtime state
- Playback logic

The Domain Layer must remain independent of frameworks and operating systems.

↓

Infrastructure Layer

Provides technical implementations.

Examples include:

- JSON persistence
- Image decoding
- Video decoding
- Logging
- File system access

↓

Platform Layer

Provides operating-system-specific functionality.

Examples include:

- Windows Desktop APIs
- Win32
- DWM integration

---

# 6. Dependency Rules

Dependencies shall always point toward more stable layers.

Allowed dependency direction:

Application
↓

Domain
↓

Infrastructure
↓

Platform

The reverse direction is prohibited.

The Domain Layer must never depend on:

- Qt
- Win32
- DirectX
- FFmpeg
- JSON libraries
- Database libraries

Framework-specific code shall remain outside the Domain Layer.

---

# 7. Module Responsibilities

Each module shall have exactly one primary responsibility.

Business logic belongs to the Domain Layer.

Technical implementation belongs to the Infrastructure Layer.

Operating-system interaction belongs to the Platform Layer.

User interaction belongs to the Editor.

No responsibility shall be duplicated across modules.

---

# 8. Extension Strategy

The architecture is designed for continuous extension.

New functionality should be added by introducing new modules rather than modifying existing stable modules whenever possible.

Future features should integrate through existing interfaces.

Architectural changes shall be documented through Architecture Decision Records (ADR).

---

# 9. Phase 83 Canvas / Media Presentation / Display Target Editor Foundation

Phase 83 is complete. It implements the editor foundation for Canvas, per-Item Media
Presentation, Windows Display Target selection, Identify, and the Player Preview
CanvasFrame. It does not implement physical desktop output or JSON vNext.

## Current Semantic Pipeline

```text
Source Media
    ↓
Media Presentation
    ↓
Desktop Canvas
    ↓
Canvas-to-Display Mapping
    ↓
Physical Display / Display Target
```

The Player Preview currently renders Source Media through Media Presentation into the
Desktop Canvas. The final Canvas-to-Display Mapping and physical output stages are
deferred. Display selection supplies the current Canvas aspect only; it does not
render to that display.

## Current Boundaries

Domain owns Scene Crop, Layout, and MediaPresentation value types without Player,
Windows, or graphics dependencies. The current Desktop Canvas and selected Display
Target are transient Player workspace/session state while JSON v1.0 is frozen.

Player owns Canvas and presentation editing UX, the freely resizable Preview Viewport,
Display Target selection, and the CanvasFrame. Its visual hierarchy is:

```text
PreviewViewportHost
    ↓
CanvasFrame
    ↓
RenderWidget
```

CanvasFrame preserves the logical Desktop Canvas aspect while fitting inside the
Preview Viewport. RenderWidget remains the Engine HWND target. Resizing the Preview
Viewport never changes Canvas semantics.

Engine takes the active Scene's Crop, Layout, and MediaPresentation values, builds
private immutable presentation input, and coordinates Rendering. It remains Qt-free
and exposes no Player types.

Rendering consumes that private input and owns rendering execution and GPU resources.
It does not query Player controls, Runtime, or display identity.

Platform owns Windows display enumeration, identity resolution, and the temporary
Identify overlay. It provides platform-neutral descriptors upward; Win32 handles and
DISPLAYCONFIG structures remain internal.

## Media Presentation V1

Each Item uses one of these presentation modes:

- **Native:** contain the full source, preserve aspect, center it, and permit unused
  Canvas area.
- **Auto Fill:** cover the Canvas, preserve aspect, and center-crop the source as
  needed.
- **Custom:** use the selected Custom aspect, scale, and X/Y position.

`16:9` and `16:10` are aspect values/presets for Custom presentation, not Preview
Viewport modes. Editor position uses `X+ = right` and `Y+ = up`; Engine presentation
math converts to top-left Canvas coordinates for rendering. Images and video use the
same presentation semantics.

## Display Target V1

Display selection resolves a `DisplayId`, never a snapshot number. Target device path
is preferred identity and GDI source name is fallback identity. Display numbers are
user-facing labels only. A missing target remains unresolved and never silently falls
back to a different display.

When the selection resolves, its physical pixel aspect synchronizes the current
Desktop Canvas aspect. If it becomes unresolved, Player preserves the last valid
Canvas aspect. This V1 synchronization policy does not make Display Target and
Desktop Canvas the same model concept and is not Canvas-to-Display Mapping.

Platform re-resolves the DisplayId immediately before showing the temporary,
non-activating, click-through where practical Identify overlay. It targets the same
fresh display snapshot used for the displayed number.

## Deferred Work

The following remain outside Phase 83:

- Canvas-to-Display Mapping policies such as Fill, Fit, Stretch, and Exact;
- physical desktop output, including WorkerW/Progman, wallpaper deployment,
  presentation windows, multi-monitor behavior, and lifecycle/recovery;
- JSON vNext persistence and migration for Canvas, Display Target, and presentation;
- presentation-aware Timeline Preview composition;
- representative-frame caching that depends on presentation state; and
- offscreen, multi-target, or generalized compositor architecture.

## Persistence Boundary

JSON v1.0 remains frozen and does not store Desktop Canvas or Display Target. The
implemented Player workspace state is not retroactive justification to change v1.0.
Any persistence requires a later explicit schema version and migration policy.

---

# 10. Out of Scope

This document intentionally does not define:

- Domain models
- JSON schemas
- Configuration formats
- Class diagrams
- API definitions
- Source code organization
- Build system
- Coding conventions

These topics are specified in their respective documents.

---

# Related Documents

CONSTITUTION.md

02_DATA_MODEL.md

03_DEVELOPMENT_GUIDE.md

04_JSON_SPECIFICATION.md

ADR/

---

End of Document
