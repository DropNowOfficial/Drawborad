# Desktop performance investigation (not a responsiveness acceptance)

The prior 38-second poster journey was a **Debug** build and included project file
construction, two saves and two opens. Split measurements from the same cloud host:

| Phase | Debug milliseconds, one sample |
|---|---:|
| Synthetic fixture construction | 3760 |
| Initial save | 7313 |
| Open including display composite | 8000 |
| Display / screenshot including 300ms wait | 305 |
| Add empty layer including composite | 786 |
| Mouse stroke including composite | 795 |
| Stroke screenshot including 200ms wait | 208 |
| Painted save | 8252 |
| Painted reopen including composite | 8110 |

These are diagnostic samples, not a benchmark distribution. Most elapsed time is
in save/open, not screenshot rendering; Debug numbers cannot be advertised as
release throughput. Project validation/hash integrity and recovery checks remain
unchanged. A separate Release build is measured with the same actual QML fixture.

Low-risk display change: convert the immutable display composite to premultiplied
RGBA once per snapshot refresh, and share the full image when creating the software
texture. Previously every pan/resize copied and converted another full 67MB frame.
GPU subtiles still copy their pixel rectangle. Native source layers remain untouched.
QImage implicit sharing keeps pixels alive until the scene-graph texture is created.

Next safety requirements before asynchronous work: serialize open/save operations,
keep a generation token for late results, retain immutable snapshot lifetimes, expose
busy/cancel/failure states, avoid writes from stale documents, and preserve atomic
save/recovery behavior. Rendering and file I/O are still synchronous in this patch;
no claim of fluid interaction or Windows/D3D11 acceptance is made.

Memory caveat: Qt may allocate its own texture storage; this is not end-to-end
zero-copy. Old cached frames, incoming straight RGBA and new premultiplied RGBA can
coexist during replacement. The compositor's 128MiB incremental budget is not a
whole-application/display memory limit.

## Separate Release build, same actual-QML poster fixture

With display caching enabled, one local Release sample passed all QML, screenshot,
source-preservation, save/reopen and pan checks:

| Phase | Release milliseconds |
|---|---:|
| Fixture | 433 |
| Initial save | 714 |
| Open + composite | 813 |
| Add empty layer + composite | 101 |
| Mouse stroke + composite | 128 |
| Painted save | 670 |
| Painted reopen + composite | 959 |
| Four pans + screenshots | 7 |

This compares build configurations, not an isolated before/after speedup claim.
It is one synthetic fixture on one cloud CPU/software renderer. Larger stacks,
complex strokes, GPU backends and Windows may differ. File operations still block
the GUI for hundreds of milliseconds; asynchronous operation work remains open.
