# Desktop Scene Engine Glossary

Version: 1.1.0

Status: Stable

Last Updated: 2026-08-20

---

# 1. Purpose

This document defines the official terminology used throughout Desktop Scene Engine.

Every technical document, source file, commit message, issue discussion, and AI-generated output shall use the terminology defined here.

This document is the single source of truth for project terminology.

---

# 2. Scope

This glossary defines concepts only.

It does not define:

- Source code classes
- JSON structures
- File formats
- Runtime behavior
- Implementation details

---

# 3. Naming Rules

Each concept shall have exactly one official name.

One name shall represent exactly one concept.

Synonyms should be avoided.

Abbreviations should only be used when officially defined.

---

# 4. Core Concepts

## Project

The complete Desktop Scene Engine workspace.

A Project owns all user-created data.

Related:

Scene

Playlist

Library

Settings

---

## Scene

The smallest independently switchable desktop presentation.

A Scene combines media resources with layout, crop, and playback configuration.

A Scene represents the complete visual state shown on the desktop.

---

## Media

A reusable media resource.

Examples include:

- Image
- Video
- GIF

Media does not contain display configuration.

---

## Library

The collection of all registered media resources within a Project.

Media should be registered once and reused by reference.

---

## Playlist

An ordered collection of Scene references.

A Playlist controls playback order only.

---

## Crop

Defines which portion of a Media resource is displayed.

Crop never modifies the original media.

---

## Layout

Defines how a Scene is placed on the desktop.

Examples include:

- Position
- Scale
- Alignment
- Background

---

## Playback

Defines runtime playback behavior.

Examples include:

- Loop mode
- Playback speed
- Frame rate
- Volume

---

## Settings

Application-wide configuration.

Settings affect application behavior but do not modify Scene definitions.

---

## History

The collection of recently used Scene references.

History never owns Scene objects.

---

## Favorites

The collection of user-selected Scene references.

Favorites never duplicate Scene data.

---

# 5. Architectural Terms

## Engine

The core runtime of Desktop Scene Engine.

The Engine contains business logic and runtime functionality.

The Engine has no graphical user interface.

---

## Editor

The graphical application used to edit Projects.

The Editor communicates with the Engine through public interfaces.

---

## Domain

The business layer of the Engine.

The Domain defines business rules independent of operating systems or UI frameworks.

---

## Infrastructure

Provides technical implementations such as:

- JSON serialization
- File access
- Image decoding
- Video decoding
- Logging

---

## Platform

Provides operating system integration.

Examples include:

- Win32
- Desktop APIs
- Window management

---

# 6. Data Terms

## Entity

An object with a stable identity.

Examples:

- Project
- Scene
- Media

---

## Value Object

An object that exists only as part of another object.

Examples:

- Crop
- Layout
- Playback

---

## Ownership

Ownership determines lifecycle.

Deleting an owner deletes the objects it owns unless explicitly preserved.

---

## Reference

A relationship established through stable identifiers rather than object duplication.

---

## Source Media

The original reusable media resource reference and its intrinsic properties, such as
dimensions, duration, codec information, and resource metadata. Decoded pixels and
video frames are transient Resource Boundary representations derived from Source
Media; they are not Domain presentation or Canvas data.

Source Media is content input. It does not own Crop, Layout, Canvas geometry, target
display identity, output mapping, or Preview Viewport geometry.

---

## Media Presentation

The per-Item visual interpretation of one Source Media inside a Desktop Canvas.

The current implementation uses Scene Crop, Layout, and MediaPresentation values.
Its modes are Native, Auto Fill, and Custom. Player Workspace Item state edits the
same semantics before the active playback projection supplies the temporary bound
Domain Scene; it does not create a competing persistent Crop or Layout model.

---

## Desktop Canvas

The logical composition/output space into which Media Presentation is rendered.

In current V1, Desktop Canvas is transient Player workspace/session state while JSON
v1.0 remains frozen. A resolved Display Target may synchronize its aspect, but the
two concepts remain separate.

Desktop Canvas is not a Preview Viewport, a RenderWidget size, a physical Display,
or a media's intrinsic dimensions.

---

## Source UV Space

Normalized Source Media coordinates in `[0, 1] × [0, 1]` with a top-left origin.

Source UV Space is used for Crop.

---

## Canvas Space

Renderer Canvas rectangles use normalized logical coordinates relative to Desktop
Canvas with a top-left origin. Preview widget pixels are not Canvas Space.

Editor-facing Layout position uses `X+ = right` and `Y+ = up`; presentation math
converts to the renderer's top-left Canvas coordinates.

---

## Display Pixel Space

Physical pixel bounds of a Windows display/output target.

Display Pixel Space is Platform/runtime information used only when mapping Desktop
Canvas to an output display.

---

## Display Target

An explicitly selected physical Windows display/output destination for Desktop
Canvas.

Display Target is not inferred from Player-window location, first enumeration order,
or a Qt screen index. An unavailable target remains unresolved rather than silently
binding to another display.

Current selection synchronizes Canvas aspect when resolved, but does not perform
physical display output or Canvas-to-Display Mapping.

---

## DisplayId

A stable-ish, re-resolvable Platform identity candidate for a physical display.

It is not a permanent hardware UUID. Windows target device path is preferred and GDI
source device name is fallback identity. Resolution can be affected by topology,
docks, ports, remote sessions, and driver changes.

---

## DisplayDescriptor

A platform-neutral value describing one display in an enumeration snapshot.

It may contain DisplayId, user-facing snapshot number, friendly name, source device
name, physical bounds, pixel dimensions, primary state, and DPI/scale information.
It never exposes native handles, Qt objects, or graphics objects to upper layers.

---

## Canvas-to-Display Mapping

The future transformation of the complete Desktop Canvas into a selected Display
Target.

Canvas-to-Display Mapping is separate from Media Presentation. Fill, Fit, Stretch,
and Exact are deferred physical-output policies, not current Preview modes.

---

## Media Presentation Mode

The current per-Item visual policy: Native, Auto Fill, or Custom.

Native contains the cropped source; Auto Fill covers the Canvas through a centered
source crop; Custom uses an explicit aspect together with Layout scale and position.

---

## Custom Presentation Aspect

The selected aspect value for Custom Media Presentation. `16:9` and `16:10` are
values/presets here, not Preview Viewport modes.

---

## Canvas-to-Display Mapping Policies

Fill, Fit, Stretch, and Exact are deferred policies for mapping a complete Desktop
Canvas to a physical Display Target. They are not current Media Presentation or
Preview Viewport behavior.

Exact Mapping, when introduced, will accept only numerically aspect-compatible Canvas
and Display Target dimensions within a defined tolerance; it will not require
identical pixel dimensions.

---

## Preview Viewport

The freely resizable Player/editor region that contains CanvasFrame. It is not
Desktop Canvas, Media Presentation, Display Target, or Canvas-to-Display Mapping and
has no top-level aspect modes.

---

## CanvasFrame

The fixed-aspect editor frame that fits the current Desktop Canvas inside Preview
Viewport. It maximizes the Canvas within the available viewport while preserving the
Canvas aspect; RenderWidget remains inside it as the Engine HWND target.

---

## Identify Display

A temporary Platform-owned visual overlay that helps a user confirm a selected
Display Target. It is non-destructive and does not mutate Engine playback state.

---

# 7. Development Terms

## Serialization

The process of converting runtime objects into a persistent storage format.

---

## Deserialization

The process of reconstructing runtime objects from persistent storage.

---

## Runtime

The period during which the Engine is actively executing.

---

## Configuration

User-editable settings that influence application behavior.

Configuration is not business logic.

---

# 8. AI Collaboration Terms

## Documentation

The complete collection of official project documents.

Documentation defines project rules and architecture.

---

## Specification

A document defining mandatory project behavior.

Specifications are normative.

---

## ADR

Architecture Decision Record.

An ADR documents significant architectural decisions and their rationale.

---

# 9. Terminology Rules

New terminology shall be added to this document before being introduced into source code or technical documentation.

Existing terminology shall not be renamed without an ADR.

Deprecated terminology should remain documented until fully removed from the project.

---

# Related Documents

- CONSTITUTION.md
- 01_ARCHITECTURE.md
- 02_DATA_MODEL.md
- 03_DEVELOPMENT_GUIDE.md
- 04_JSON_SPECIFICATION.md
- ADR/

---

# Status

This document is **Stable (v1.1)**.

The Phase 83 terminology is governed by ADR-0005. Future terminology changes shall
follow the ADR process.

---

End of Document
