# ADR-0003: Minimal Core Architecture

Status: Accepted

Date: 2026-08-07

Related Documents:

- CONSTITUTION.md
- 01_ARCHITECTURE.md
- 03_DEVELOPMENT_GUIDE.md
- ADR/0001-Architecture.md
- ADR/0002-Engine-Lifecycle-Contract.md

---

# Context

Desktop Scene Engine is currently in its architectural foundation stage.

The Engine currently provides only basic lifecycle coordination through:

```text
initialize()
update()
shutdown()
```

The project does not yet contain implementations for:

- Scene management
- Playback
- Rendering
- Storage
- Editor

The architecture defines future layers and modules, but no concrete responsibility currently requires additional runtime abstractions.

Creating empty Foundation, Runtime, Manager, or subsystem layers at this stage would add structure without providing actual behavior or ownership.

---

# Decision Summary

Desktop Scene Engine will maintain a minimal Engine core.

Architectural layers shall be introduced only when concrete responsibilities require them.

---

# Decision

The Engine shall remain a minimal core at the current development stage.

## 1. Engine Responsibility

Engine shall remain a thin lifecycle coordinator.

Engine is responsible for coordinating the runtime lifecycle, but shall not become the implementation location for all future business functionality.

## 2. No Empty Layers

The project shall not introduce empty architectural layers or placeholder abstractions without a concrete responsibility.

The following shall not be created prematurely:

- Foundation layer
- Runtime layer
- Manager hierarchy
- Generic subsystem registry
- Event system
- State machine

## 3. Concrete Modules First

New modules shall be introduced only when actual functionality requires them.

Examples include:

- Scene module when Scene behavior is implemented
- Storage module when persistence is implemented
- Playback module when playback behavior is implemented
- Rendering module when rendering behavior is implemented

Each module shall be introduced with a clear responsibility and stable boundary.

---

# Consequences

## Positive

- Lower architectural complexity
- Clearer ownership and lifecycle responsibilities
- Easier maintenance and debugging
- Fewer hidden dependencies
- Reduced risk of premature abstraction
- Better alignment with the current project requirements

## Negative

- Some abstractions will be introduced later when concrete requirements appear.
- Existing interfaces may need to evolve when real runtime modules require additional lifecycle, timing, or error-handling behavior.
- Future module boundaries must be designed carefully when implementation begins.

These trade-offs are accepted because introducing abstractions without real responsibilities would create unnecessary maintenance cost.

---

# Future Rules

Future development shall follow these rules.

## 1. Clear Module Responsibility

Every new module must have one clearly defined primary responsibility.

Modules shall not be introduced only to reserve a directory or namespace.

## 2. Domain Independence

Domain modules must not depend on:

- UI frameworks
- JSON libraries
- Platform APIs
- Infrastructure implementations

Domain modules should depend only on stable project-defined abstractions and value types.

Framework-specific and platform-specific code shall remain outside the Domain Layer.

## 3. Engine Boundary

Engine shall coordinate lifecycle and module interaction without becoming a God Object.

Business logic shall remain in the module responsible for that business concept.

## 4. Ownership

Each future module must define:

- Who creates its objects
- Who owns its objects
- How long its objects live
- When its resources are released

Ownership shall follow RAII and deterministic lifetime management principles.

---

# Alternatives Considered

## Alternative A - Create All Architectural Layers Immediately

Description

Create Foundation, Runtime, Manager, and subsystem directories and placeholder classes before implementing functionality.

Decision

Rejected.

Reason

Empty layers provide no concrete value and may establish artificial dependencies or unclear ownership.

## Alternative B - Put All Future Functionality Directly in Engine

Description

Expand the Engine class to contain Scene, Playback, Rendering, Storage, and other functionality.

Decision

Rejected.

Reason

This would create a God Object, reduce cohesion, and violate the project's modular architecture.

---

# Related Documents

- CONSTITUTION.md
- 01_ARCHITECTURE.md
- 02_DATA_MODEL.md
- 03_DEVELOPMENT_GUIDE.md
- ADR/0001-Architecture.md
- ADR/0002-Engine-Lifecycle-Contract.md

---

# Status

This ADR is **Accepted**.

---

End of Document
