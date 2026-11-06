# Real Photo Review Board Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans and test-driven-development. This is a narrow QA addition to the approved Linux cloud architecture.

**Goal:** Emit a human-review PNG and metrics JSON after each real-photo CI run.

**Architecture:** Keep the existing C++ drawing engine and .draw authoritative. Read generated artifacts with Python/Pillow; never substitute cutouts for painted output.

**Tech stack:** Python 3, Pillow, unittest, GitHub Actions Ubuntu 24.04.

**Spec:** docs/superpowers/specs/2026-10-29-linux-cloud-architecture-design.md

## Constraints
- Build on work/linux-headless-engine-20261104. Do not modify main or merge existing draft PRs.
- Do not claim SAM3 inference or segmentation IoU; foreground count is only a diagnostic.
- Fail on mismatched dimensions and missing artifacts.

### Task 1: Board implementation
- [x] RED test: verify normalized RGB MAE, alpha coverage, valid PNG, and JSON.
- [x] RED test: dimension mismatch is rejected without output.
- [x] GREEN: implement real_photo_review.py and validate fixture.
- [x] Independent baseline check: current Chelsea photo MAE 0.024490336499183005, matching C++ report 0.024490336499181472.

### Task 2: CI wiring
- [x] Run QA unit tests in the existing Linux build job.
- [x] Generate real-photo-review.png/.json from existing real-input and real-painting files.
- [x] Include the board in existing uploaded evidence.
- [ ] Review full remote CI on exact new PR head. Until then, do not claim remote success.
