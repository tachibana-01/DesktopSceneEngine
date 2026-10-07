# Legacy v1 Project Import

## Purpose

`LegacyProjectImporter` converts the historical JSON v1 domain model,
`Domain::Project`, into the canonical `Domain::AuthoredProject` model. This is an
in-memory model conversion only. PERSIST-B2 does not read files, dispatch JSON
versions, or save projects.

## Identity

Legacy textual IDs remain lookup keys and diagnostic context; they are not reused as
authored identity. Every imported Playlist and every Scene occurrence receives a new
random canonical lowercase UUID. Importing the same v1 project twice may therefore
produce different authored IDs. Once an imported project is saved and loaded through
JSON v2, those first generated IDs are preserved exactly.

## Playlist and Scene mapping

Legacy Playlist order and each Playlist's Scene-reference order are preserved. Each
legacy Playlist becomes one authored Playlist. Every Scene reference occurrence
becomes a separately owned authored Item, so repeated or shared Scene references
produce distinct Items with distinct IDs.

Scenes referenced by no Playlist are retained, in legacy Scene-vector order, in one
final Playlist named `Imported Unreferenced Scenes`. The synthetic Playlist is omitted
when every Scene is referenced. Media referenced by no Scene cannot be represented in
the authored model and rejects the import. Missing references, duplicate IDs, and empty
legacy IDs also reject rather than being skipped or guessed.

## Media mapping

The source is copied byte-for-byte and must contain meaningful non-whitespace UTF-8
text. A meaningful Media name is copied exactly as the Item alias; otherwise the
legacy Scene ID is used. A whitespace-only fallback rejects.

Media type is inferred case-insensitively from the filesystem-style source extension:

- Image: `.png`, `.jpg`, `.jpeg`, `.webp`, or no extension;
- Video: `.mp4`, `.avi`, `.mov`, `.mkv`, `.webm`, `.video`.

Other extensions reject. This is intentionally stricter than the raw Engine path that
may attempt an unknown extension as an image; durable authored media type is never
guessed from unsupported input.

For Images, a positive legacy duration is preserved and zero uses the authored
10-second default. Video duration is validated but does not become authored timing;
the decoder remains authoritative. Video speed is preserved within `[0.5, 2.0]`.
Legacy Image speed has no authored equivalent: a finite positive value is accepted,
but the authored Video-rate field remains at its `1.0` default. Scene volume is
preserved as Item media volume for both media types.

Legacy Crop and Layout are preserved exactly when valid. JSON v1 never persisted
MediaPresentation, so imported Items intentionally use the current `AutoFill` and
16:9 presentation defaults even if an in-memory legacy Scene carries another value.

## Intentional defaults and losses

Legacy per-Scene loop state has no authored Item equivalent and is not translated into
project or Playlist sequence modes. The project keeps `RepeatCurrent`; each Playlist
keeps `ListLoop`. Imported Playlists and Items remain enabled, volumes retain authored
defaults unless explicitly mapped, and both Background Music queues remain enabled,
empty, `ListLoop`, and at volume `1.0`.

No Background Music track identity or runtime state is created by legacy import.
ProjectFile support, v1/v2 version dispatch, atomic saving, Player Open/Save UI, dirty
state, and QSettings integration remain outside PERSIST-B2.
