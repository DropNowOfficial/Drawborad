# Complete manual renderer-link audit after second CI failure

## Incident and correction of scope

845c455 fixed automatic-stage and two flow outputs, but Linux CI43425269812 then failed at hybrid native linking (Ubuntu131885030451 and Debian131885031035). Both log errors name missing chiselInkCoverage. The earlier statement that three targets covered all independent native builds was incorrect: the search missed module arrays without a .cpp suffix and fixed-script validation repeated that blind spot.

Read-only inventory of repository compile commands and CI entry points found:
- automatic_stages/build_native.sh: one native-plan output.
- flow_planner/build.sh: libflowfeedback.so and native-flow, separate link invocations.
- hybrid_planner/build.sh: native-hybrid (also invokes flow build; that does not repair its own link list).
- locality_reorder/build.sh: native-locality.
- tools/artwork/README.md: two explicit g++ lists for CoverRegions and DryInkStudy; FaceStudy instructions inherit the Cover list.
- CMake includes the dependency. Docker and Windows build via CMake, rather than a further independent literal source list.

This revision fixes the two missed experimental module lists and documented commands. The existing test now discovers renderer-consuming shell scripts and documented compiler commands rather than maintaining a two-file whitelist. Per-target parsing must reject unsupported layouts and cannot let one output's sources satisfy another. Source lists include coverage dependencies directly discovered from the dispatcher's includes. This remains a deliberately restricted parser, not a full shell interpreter.

## Evidence

Before the two module fixes, dynamic discovery test reported both hybrid and locality omissions. Real native builds and the four matching experiment suites, followed by the complete Release regression suite, are required before this checkpoint is called locally verified. Remote full workflows are separate and must complete before queued feature publication. Current Windows/physical-device behavior has not been qualified by this audit.

## Queue safety

Do not publish359b3d6 or22b490f directly on the failing parent. Rebuild/reparent their features onto the repaired tree, including ContinuousEraserCoverage in every independent source list when engine22 is introduced, and rerun the guard and actual links. Frozen old checkpoints remain preserved. No merge or private artwork upload is part of this repair.

## Local execution evidence

All five native experimental outputs rebuilt from their scripts; hybrid explicitly rebuilt both flow outputs. Matching tests passed: automatic15/15 (4.151s), flow15/15 (2.830s), hybrid15/15 (2.711s), locality20/20 (0.567s). Independent review added guard negative cases for inline-comment false dependencies, unknown second compiler targets masked by a valid modules array, new dynamic scripts, continuation/brace lists, and removing each required dependency from each actual target. Guard10/10 passed. This is local evidence, not a claim that remote complete CI has passed.

Fresh Release build succeeded and complete109/109 CTest passed in32.73s, including actual Qt software canvas20.83s. The existing bounds Binding loop warnings remain on this repair parent; the separately queued nib-guide revision fixes them. This repair does not claim those warnings are gone.

The three documented example targets also linked successfully: BuildDryInkStudy, BuildCoverRegions and BuildFaceStudy, using the installed toolchain PNG include/library paths for the latter two. They were only compiled, never run to produce artwork. All eight audited independent outputs therefore have actual local linker evidence in addition to the static list check.
