# ADR-0005: Canvas, Media Presentation, and Display Target Boundaries

Status: Accepted — Phase 83 foundation implemented

Date: 2026-08-20

Closure update: 2026-08-25

Related Documents:

- CONSTITUTION.md
- GLOSSARY.md
- 01_ARCHITECTURE.md
- 02_DATA_MODEL.md
- 04_JSON_SPECIFICATION.md
- 05_ROADMAP.md
- ADR/0001-Architecture.md
- ADR/0004-Infrastructure-Serialization-Boundary.md

---

# Context

Phase 83 began by separating editor widget geometry from the semantic space used to
present media. The foundation is now implemented:

- Player workspace state owns a logical `DesktopCanvas` for the current session.
- Each Workspace Item carries Crop, Layout, and `MediaPresentation` values that map
  into its projected `Domain::Scene`.
- Engine translates the active Scene presentation into private immutable render data.
- Platform enumerates Windows displays through re-resolvable `DisplayId` values and
  owns the temporary Identify overlay.
- Player provides display selection, a fixed-aspect CanvasFrame, and a canvas-boundary
  editor aid.

The implementation does **not** add physical desktop output, a general
Canvas-to-Display Mapping policy, or persistence changes to JSON v1.0. This ADR
records the implemented Phase 83 contract and keeps those later concerns distinct.

# Decision

## 1. Semantic pipeline

The authoritative conceptual pipeline is:

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

Only the first three stages are rendered in the current Player Preview. Display
selection currently supplies Canvas aspect information; it does not perform the
fourth or fifth stage as physical output.

## 2. Release 1.x scope

Release 1.x renders one visual Source Media for one Playlist Item through one Media
Presentation into one Desktop Canvas. It does not define:

- AE/Premiere-style multi-layer composition;
- arbitrary simultaneous visual layers;
- a compositor graph or generalized scene graph authoring;
- spanning Canvas instances;
- simultaneous multi-target output.

Extended Media Layers remain Player metadata and do not create visual composition in
the current renderer. Broader composition requires a later architectural decision.

## 3. Source Media

Source Media is the original reusable image or video and its intrinsic properties,
including dimensions, duration, codec data, and resource metadata. Decoded image
pixels and video frames are transient Resource Boundary representations derived from
Source Media.

Source Media is content input only. It does not own Canvas geometry, Crop, position,
scale, presentation mode, DisplayId, display mapping, or Player widget geometry.

## 4. Media Presentation

Media Presentation is the per-Item visual interpretation of one Source Media inside
the Desktop Canvas. The current implementation reuses the existing Domain value
types:

- `Domain::Scene::crop` selects normalized Source UV space.
- `Domain::Scene::layout` supplies X/Y position and X/Y scale.
- `Domain::Scene::presentation` supplies `Native`, `AutoFill`, or `Custom` mode and
  the Custom aspect value.

Player `WorkspaceItemPresentation` holds the editable session values. Rebuilding the
active playback projection copies them into the temporary Domain Project bound to
Engine; Engine then prepares private renderer input. This creates no second
persistent Crop or Layout model.

The current modes have these meanings:

- **Native** contains the full cropped source, preserves source aspect, centers it,
  and permits unused Canvas area.
- **Auto Fill** covers the Canvas, preserves source aspect, and applies a centered
  source crop when needed.
- **Custom** applies the selected Custom aspect, then the Scene Layout scale and
  X/Y position.

`16:9` and `16:10` are aspect values/presets for Custom presentation. They are not
top-level Preview modes.

Editor-facing position uses `X+ = right` and `Y+ = up`. Source UV and renderer
Canvas rectangles use a top-left origin; the presentation-math boundary performs the
Y-axis conversion.

## 5. Desktop Canvas and Preview Viewport

Desktop Canvas is the logical composition/output space for Media Presentation. In
the current V1 it is transient Player `WorkspaceModel` state, not a JSON v1.0 field
and not a physical monitor. Its default aspect is 16:9.

Preview Viewport is the freely resizable editor region. It is not Desktop Canvas
state and has no `Free`, `Native`, `16:10`, or `16:9` mode. The visual hierarchy is:

```text
PreviewViewportHost
    ↓
CanvasFrame
    ↓
RenderWidget
```

`CanvasFrame` contains the logical Canvas at its fixed current aspect. It is
maximized within the Preview Viewport while preserving that aspect: wide viewports
produce equal side gutters, while square or tall viewports produce equal top/bottom
gutters. `RenderWidget` remains the Engine HWND target.

## 6. Display Target, descriptors, and identity

Display Target is an explicitly selected physical Windows display. It remains
separate from Desktop Canvas: selecting a display does not make the display itself
the Canvas and does not start physical display output.

Platform owns native enumeration, identity resolution, and temporary Identify
overlays. Upper layers receive only a platform-neutral `DisplayDescriptor` snapshot
with a `DisplayId`, user-facing number, friendly/source name, physical bounds, pixel
dimensions, primary state, and relevant refresh information. It contains no native
handle, Qt object, or graphics object.

`DisplayId` is stable-ish and re-resolvable, not a permanent hardware UUID. Windows
target device path is preferred; GDI/source device name is the fallback identity.
Unplug/replug, docks, ports, remote sessions, and driver changes can make a selected
target unresolved. An unresolved target remains unresolved and must not silently
fall back to another display.

The user-facing `Display 1` / `Display 2` number is an enumeration-snapshot label,
not identity. A number and the target used for Identify come from the same fresh
snapshot.

## 7. Current Display-to-Canvas V1 policy

When the selected Display Target resolves successfully, its physical pixel aspect is
applied to the current Desktop Canvas aspect. For example, `2560 × 1600` yields a
16:10 Canvas and `1920 × 1080` yields a 16:9 Canvas.

If the selected Display Target later becomes missing or unresolved, Player preserves
the last valid Canvas aspect and does not silently select another display. This is a
V1 Canvas-aspect synchronization policy only; it is not Canvas-to-Display Mapping.

## 8. Identify Display

Platform re-resolves the requested `DisplayId` immediately before showing the
temporary Identify overlay. The overlay is non-destructive, non-activating,
input-transparent where practical, topmost, targeted to the freshly resolved
display, and expires automatically. It does not alter Engine playback or create
desktop wallpaper output.

## 9. Deferred work

The following are deliberately outside Phase 83:

- **Canvas-to-Display Mapping:** physical-output policies such as Fill, Fit,
  Stretch, and Exact.
- **Real Desktop Output:** wallpaper deployment, WorkerW/Progman integration,
  physical-display presentation windows, multi-monitor output behavior, and desktop
  lifecycle/recovery.
- **JSON vNext:** persistence and migration for Canvas, Display Target, and
  presentation state. JSON v1.0 remains frozen.
- **Timeline Preview v2:** presentation-aware preview composition. Current Timeline
  Hover Preview remains source-frame oriented and does not claim to represent Crop,
  Scale, Canvas, or Display composition.
- **Representative-frame cache:** any Source + Time cache/preload must remain
  independent of Crop, Scale, Position, Canvas, and Display.
- **Offscreen or multi-target composition:** generalized compositor, multiple
  simultaneous render targets, and multi-target output architecture.

No future manager, compositor, graph, or output API is specified by this ADR.

## 10. Engineering follow-up

Any future change to Renderer, GraphicsBackend, shaders, vertex winding, or UV
mapping should include a visible-media rendering gate. A desirable future automated
regression is a known small texture rendered to a hidden/offscreen D3D target, read
back and asserted to differ from the clear color. This follows the Phase 83
blank-preview regression, where pure math and build checks passed while back-face
culling removed all visible output. That gate is an engineering follow-up, not a
Phase 83 blocker.

# Alternatives Considered

## Treat Preview Viewport geometry as Canvas

Rejected. Preview Viewport is driven by editor layout and must not redefine the
logical Canvas when it resizes.

## Let Source Media own Crop, Layout, or Display Target

Rejected. One source can be presented differently by different Scene/items. Source
Media remains reusable content input.

## Add a second persistent Media Presentation object

Rejected. It would compete with the existing Scene-owned Crop, Layout, and
MediaPresentation value types.

## Use display number or primary status as display identity

Rejected. Both are snapshot/runtime facts rather than stable target identity. Primary
status may be chosen only as an explicit first selection, never as ongoing fallback.

## Design multi-layer or multi-target composition now

Rejected. The current product has one visual source, one presentation, and one
Canvas. The added architecture has no current consumer.

# Consequences

Positive consequences:

- Preview widget geometry, Media Presentation, Desktop Canvas, and Display Target
  have separate owners and meanings.
- Renderer receives Engine-prepared immutable presentation values rather than
  querying Player or Domain state directly.
- Platform-specific display handles remain private.
- Source-frame Timeline Preview and any future representative-frame cache remain
  reusable across presentation changes.
- One current render target remains sufficient for the implemented V1 Preview.

Constraints:

- Current Canvas and Display selection are session/editor state while JSON v1.0 is
  frozen.
- Physical display output and Canvas-to-Display Mapping need explicit later product
  and architecture work.
- A selected display may become unresolved and is never silently replaced.

# Status

Phase 83 — Canvas / Media Presentation / Display Target Editor Foundation is
complete. This ADR records the implemented foundation and its explicitly deferred
work; it does not claim desktop output or JSON vNext implementation.
