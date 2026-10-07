# ADR-0002: Engine Lifecycle Contract

Status: Accepted

Date: 2026-08-07

---

# 1. Context

Desktop Scene Engine currently contains a minimal runtime skeleton.

The Engine module is responsible for coordinating application lifecycle, but does not yet contain actual runtime systems such as:

- Scene management
- Media playback
- Rendering
- Data loading
- Platform integration

At this stage, the lifecycle interface must remain simple while preserving future extensibility.

The current lifecycle interface is:

```cpp
initialize()

update()

shutdown()
2. Decision

The Engine lifecycle shall follow these principles:

Engine owns runtime lifecycle coordination.
Engine lifecycle APIs shall remain minimal during foundation stages.
Complex runtime management systems shall not be introduced before actual requirements exist.

The initial lifecycle contract is:

Construction

    ↓

initialize()

    ↓

update()

    ↓

shutdown()

    ↓

Destruction
3. initialize()
Current Design

initialize() is responsible for preparing Engine runtime state.

Current responsibilities:

Establish initialized state
Prepare future subsystem initialization entry point

It shall not currently handle:

Scene loading
Media loading
Rendering initialization
External resource management
Error Handling Decision

The current implementation does not introduce a complex error system.

Future implementations may evolve into:

bool initialize();

or:

Result initialize();

depending on actual subsystem requirements.

Before such requirements exist, introducing a general error framework is considered unnecessary complexity.

4. update()
Current Design

update() represents the runtime update entry point.

Current responsibilities:

Provide a stable runtime loop interface
Reserve future update coordination capability

It currently does not implement:

Animation updates
Playback updates
Rendering updates
Event processing
Timing Decision

The current interface does not include timing information.

Example:

void update();

Future timing models may include:

void update(float deltaTime);

or a dedicated timing abstraction.

The timing model shall be decided when runtime systems requiring temporal control are introduced.

5. shutdown()
Current Design

shutdown() is responsible for safely ending Engine runtime state.

Requirements:

Must be safe to call multiple times
Must release owned resources when resources are introduced
Must leave Engine in a valid destructible state

The destructor may call shutdown() to guarantee cleanup.

6. Ownership Rules

Engine lifecycle management shall follow:

Explicit ownership
RAII principles
No hidden global ownership
No unmanaged resource lifetime

Future subsystems added to Engine must clearly define:

Who creates the object
Who owns the object
Who destroys the object
7. Future Extension Rules

When adding new runtime modules:

Examples:

Scene System
Storage System
Playback System
Rendering System

They should not expand Engine into a large business object.

Preferred direction:

Engine

├── Lifecycle Coordination

├── Scene Module

├── Storage Module

├── Playback Module

└── Rendering Module

Engine remains the coordinator, not the owner of all business logic.

8. Rejected Alternatives
8.1 Complex State Machine

Rejected.

Reason:

The current lifecycle requirements are simple.

A state machine would introduce unnecessary abstraction before real lifecycle complexity exists.

8.2 Event System

Rejected.

Reason:

No subsystem currently requires event-driven communication.

Introduce only when actual decoupling requirements appear.

8.3 Immediate Error Framework

Rejected.

Reason:

The project currently has no resource loading or external failure scenarios requiring a unified framework.

9. Consequences
Positive
Simple initial architecture
Easy testing through Sandbox
Clear future extension point
Avoids premature abstraction
Negative
Some API decisions will need revision when runtime systems become complex
Error handling and timing models are intentionally postponed

These changes are expected and should be handled through future ADRs.

10. Related Documents
CONSTITUTION.md
01_ARCHITECTURE.md
03_DEVELOPMENT_GUIDE.md
ADR_INDEX.md
Status

Accepted.

This decision remains valid until replaced by a future ADR.