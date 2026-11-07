# Drawborad headless runtime — Ubuntu 24.04 / Debian 13 x86_64

Download the matching `ubuntu24.04-x86_64` or `debian13-x86_64` tarball and extract it into a directory you own. The two executables are compiled against their own distribution's Qt, glibc and JPEG ABI. Do not move the Ubuntu binary to Debian or alias `libjpeg.so.8` to `libjpeg.so.62`.

Python 3 is required for preflight. Bootstrap it from the distribution's official repositories if missing, then verify the manifest before executing the engine. Install system dependencies only with appropriate permission:

```sh
sha256sum -c SHA256SUMS
python3 runtime_platform.py check-bundle "$PWD/runtime-target.json"
sudo bash install-runtime-linux.sh
./DrawboradHeadless --help
./DrawboradHeadless smoke --output "$HOME/drawborad-smoke"
python3 verify_artifacts.py "$HOME/drawborad-smoke"
```

Output must be a new directory. `smoke` uses a labelled synthetic two-colour fixture and runs the native C++ reconstruction pipeline. It produces project.draw, zero.png, middle.png, final.png, painted.ora and report.json.

For existing source/mask data conforming to the semantic manifest schema:

```sh
./DrawboradHeadless run --manifest /absolute/path/manifest.json --output /absolute/path/new-output --accept-all
```

This runtime imports masks; it does not install a SAM model or execute SAM inference. The real coffee source/model and professional fidelity/process review remain outstanding. It has no desktop GUI. See readiness.json for exact build provenance, passed and unavailable tests. SHA256SUMS checks package contents. The fresh-runtime CI job tests the extracted executable outside the source/build tree.


`install-runtime-ubuntu.sh` remains as a compatibility entry point to the distro-aware installer. Neither installer repairs a read-only filesystem. The native editor (browser interface) has a separate bundle and startup guide in `tools/packaging/native-editor/README.md`. These changes do not alter the Windows desktop build.
