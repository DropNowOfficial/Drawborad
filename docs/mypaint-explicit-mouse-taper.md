# Explicit MyPaint mouse tip assistance — locally accepted WIP

Goal: allow mouse users to choose spatial start/end taper without pretending it is measured pen pressure. This corrects a practical control gap, not anatomy, composition or artwork quality.

Default fraction0 preserves the existing constant-pressure1 mouse policy. Enabled range(0,0.5] centrally normalizes raw mouse pressure to1 before applying the existing arc-length envelope. Callers cannot supply arbitrary pre-tapered mouse pressure as measured input. Tablet inputs with a nonzero mouse-taper request are rejected; desktop tablet events always send0 and retain measured pressure/time. The MyPaint gesture captures assistance at press. Changing the next-stroke preference through the core API mid-gesture does not alter that captured value. The disabled assistance control rejects a synthetic moved signal and restores its binding during drawing or pending review; this is not a claim that every core settings API is disabled.

The original and effective input arrays, policy version and fraction are recorded. Effective samples undergo the same finite/coordinate/time/pressure checks and1024point budget; expansion beyond the cap is rejected, not truncated. Native project pixels and provenance remain stored for worker-free reopening. No preset hash, installed library or old project content is changed.

Initial UI verification exposed two real omissions: the disabled slider handler could still change settings when invoked programmatically, and the separate MyPaint press branch did not capture the selected assistance value. Both were fixed without removing the assertions. The focused adapter, MyPaint UI and persistent-layer UI then passed 3/3 (8.83s). Additional regressions now exercise mid-gesture next-setting changes and assisted-stroke provenance/pixel save-recovery. The final full Release build succeeded and all 134 local CTest tests passed (67.29s), including those added regression checks. This is Linux software/worker acceptance, not remote CI or physical input acceptance.

A bounded24stroke eye-region comparison using the actual new adapter and explicit mouseTaper0.25 restores pointed endpoints. It remains an artistically unacceptable study: stroke bellies/negative spaces and iris anatomy are wrong. Reference was visual-only, no reference pixels or model output were copied. Software-authored mouse paths are not physical hand drawing. The study is private and excluded from GitHub.

Windows, physical tablet behavior and live MyPaint ink while dragging remain unverified/unsupported as previously documented. Remote CI remains billing-blocked, not retried; no billing change or merge.

Timing preservation means the original motion-time mapping, not identical sample count: the bounded helper may insert ramp knots or compact exactly redundant samples. Raw samples remain separately recorded.

Final focused stability check: the original MyPaint UI and persistent-layer UI each passed three consecutive runs (six executions total, 13.46s). No retry waiver was applied to the earlier failures; their causes and fixes are recorded above.
