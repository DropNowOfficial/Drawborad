"""Run the same browser acceptance against the unpacked, foreground Linux launcher."""
import os
from pathlib import Path
import signal
import socket
import stat
import subprocess
import sys
from tempfile import TemporaryDirectory
import time
from urllib.error import URLError
from urllib.request import build_opener, ProxyHandler

from browser_native_acceptance import verify_in_chromium


def main(source, output):
    assert os.geteuid() != 0, "Exercise a normal user's cloud session"
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    url = f"http://127.0.0.1:{port}"
    http = build_opener(ProxyHandler({}))
    launcher = ["bash", "start-cloud-session.sh", "--install-deps"]
    with TemporaryDirectory() as tmp:
        root = Path(tmp)
        session = root / "owner"
        env = dict(os.environ, DRAWBORAD_SESSION_HOME=str(session), DRAWBORAD_CLOUD_PORT=str(port))
        env.pop("DRAWBORAD_CLOUD_TOKEN", None)
        token_file = session / "owner-token"
        log_file = root / "session.log"
        process = None

        def start():
            nonlocal process
            with log_file.open("ab") as log:
                process = subprocess.Popen(launcher, env=env, stdout=log, stderr=log, start_new_session=True)
            deadline = time.monotonic() + 300
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f"Launcher exited with {process.returncode}")
                try:
                    with http.open(url + "/healthz", timeout=1) as response:
                        if response.status == 200:
                            return
                except (OSError, URLError):
                    time.sleep(0.25)
            raise RuntimeError("Cloud session did not become ready")

        def stop():
            if process is not None and process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()

        try:
            start()
            token = token_file.read_text().strip()
            assert stat.S_IMODE(token_file.stat().st_mode) == 0o600 and len(token) >= 32
            if os.environ.get("GITHUB_ACTIONS") == "true":
                print("::add-mask::" + token, flush=True)
            second = subprocess.run(launcher, env=env, capture_output=True, text=True, timeout=10)
            assert second.returncode != 0 and "already owns" in second.stderr

            def restart():
                stop()
                env["PIP_NO_INDEX"] = "1"
                start()
                assert token_file.read_text().strip() == token

            verify_in_chromium(source, output, url, token, restart, {
                "target": "unprivileged Linux cloud-session launcher on CI loopback",
                "foreground_process_recovery": "saved revision", "owner_token_reused": True,
            })
            assert token not in log_file.read_text(errors="replace")
        except Exception:
            if log_file.exists():
                tail = log_file.read_text(errors="replace")[-3500:]
                if token_file.exists() and (secret := token_file.read_text().strip()):
                    tail = tail.replace(secret, "[redacted]")
                print(tail, file=sys.stderr)
            raise
        finally:
            stop()


if __name__ == "__main__":
    main(*(Path(arg).resolve() for arg in sys.argv[1:]))
