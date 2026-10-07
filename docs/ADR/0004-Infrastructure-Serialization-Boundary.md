# ADR-0004: Infrastructure Serialization Boundary

Status: Accepted

Date: 2026-08-07

Related Documents:

- CONSTITUTION.md
- 01_ARCHITECTURE.md
- 02_DATA_MODEL.md
- 03_DEVELOPMENT_GUIDE.md
- ADR/0001-Architecture.md
- ADR/0002-Engine-Lifecycle-Contract.md
- ADR/0003-Minimal-Core-Architecture.md

---

# Context

Desktop Scene Engine has completed the initial Domain Model foundation.

The current architecture contains:


Engine
├── Lifecycle Coordination
│ ├── initialize()
│ ├── update()
│ └── shutdown()
│
└── Domain
├── Value Types
│ ├── EntityId
│ ├── Crop
│ ├── Layout
│ └── Playback
│
└── Entities / Aggregates
├── Media
├── Scene
├── Playlist
├── Library
└── Project


The Domain layer currently contains only pure data-oriented models.

The project now requires a mechanism to:

- Load project data from external files.
- Save project data to external files.
- Convert between persistent representations and Domain objects.

The current planned data format is JSON.

However, serialization introduces a dependency boundary problem.

A naive implementation could make Domain objects directly depend on JSON:


Domain
|
v
JSON Library


For example:

```cpp
class Scene
{
    void serialize(json& data);
};

This would couple business concepts with storage format and make future changes difficult.

The project requires a clear separation between:

Domain concepts
External representations
Storage formats
Serialization implementations
Decision

Serialization shall belong to the Infrastructure layer.

The dependency direction shall be:

Infrastructure
        |
        v
     Domain

Infrastructure may depend on Domain types.

Domain must not depend on:

JSON libraries
File systems
Serialization formats
Database systems
External storage mechanisms
1. Domain Responsibility

The Domain layer is responsible for:

Representing project concepts.
Maintaining ownership relationships.
Providing data structures used by runtime systems.

Domain objects shall not:

Load themselves from files.
Save themselves to files.
Know JSON formats.
Contain serialization methods.
Depend on external libraries.

Example:

Allowed:

Scene scene;
scene.mediaId = EntityId("media001");

Not allowed:

Scene scene;

scene.loadFromJson(file);

or:

scene.toJson();
2. Infrastructure Serialization Responsibility

The Infrastructure layer is responsible for:

Reading external data.
Writing external data.
Mapping external representations to Domain objects.
Mapping Domain objects to external representations.
Handling serialization format details.

Future structure:

Engine
├── include
│   └── DesktopSceneEngine
│
├── Domain
│
└── Infrastructure
    └── Json
        ├── Loader
        └── Serializer

Infrastructure owns:

JSON
File IO
Version Handling
Format Validation
3. Serialization Direction

The intended data flow:

Loading:

JSON File
    |
    v
Json Loader
    |
    v
Domain Objects
    |
    v
Project

Saving:

Project
    |
    v
Json Serializer
    |
    v
JSON File

Domain objects remain unaware of this process.

4. JSON Library Boundary

A JSON library may only appear inside Infrastructure code.

Allowed:

Infrastructure
    |
    +-- nlohmann::json
    |
    +-- Domain headers

Forbidden:

Domain
    |
    +-- nlohmann::json

Public Domain headers must remain independent from:

JSON headers.
Platform headers.
Qt headers.
Rendering APIs.
Media libraries.
5. Versioning Responsibility

Serialization formats require version handling.

The JSON format shall contain version information.

Example:

{
    "version": "1.0",
    "project": {}
}

JSON schema versions use string representation because they describe schema and protocol evolution rather than numeric values. String versions support semantic progression such as `"1.0"`, `"1.1"`, and `"2.0"`.

Version compatibility checking and migration belong to Infrastructure.

Domain objects should not contain:

int serializationVersion;

or:

migrateFromVersion();

Migration logic belongs outside Domain.

6. Project Serialization Scope

The first serialization implementation shall support:

Project
├── Library
│   └── Media
├── Scene[]
└── Playlist[]

Including:

Media
EntityId
Basic metadata
Resource reference
Scene
EntityId
Media reference
Crop
Layout
Playback
Playlist
EntityId
Ordered Scene references
Library
Media collection
Project
Aggregate root data
7. Deferred Concepts

The following are intentionally excluded from the first serialization phase:

History

Reason:

No concrete history behavior exists yet.

Favorites

Reason:

The ownership model and usage requirements are not finalized.

Settings

Reason:

Settings represent application configuration rather than Project domain data.

Ownership between:

Application
Project
User Profile

requires future clarification.

Repository Pattern

Repository abstraction is intentionally deferred.

Current requirement:

JSON File
    |
Loader
    |
Project

is sufficient.

Introducing:

IProjectRepository
        |
ProjectRepository

would add abstraction without multiple storage implementations.

Consequences
Positive
Domain remains independent and reusable.
Serialization format can change without modifying Domain.
JSON implementation can evolve independently.
Runtime systems can consume stable Domain objects.
Future storage options remain possible.
Negative
Additional mapping code is required.
Infrastructure must maintain conversion logic.
Domain changes may require serializer updates.

These costs are accepted because clear dependency boundaries are more important than minimizing initial code size.

Alternatives Considered
Alternative A - Domain owns serialization

Description:

Each Domain class implements JSON conversion.

Example:

Scene::toJson();
Scene::fromJson();

Decision:

Rejected.

Reason:

Would couple Domain concepts with external formats and violate layer separation.

Alternative B - Global Serialization Manager

Description:

Create a central manager responsible for all serialization.

Example:

SerializationManager

Decision:

Rejected.

Reason:

A global manager would become a service container and accumulate unrelated responsibilities.

Alternative C - Repository Pattern Immediately

Description:

Introduce repository interfaces before storage requirements exist.

Decision:

Rejected.

Reason:

Current requirements only need JSON file persistence. Additional abstraction would be premature.

Future Rules

Future serialization implementations shall follow:

Domain types remain serialization-agnostic.
Infrastructure owns all external format knowledge.
JSON types never appear in Domain headers.
Serialization does not execute runtime behavior.
Loading data does not automatically start playback.
Persistence logic does not become part of Engine lifecycle.
Status

This ADR is a Draft and requires architectural review before implementation.

End of Document
