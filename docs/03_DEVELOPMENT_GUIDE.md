# Desktop Scene Engine Development Guide

Version: 1.1.0

Status: Stable

Last Updated: 2026-07-30

---

# 1. Purpose

This document defines the development rules of Desktop Scene Engine.

Its purpose is to ensure that all source code follows the same architectural principles, coding philosophy, project structure, memory safety requirements, and development workflow.

Every contributor, whether human or AI, shall follow this guide.

---

# 2. Development Philosophy

Desktop Scene Engine is designed as a long-term maintainable project.

Development priorities are:

1. Correct architecture
2. Maintainability
3. Readability
4. Extensibility
5. Reliability
6. Performance
7. Convenience

Performance optimization shall never sacrifice architectural consistency without clear justification.

The project prioritizes stable and understandable engineering solutions over short-term implementation speed.

---

# 3. General Principles

The following principles apply to all source code.

- Single Responsibility Principle (SRP)
- Separation of Concerns (SoC)
- Low Coupling
- High Cohesion
- Composition over Inheritance
- Explicit Dependencies
- Predictable Behavior
- Clear Ownership
- Deterministic Lifetime Management

Temporary shortcuts are prohibited unless documented.

---

# 4. Project Structure

The repository shall remain organized as follows.


DesktopSceneEngine/

├── Engine/
├── Editor/
├── Sandbox/

├── docs/
├── assets/
├── data/

├── tests/

├── tools/

├── third_party/

└── CMakeLists.txt


Each directory has one clearly defined responsibility.

Directories shall not contain unrelated files.

---

# 5. Module Responsibilities

## Engine

Responsible for:

- Business logic
- Runtime system
- Rendering abstraction
- Playback system
- Data loading
- Serialization
- Platform abstraction

Engine shall not depend on Editor or UI implementation.

Engine shall provide stable public interfaces for external modules.

---

## Editor

Responsible for:

- User Interface
- Editing tools
- Preview
- Settings management
- Media management

Editor communicates with Engine only through public interfaces.

Editor shall not contain core business logic.

---

## Sandbox

Responsible for:

- Engine verification
- Experimental testing
- Architecture validation
- Early-stage functional verification

Sandbox is NOT the final user interface.

Sandbox exists only as a development and testing environment.

---

## docs

Contains project documentation only.

No generated files.

---

## tests

Contains automated tests.

Production code shall not be placed here.

---

## assets

Contains built-in resources shipped with the application.

---

## data

Contains runtime configuration and user-generated data.

---

## tools

Contains development utilities.

Utilities shall never become runtime dependencies.

---

## third_party

Contains external libraries.

Third-party source code should remain unmodified whenever possible.

---

# 6. Dependency Rules

Allowed dependency direction:


Editor

↓

Engine

↓

Platform APIs


Reverse dependencies are prohibited.

Business logic shall never depend on UI code.

UI code shall never implement business logic.

Sandbox may depend on Engine for verification purposes.

Engine shall remain independently compilable.

---

# 7. Sandbox Development Policy

Sandbox is used during Engine development before the final Editor implementation.

The purpose of Sandbox is:

- Verify Engine APIs
- Test system behavior
- Validate architecture
- Provide minimal visualization

Sandbox shall prioritize simplicity.

---

## UI Strategy

During early development:

- Qt shall NOT be introduced.
- External GUI frameworks shall NOT be introduced.
- UI development is not a priority.

If visualization is required:

Use:

- Windows native APIs
- System-provided components
- Minimal test windows

The purpose is only functional verification.

UI complexity shall never delay Engine development.

---

## Future Editor

The final Editor module will be implemented independently.

Expected architecture:


Engine

↑

Editor(Qt)


The Engine module must never depend on Editor technology choices.

---

# 8. Prototype First Development Strategy

During early development, priority order is:

1. Architecture correctness
2. Module relationships
3. Interface design
4. Data flow
5. Ownership design
6. Function implementation details

The first goal is establishing:

- Class relationships
- Module boundaries
- API design
- Dependency direction
- Object lifetime

Complete implementation is not required during skeleton construction.

Example:

Allowed:

```cpp
class SceneManager
{
public:

    void loadScene();

private:

    std::vector<Scene> scenes_;
};

The complete file parsing logic may be implemented later.

9. File Organization

Each source file should have a single responsibility.

Recommended file size:

Header: < 500 lines
Source: < 1000 lines

Large files should be refactored.

10. Class Design

Classes should represent one concept only.

Avoid:

God Objects
Excessive inheritance
Unclear ownership
Hidden dependencies

Prefer composition over inheritance.

Virtual inheritance should only be introduced when clearly justified.

11. Function Design

Functions should:

Perform one task
Have descriptive names
Minimize side effects
Validate inputs
Return predictable results

Deep nesting should be avoided.

Early return is encouraged.

12. Naming Convention

Use descriptive English names.

Examples:

SceneManager

MediaLibrary

PlaybackController

CropRegion

Avoid abbreviations unless universally recognized.

Examples:

Allowed:

JSON

UUID

API

Avoid:

Mgr

Cfg

Tmp

Obj
13. Memory Safety Policy

C++ memory safety is a primary engineering concern.

All code shall prioritize:

Clear ownership
Predictable lifetime
Resource safety
Deterministic cleanup
Variable Initialization

All variables must be initialized before use.

Forbidden:

int count;

Preferred:

int count = 0;

Uninitialized variables are prohibited.

Pointer Usage

Raw pointers shall not be used for ownership management.

Forbidden:

Object* obj = new Object();

Preferred:

auto obj = std::make_unique<Object>();

or:

auto obj = std::make_shared<Object>();

when shared ownership is genuinely required.

Resource Management

All resources must have deterministic release.

Including:

Memory
Files
Handles
Threads
Locks

Prefer RAII design.

Lifetime Management

Every object should have:

Clear owner
Clear lifetime
Predictable destruction

Avoid:

Global mutable state
Hidden ownership
Uncontrolled singleton patterns
14. Error Handling

Errors shall never be silently ignored.

Every recoverable error should produce meaningful diagnostics.

Assertions should protect programming assumptions.

User-facing errors should remain understandable.

15. Logging

Logging should be meaningful.

Avoid excessive logging.

Every log entry should help debugging.

Logging should never replace proper error handling.

16. Configuration

Runtime behavior should be configurable.

Configuration belongs in JSON files rather than hardcoded constants whenever practical.

Business rules shall not be embedded in configuration files.

17. Implementation Decision Policy

When multiple implementations are possible, prioritize:

Correctness
Simplicity
Maintainability
Performance

Do not introduce unnecessary complexity for theoretical future requirements.

Examples:

Preferred:

Simple data structures
Clear ownership
Easy debugging

Avoid:

Premature optimization
Complex abstraction layers without actual requirements
Over-engineered systems
18. AI Collaboration Rules

Desktop Scene Engine is designed for long-term AI-assisted development.

AI-generated code shall:

Follow project architecture
Respect document boundaries
Preserve module responsibilities
Avoid unnecessary refactoring
Avoid introducing undocumented concepts
Maintain backward compatibility whenever practical

AI should modify the minimum amount of code required.

AI Development Workflow

Recommended workflow:

Requirement

↓

Architecture Analysis

↓

Implementation Plan

↓

Human Review

↓

Code Modification

↓

Build Verification

↓

Git Commit

AI should not directly perform large architectural changes without review.

19. Documentation Rules

Architecture changes require ADR.

New business concepts require updates to:

Data Model
JSON Specification (if applicable)
Glossary

Documentation shall remain synchronized with implementation.

20. Scope Boundaries

This document does not define:

Business models
JSON structures
Rendering algorithms
UI design
Platform implementation details

These belong to their respective documents.

Related Documents
CONSTITUTION.md
01_ARCHITECTURE.md
02_DATA_MODEL.md
04_JSON_SPECIFICATION.md
05_ROADMAP.md
ADR/
Status

This document is Stable (v1.1.0).

Future modifications shall follow the ADR process.

21. Coding Style

Code style should prioritize readability, consistency, and maintainability.

The following rules apply to all source code:

- Use consistent formatting throughout the project.
- Follow the existing code style when modifying existing files.
- Use meaningful indentation and line breaks.
- Avoid excessive empty lines or overly compressed code.
- Keep related logic together and separate unrelated logic clearly.
- Prefer clear and readable code over clever but difficult-to-understand solutions.
- Comments should explain design intent or important decisions, not repeat obvious code behavior.
- Avoid unnecessary comments that create maintenance burden.
- Keep functions and classes reasonably sized.
- Do not introduce formatting changes unrelated to the current task.

AI-generated code shall follow the existing project style and minimize unnecessary formatting changes.

End of Document