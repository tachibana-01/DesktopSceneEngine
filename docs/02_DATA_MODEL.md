# Desktop Scene Engine Data Model

Version: 1.1.0

Status: Stable

Last Updated: 2026-08-20

---

# 1. Purpose

This document defines the logical data model of Desktop Scene Engine.

The data model describes the business objects managed by the Engine, their relationships, ownership, lifecycle, and constraints.

This document is implementation-independent.

It does not define:

- Source code classes
- JSON format
- Database schema
- Serialization
- Runtime implementation

---

# 2. Design Principles

The data model follows the following principles.

## Single Responsibility

Each object represents exactly one business concept.

---

## Explicit Ownership

Every object shall have one clearly defined owner.

Ownership determines lifecycle.

---

## Reference over Duplication

Objects should reference existing objects whenever possible.

Business data shall not be duplicated.

---

## Stable Identity

Business entities should be identified by stable unique identifiers.

Names and file paths are not identities.

---

## Storage Independence

The data model shall remain independent of JSON, databases, or any persistence technology.

---

## UI Independence

The data model shall never depend on Editor implementation or UI behavior.

---

# 3. Core Entities

Desktop Scene Engine consists of the following core entities.

## Project

Represents one complete Desktop Scene Engine workspace.

A Project owns all user-created data.

Phase 83 currently represents one logical Desktop Canvas and an explicitly selected
Display Target as transient Player workspace/session state. They are not fields of
the Domain Project and are not persisted by JSON v1.0. A later JSON vNext decision
must define any durable Project-level representation and migration.

---

## Scene

Represents one complete desktop scene.

A Scene is the smallest independently switchable unit managed by the Engine.

A Scene combines:

- Media
- Crop
- Layout
- Media Presentation
- Playback

Scene owns the per-item Media Presentation semantics. Existing Crop and Layout are
the durable presentation fields, and `MediaPresentation` supplies the current
Native, Auto Fill, or Custom policy. The model does not introduce a competing
persistent presentation object.

---

## Media

Represents one media resource.

Examples include:

- Image
- Video
- GIF

Media describes the resource itself.

It does not define how the resource is displayed.

---

# 4. Value Objects

The following objects exist only as part of another entity.

They cannot exist independently.

---

## Crop

Defines which portion of a Media is used.

Crop is resolution-independent.

Crop never modifies the original media.

---

## Layout

Defines where and how a Scene is displayed.

The current Layout values are:

- X/Y position
- X/Y scale

Layout does not affect Media.

`MediaPresentation` owns the current presentation mode. Native contains the source,
Auto Fill covers the Desktop Canvas, and Custom uses its explicit aspect plus Layout
scale and position. The editor exposes `X+ = right` and `Y+ = up`; presentation math
converts that Y convention to top-left Canvas coordinates before rendering.

---

## Playback

Defines runtime playback behavior.

Examples include:

- Loop Mode
- Playback Speed
- Volume
- Frame Rate
- Transition Policy

Playback belongs exclusively to one Scene.

---

# 5. Collection Objects

Collection objects organize entities.

---

## Playlist

Represents an ordered collection of Scene references.

Playlist does not own Scene.

Deleting a Playlist shall not delete Scene.

---

## Library

Represents the collection of registered Media.

Media should be imported once and referenced by multiple Scenes.

---

## History

Represents recently used Scene references.

History never owns Scene.

---

## Favorites

Represents user-selected Scene references.

Favorites never own Scene.

---

# 6. Relationships

The logical relationships are defined below.

Project

├── Library (1)

├── Playlist (N)

├── History (1)

├── Favorites (1)

└── Scene (N)

Library

└── Media (N)

Playlist

└── Scene Reference (N)

Scene

├── Media Reference (1)

├── Crop (1)

├── Layout (1)

└── Playback (1)

---

# 7. Ownership

Ownership determines lifecycle.

Project owns:

- Library
- Scene
- Playlist
- History
- Favorites

The current Desktop Canvas and Display Target selection are Player workspace/session
state while JSON v1.0 is frozen. They must not be described as persisted Domain
Project fields.

Scene owns:

- Crop
- Layout
- Playback
- Media Presentation

Library owns:

- Media

Playlist owns only references.

History owns only references.

Favorites own only references.

---

# 8. References

Business entities should communicate through references.

References should use stable unique identifiers.

Objects should never reference memory addresses.

File paths are external resource locations rather than business identities.

---

# 9. Lifecycle

Media

Create

↓

Register

↓

Reference

↓

Use

↓

Remove

---

Scene

Create

↓

Configure

↓

Store

↓

Reference

↓

Play

↓

Modify

↓

Delete

---

Playlist

Create

↓

Add Scene References

↓

Play

↓

Modify

↓

Delete

---

# 10. Extensibility

Future entity types may include:

- Widget
- Plugin
- Shader
- Animation
- Audio
- Multi-Monitor Layout

The model is designed so that new entities can be introduced without modifying existing stable entities whenever possible.

---

# 11. Phase 83 Canvas and Display Target Contract

Phase 83 implements the editor foundation described here. It does not add JSON v1.0
persistence, Canvas-to-Display Mapping, or physical desktop output.

## Semantic Pipeline

```text
Source Media
    ↓
Media Presentation
    ↓
Desktop Canvas
    ↓
Canvas-to-Display Mapping
    ↓
Display Target / Physical Display
```

Source Media is reusable content input and does not own presentation or output
state. Media Presentation belongs to one Scene/item. The current Desktop Canvas is
Player workspace/session state and remains stable across active-item changes. Display
Target selection resolves through a Platform `DisplayId` at runtime and remains
separate from Desktop Canvas.

## Coordinate Spaces

- Source UV Space is normalized `[0, 1] × [0, 1]`, top-left origin, and is used by
  Crop.
- Canvas render rectangles are normalized relative to Desktop Canvas with a top-left
  origin. Editor-facing Layout uses `X+ = right` and `Y+ = up`; the presentation-math
  boundary performs the conversion.
- Display Pixel Space is Platform-owned physical display geometry used only for
  future Canvas-to-Display Mapping.

## Scope

Release 1.x has one Source Media and one Media Presentation per Playlist Item, one
Desktop Canvas, and one explicitly selected Display Target. It excludes multi-layer
composition, simultaneous multi-target output, spanning Canvas, and independent
multi-display Canvas instances. Current Extended Media Layers are metadata only and
do not introduce visual composition.

Preview Viewport is a freely resizable Player UI region. CanvasFrame contains the
fixed-aspect Desktop Canvas within it; Preview geometry is not a Domain Canvas or
presentation field.

## Persistence Status

Desktop Canvas and Display Target selection are implemented transient state, but
JSON v1.0 remains unchanged. Any future persistence needs explicit JSON vNext version
support and migration policy as defined by ADR-0005.

---

# Related Documents

CONSTITUTION.md

01_ARCHITECTURE.md

03_DEVELOPMENT_GUIDE.md

04_JSON_SPECIFICATION.md

---

End of Document
