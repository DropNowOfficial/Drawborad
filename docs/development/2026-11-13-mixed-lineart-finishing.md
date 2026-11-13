# Controlled lineart plus native finishing strokes

## Goal and limits
The user needs a real hand-drawing / region-by-region coloring workflow, not recoloring a flattened illustration. This change lets native dry ink and ordinary brush/eraser strokes coexist with the already controlled original-lineart, hollowing, gap-closing and bucket workflow. It does not reconstruct lost image details or create an artist-quality cover/comic. All acceptance images are technical fixtures; private reference images are not uploaded.

## Layer semantics
- The locked original raster remains byte-for-byte untouched. Existing lower color, hollow/gap correction and optional upper hollow color retain their meanings.
- An explicit **hand-drawn details (not fill boundaries)** button adds a root-level native finishing layer above the controlled stack. Up to 16 such layers can be added. The accepted bucket command retains the selected finishing layer rather than silently switching subsequent drawing onto a controlled color layer.
- These upper strokes are visual finishing only. They do NOT close fill gaps or become sampled region boundaries. Use the explicit controlled gap tool for barriers. General imported/arbitrary layer stacks, grouping, compositing modes, transforms and provenance are not silently accepted.
- A projection validates the full document and temporarily excludes only the strict finishing suffix while existing controlled helpers operate. Restoring a command inserts new controlled layers below finishing layers and rebases new paint times after the entire original timeline. Original events/resources are preserved.

## Safety
The facade caps nodes at 4096, events at 8192, total geometry at one million points, dimensions at 8192 per axis / 20 million pixels, and accepts only supported finishing engines. Cancellation propagates through validation. A candidate is applied in a temporary document and revalidated before publication. Root-level ordinary brush compilation uses an explicit opt-in; existing callers still reject empty object IDs by default. Native detail strokes preserve their real empty object ID, UUID, pressure and relative sample times rather than inventing an object association.

On release, finishing strokes use asynchronous candidate validation and render-before-commit, including ordinary brush/eraser. Busy guards, cancellation, revision/generation checks and save consistency are shared with dry ink. Rendering may still take noticeable time; no universal fluidity or total process-memory promise.

## Acceptance status
Core B38 covers strict structure, budgets, rejection, event and resource preservation, ordering, ordinary root compile opt-in, apply/Undo and mixed command adaptation. The actual Qt/QML test exercises adding detail layers, dry drawing, lower fill, hollow, upper fill, explicit gap, another detail layer with ordinary drawing, further fill, Undo/redo, visibility, preview/cancel and exact save/reopen snapshot plus render. Final reviewed Release build passed 104/104 tests (parallel -j4, 20.17 s wall time). A prior final targeted Qt/QML pass took 19.55 s; the reviewed full suite also includes actual pointer rejection of a renderable but malformed finishing layer, leaving the original snapshot unchanged. Review found and fixed a cumulative-budget/strict-structure omission by validating the complete candidate before rendering and committing. Existing baseline QML binding-loop warnings remain; this is not a warning-free claim.

Windows and physical tablet hardware are not tested. This is useful editing capability, not a finished artwork or proof of natural artistic interpretation. Continue reporting stages and retaining the remaining quality gap explicitly.
