# Authored Project File I/O

## Purpose

The authored ProjectFile boundary converts UTF-8 JSON file text to and from
`Domain::AuthoredProject`. It owns file reading, textual duplicate-key rejection,
serialization-version dispatch, and atomic authored-file saving. It does not own
Player document lifecycle or runtime state.

## Loading

`loadAuthoredProjectFile()` reads the complete file before parsing it. A valid UTF-8
byte-order mark is accepted through nlohmann-json's normal parser behavior. Empty,
malformed, unreadable, and non-UTF-8 documents reject with file-path context.

Textual duplicate keys reject at every JSON object depth before nlohmann-json can
collapse them into its object representation. The root must be an object with a
string `version` field. Dispatch is exact:

- `1.0` is parsed by the legacy v1 JSON parser and converted by
  `LegacyProjectImporter`;
- `2.0` is parsed by the authored JSON v2 parser;
- every other version rejects.

The ProjectFile boundary does not duplicate either schema or migration policy.

## Authored saving

`saveAuthoredProjectFile()` writes JSON v2 only. Authored validation, JSON construction,
and four-space-indented text serialization complete in memory before any filesystem
mutation begins.

The serialized text is written to an exclusively created sibling temporary file. The
writer handles partial `WriteFile` progress, flushes the completed file with
`FlushFileBuffers`, and checks `CloseHandle` before finalization. It first uses
`ReplaceFileW` for an existing target. Only `ERROR_FILE_NOT_FOUND` enters the
missing-target `MoveFileExW` path, without `MOVEFILE_REPLACE_EXISTING`.

There is no direct authored-target truncation and no delete-target-first fallback.
Exact temporary-file cleanup is best effort after failure and is disarmed after
successful finalization. Missing parent directories are not created automatically.

## Failure and durability limits

Validation, serialization, temporary creation, writing, flushing, and closing failures
occur before finalization and therefore do not modify an existing target. Under the
tested Windows sharing semantics, an ordinary locked-target replacement failure also
preserves the existing target.

This contract does not claim that the old target remains at its original path after
every documented exceptional partial-state failure of `ReplaceFileW`. It also does not
guarantee survival from hardware failure, filesystem corruption, controller-cache
loss, or unflushed directory metadata.

## Legacy API

The existing `saveProjectFile()` and `loadProjectFile()` APIs retain their legacy v1
behavior. In particular, B3 does not migrate legacy saving to the authored atomic
writer. Authored loading accepts v1 only through the existing parser and importer.

## Excluded lifecycle state

ProjectFile I/O does not persist or coordinate current document paths, dirty state,
Open/Save UI, close confirmation, autosave, QSettings, physical Display Target, or
runtime playback state.
