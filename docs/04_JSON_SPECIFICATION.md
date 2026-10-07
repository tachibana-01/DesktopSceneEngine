# Desktop Scene Engine JSON Specification

Version: 1.0.0

Status: Stable

Last Updated: 2026-07-23

---

# 1. Purpose

This document defines the JSON serialization specification used throughout Desktop Scene Engine.

The purpose of this specification is to ensure that all JSON files within the project follow a unified format, naming convention, compatibility strategy, and serialization behavior.

This document defines **how data is represented**, not **what the data means**.

Business concepts are defined in **02_DATA_MODEL.md**.

---

# 2. Design Principles

All JSON files shall follow the principles below.

## Human Readability

JSON files should remain easy to read and edit manually.

---

## AI Readability

JSON structures should be explicit, predictable, and deterministic to improve AI-assisted development.

---

## Stable Structure

The overall structure of a JSON document should remain stable across versions whenever possible.

---

## Data Model Mirror

JSON is a serialization format only.

It mirrors the logical data model defined in **02_DATA_MODEL.md** and must never introduce new business concepts.

---

## Storage Independence

The logical data model must remain independent of JSON.

Future storage technologies (SQLite, databases, binary formats, etc.) should be able to replace JSON without changing business logic.

---

# 3. General Rules

## Encoding

All JSON files shall use UTF-8 encoding.

---

## Indentation

Indentation shall use four spaces.

Tabs are prohibited.

---

## Line Endings

LF is recommended.

CRLF is acceptable on Windows.

---

## File Extension

All JSON files shall use the `.json` extension.

---

## Comments

JSON comments are prohibited.

Documentation belongs in Markdown rather than JSON files.

---

## Trailing Commas

Trailing commas are prohibited.

---

## Duplicate Keys

Duplicate keys are prohibited.

---

# 4. Root Object

Every JSON document shall contain a single root object.

A version field is mandatory.

Example:

```json
{
    "version": "1.0"
}
```

An object type field is recommended whenever the JSON file represents a business object.

Example:

```json
{
    "version": "1.0",
    "type": "Scene"
}
```

Additional fields are defined by the corresponding business object specification.

---

# 5. Naming Convention

JSON keys shall use **camelCase**.

Examples:

```text
mediaPath
frameRate
loopMode
backgroundColor
```

The following naming styles are prohibited:

- snake_case
- PascalCase
- Hungarian notation

Abbreviations should be avoided unless officially defined.

---

# 6. Value Rules

## Boolean

Use JSON boolean values.

```json
true
false
```

Never use numeric values such as `0` or `1` to represent booleans.

---

## Numbers

Use integer values whenever possible.

Floating-point values should only be used when fractional precision is required.

---

## Strings

Strings shall use UTF-8 encoding.

Leading and trailing whitespace should be avoided unless meaningful.

---

## Arrays

Arrays preserve order.

If ordering has no semantic meaning, it should still remain deterministic during serialization.

---

## Null Values

Fields should be omitted whenever possible instead of using `null`.

Use `null` only when it carries explicit business meaning.

---

# 7. Serialization Rules

## Deterministic Serialization

Serializing the same object multiple times shall always produce identical JSON output.

---

## Stable Field Ordering

Fields should appear in a consistent order.

Recommended order:

1. version
2. type
3. id
4. name
5. Remaining business fields
6. Nested objects
7. Collections

Stable ordering improves:

- Git diff readability
- Merge conflict resolution
- Debugging
- AI understanding

---

## Consistent Formatting

Formatting rules shall remain identical across all project-generated JSON files.

Manual formatting differences should be avoided.

---

# 8. Compatibility Rules

## Versioning

Every JSON document shall contain a version field.

The version represents the serialization format rather than the application version.

---

## Forward Compatibility

Readers should ignore unknown fields whenever possible.

---

## Backward Compatibility

Breaking changes should be avoided.

When incompatible changes become necessary, a new serialization version shall be introduced.

---

## Migration

Older JSON versions may be migrated automatically by the Engine.

Migration logic belongs to the Engine implementation rather than the JSON specification.

---

# 9. Validation Rules

Before loading a JSON document, the following validations shall be performed:

- JSON syntax validation
- Required field validation
- Version validation
- Object type validation (if applicable)

Invalid documents shall not be partially loaded.

Validation failures should produce clear and descriptive error messages.

---

# 10. Scope Boundaries

This document intentionally does not define:

- Business entities
- Entity relationships
- Object ownership
- Runtime behavior
- Class design
- API design
- Source code implementation
- File system layout

These topics are specified in their corresponding documentation.

---

# Related Documents

- CONSTITUTION.md
- 01_ARCHITECTURE.md
- 02_DATA_MODEL.md
- 03_DEVELOPMENT_GUIDE.md

---

# Status

This document is **Frozen (v1.0)**.

Future modifications shall follow the ADR (Architecture Decision Record) process.

---

End of Document