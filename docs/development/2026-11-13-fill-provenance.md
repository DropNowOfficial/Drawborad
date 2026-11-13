# Closed-ink provenance checkpoint, not selective rework

## User-visible purpose
Changing an earlier line after a fill currently hits a deliberate dependency barrier. Undoing through later marks and then editing clears redo. This stage records new closed-ink operation membership and its input fingerprints so future rework does not guess groups by color/time. It does **not** add selective withdrawal, automatic recomputation, or whole-layer clearing. No artwork quality claim follows from these engineering changes.

## Atomic and persistent scope
New closed-ink output includes its bounded group record in the same transaction as the generated paint events. Old fills retain no fabricated metadata. Original events, source bytes, and unrelated later hand strokes remain unchanged. Related boundary/member content is protected; ordinary layer display/lock changes may retain the historical record without making it a current replay authorization. Context fingerprints describe creation-time conditions, not a promise those conditions still hold. Self-consistent hashes are not proof of trusted authorship or the producer's artistic accuracy.

The record table is document content: equality, Undo/redo, canonical hashes and versioned serialization include it. New format generation 20 is required when records exist. Legacy readers explicitly reject it rather than silently discard information. Existing older-format paths and mixed-generation journals remain protected; the user must save a clean new `.draw` copy and retain the original. Preview and status text warn before acceptance, and save failures explain the new-name requirement. No implicit overwrite, migration or downgrade is performed.

## Deliberate limits
Only newly generated bounded closed-native-ink operation records are added. Other legacy/derived buckets remain causally unknown. Records are not a bypass around the old editing barrier. This stage is groundwork, not completion of the user's complete layered artwork or proof that an arbitrary flood operation can be safely undone independently.

## Verification
Final local Release suite 107/107 passed (20.60 seconds). Actual Qt/QML verifies source notice, original-file overwrite refusal with unchanged bytes, clean-copy save, Undo/redo and reopened provenance. Independent read-only review found no blocking issue. A real headless session opens format20 and saves a copy; independent native recovery compares complete content and record table exactly. PNG/ORA are rendered exports, not native source-record backups. A standalone reader compiled from the previous version rejects a new recorded fixture with `project.engine-version`; the file hash remains unchanged. Private reference images are not included.
