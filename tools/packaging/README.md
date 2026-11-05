# Drawborad headless runtime — Ubuntu 24.04 x86_64

Extract the tarball into a directory you own. Install runtime dependencies once:

```sh
sudo bash install-runtime-ubuntu.sh
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
