# Authored Project JSON v2

## Purpose

Authored Project JSON v2 is the portable persistence representation of
`Domain::AuthoredProject`. It preserves authored configuration and stable authored
identity. The serializer in this phase accepts and produces version `2.0` only.

Legacy Project JSON `1.0` remains a separate historical input format. Its migration
is handled by a later importer phase and is not part of the v2 object serializer.

## Root

Every current root field is required.

```json
{
  "type": "DesktopSceneEngineProject",
  "version": "2.0",
  "desktopCanvas": {
    "aspectRatio": {
      "numerator": 16,
      "denominator": 9
    }
  },
  "playlistPlaybackMode": "repeatCurrent",
  "playlists": []
}
```

`type` must be `DesktopSceneEngineProject` and `version` must be `2.0`.

## Canvas

```json
{
  "aspectRatio": {
    "numerator": 16,
    "denominator": 9
  }
}
```

The numerator and denominator are required positive JSON integers representable as
C++ `int`. A legal ratio is preserved exactly; it is not reduced during loading.
Physical display selection is not authored Canvas data and is not persisted.

## Playlist

```json
{
  "id": "11111111-1111-1111-1111-111111111111",
  "displayAlias": "Playlist",
  "enabled": true,
  "masterVolume": 1.0,
  "itemPlaybackMode": "listLoop",
  "backgroundMusic": {
    "enabled": true,
    "mode": "listLoop",
    "volume": 1.0,
    "tracks": []
  },
  "items": []
}
```

All Playlist fields are required. Array order is authored order; there is no
separate order or index field.

## Item

```json
{
  "id": "22222222-2222-2222-2222-222222222222",
  "displayAlias": "Item",
  "enabled": true,
  "mediaType": "image",
  "source": "C:/Media/image.png",
  "imageDurationSeconds": 10.0,
  "videoPlaybackRate": 1.0,
  "presentation": {
    "crop": {
      "x": 0.0,
      "y": 0.0,
      "width": 1.0,
      "height": 1.0
    },
    "layout": {
      "positionX": 0.0,
      "positionY": 0.0,
      "scaleX": 1.0,
      "scaleY": 1.0
    },
    "mediaPresentation": {
      "mode": "autoFill",
      "customAspect": {
        "numerator": 16,
        "denominator": 9
      }
    }
  },
  "itemMediaVolume": 1.0,
  "backgroundMusic": {
    "enabled": true,
    "mode": "listLoop",
    "volume": 1.0,
    "tracks": []
  }
}
```

All Item fields are required. `imageDurationSeconds` and `videoPlaybackRate` are
always written, even when one is inactive for the current media type. This preserves
the complete authored value. `customAspect` is likewise always written regardless of
the current presentation mode.

## Background music queue and track

Playlist global BGM and Item BGM use the same queue schema:

```json
{
  "enabled": true,
  "mode": "listLoop",
  "volume": 1.0,
  "tracks": [
    {
      "id": "33333333-3333-3333-3333-333333333333",
      "source": "C:/Music/song.m4a",
      "displayAlias": "Song",
      "volume": 0.75
    }
  ]
}
```

Queue `mode`, `volume`, and `tracks` are required. `enabled` is always emitted by the
writer, but the reader permits it to be absent and then defaults it to `true`. This
is the only optional-read field in the current v2 schema.

Every Track field is required. Track array order is queue order. Duplicate source
paths are legal and are not deduplicated; Track ID defines identity.

## Stable enum strings

Playback sequence modes:

| Domain value | JSON value |
| --- | --- |
| `ListLoop` | `listLoop` |
| `Random` | `random` |
| `RepeatCurrent` | `repeatCurrent` |

Media types:

| Domain value | JSON value |
| --- | --- |
| `Image` | `image` |
| `Video` | `video` |

Presentation modes:

| Domain value | JSON value |
| --- | --- |
| `Native` | `native` |
| `AutoFill` | `autoFill` |
| `Custom` | `custom` |

Music playback modes:

| Domain value | JSON value |
| --- | --- |
| `CurrentSingleLoop` | `currentSingleLoop` |
| `ListLoop` | `listLoop` |
| `Shuffle` | `shuffle` |

Enum ordinals are never persisted. Unknown enum strings are rejected.

## Identity

Playlist, Item, and Track IDs are canonical lowercase UUID strings:

- exactly 36 characters;
- lowercase hexadecimal digits;
- hyphens at positions 8, 13, 18, and 23;
- no braces.

Valid loaded values are preserved exactly and are never regenerated or normalized.
The following uniqueness rules apply independently:

- Playlist IDs are unique across the project;
- Item IDs are unique across every Playlist;
- Track IDs are unique across all Playlist and Item BGM queues.

The same textual UUID may be used by different strong ID types. Duplicate media or
BGM source strings are legal.

## Validation

Invalid persisted and in-memory authored values are rejected rather than clamped or
normalized.

- Playlist master, Item media, queue, and Track volumes must be finite and within
  `[0.0, 1.0]`.
- Image duration must be finite and greater than zero.
- Video playback rate must be finite and within `[0.5, 2.0]`. Persistence does not
  impose a 0.1 step.
- Crop values must be finite. X and Y are non-negative, width and height are positive,
  and the normalized right and bottom edges must not exceed `1.0`.
- Layout positions must be finite. Layout scales must be finite and positive.
- Canvas and custom presentation aspect numerator and denominator must be positive.
- Authored aliases and source strings must contain valid UTF-8. No locale conversion
  or malformed-sequence substitution is performed.

All current fields are required except queue `enabled` as described above. A known
field with the wrong JSON type is rejected. Unknown object keys are ignored so that
future additive fields can be introduced deliberately.

The object-level `fromAuthoredProjectJson()` API receives an already constructed
`nlohmann::json` value. Duplicate textual keys may already have been collapsed by the
text parser and therefore cannot be detected at this layer. Duplicate-key rejection
is enforced by the authored ProjectFile text-parsing boundary before version dispatch.

## Persistence boundary

JSON v2 does not persist runtime, presentation-target, or editor-session state. It
therefore excludes:

- selected or active Playlist/Item;
- current Track ID;
- playback time, decoder state, queue cursor, shuffle state, or lifecycle tokens;
- physical Display Target;
- window geometry, language, or other UI preferences;
- Player-only Extended Media Layers.

The BGM `enabled` field is authored configuration only in this phase. Runtime/UI
consumption remains outside this serializer contract.
