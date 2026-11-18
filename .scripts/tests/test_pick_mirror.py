#!/usr/bin/env python3
"""Tests for .github/actions/install-deps/pick-mirror.sh (Kartend-wym32, Kartend-b128j).

The script reorders the runner's apt mirror list when the first mirror is slow.
A wrong reorder would break every Linux job's dependency install, and the real
thing only runs on hosted runners, so these tests run the real script against
local HTTP servers: one that answers at once, one that trickles, one that
answers 404. They assert the list file it leaves behind.

stdlib unittest — pytest is not available in CI. Run directly
(`python3 .scripts/tests/test_pick_mirror.py`) or via `python3 -m unittest`.
"""
from __future__ import annotations

import http.server
import os
import pathlib
import subprocess
import tempfile
import threading
import time
import unittest

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
SCRIPT = REPO / ".github" / "actions" / "install-deps" / "pick-mirror.sh"
PAYLOAD = b"x" * 600_000


class Mirror:
    """A local mirror: 'fast' answers at once, 'slow' trickles about 50 kB/s, 'missing' answers 404."""

    def __init__(self, kind: str):
        self.kind, self.hits = kind, 0
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):  # noqa: N802 (http.server's name)
                outer.hits += 1
                if outer.kind == "missing":
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(PAYLOAD)))
                self.end_headers()
                try:
                    if outer.kind == "fast":
                        self.wfile.write(PAYLOAD)
                        return
                    for start in range(0, len(PAYLOAD), 5_000):
                        self.wfile.write(PAYLOAD[start:start + 5_000])
                        self.wfile.flush()
                        time.sleep(0.1)
                except (BrokenPipeError, ConnectionResetError):
                    pass  # the script's time limit cut the fetch off, as meant

            def log_message(self, *_):
                pass

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}/ubuntu/"
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()


class PickMirror(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.list = pathlib.Path(self.tmp.name) / "apt-mirrors.txt"
        self.mirrors: list[Mirror] = []

    def tearDown(self):
        for mirror in self.mirrors:
            mirror.close()
        self.tmp.cleanup()

    def mirror(self, kind: str) -> Mirror:
        self.mirrors.append(Mirror(kind))
        return self.mirrors[-1]

    def write_list(self, *urls: str) -> str:
        text = "".join(f"{url}\tpriority:{n}\n" for n, url in enumerate(urls, 1))
        self.list.write_text(text)
        return text

    def run_script(self, fallbacks: str = "", **env: str) -> subprocess.CompletedProcess:
        settings = {
            "APT_MIRRORS_FILE": str(self.list), "APT_MIRROR_SUDO": "", "APT_MIRROR_PROBE_PATH": "probe.bin",
            "APT_MIRROR_PROBE_SECONDS": "1", "APT_MIRROR_HEALTHY_BPS": "1000000", "APT_MIRROR_FALLBACKS": fallbacks,
        }
        settings.update(env)
        return subprocess.run(["bash", str(SCRIPT)], env=dict(os.environ, **settings), capture_output=True,
                              text=True, check=False, timeout=60)

    def urls(self) -> list[str]:
        return [line.split("\t")[0] for line in self.list.read_text().splitlines()]

    def test_healthy_first_mirror_leaves_the_list_and_times_nothing_else(self):
        fast, other = self.mirror("fast"), self.mirror("fast")
        before = self.write_list(fast.url, other.url)
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.list.read_text(), before)
        self.assertEqual(other.hits, 0)
        self.assertIn("the first mirror is healthy", done.stdout)

    def test_slow_first_mirror_goes_behind_a_fast_listed_one(self):
        slow, fast = self.mirror("slow"), self.mirror("fast")
        self.write_list(slow.url, fast.url)
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.list.read_text(), f"{fast.url}\tpriority:1\n{slow.url}\tpriority:2\n")
        self.assertIn("the first mirror is slow", done.stdout)

    def test_fast_fallback_is_added_in_front(self):
        slow, fast = self.mirror("slow"), self.mirror("fast")
        self.write_list(slow.url)
        done = self.run_script(fallbacks=fast.url)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.urls(), [fast.url, slow.url])

    def test_fallback_that_is_already_listed_is_not_listed_twice(self):
        slow, fast = self.mirror("slow"), self.mirror("fast")
        self.write_list(slow.url, fast.url)
        self.run_script(fallbacks=fast.url.rstrip("/"))  # the same mirror, written without its last slash
        self.assertEqual(self.urls(), [fast.url, slow.url])

    def test_fallback_that_does_not_answer_is_not_added(self):
        slow, fast, missing = self.mirror("slow"), self.mirror("fast"), self.mirror("missing")
        self.write_list(slow.url, fast.url)
        self.run_script(fallbacks=f"{missing.url} http://127.0.0.1:9/ubuntu/")
        self.assertEqual(self.urls(), [fast.url, slow.url])

    def test_listed_mirror_that_does_not_answer_stays_listed_last(self):
        slow, missing, fast = self.mirror("slow"), self.mirror("missing"), self.mirror("fast")
        self.write_list(slow.url, missing.url, fast.url)
        self.run_script()
        self.assertEqual(self.urls(), [fast.url, slow.url, missing.url])

    def test_nothing_faster_leaves_the_list(self):
        slow, missing = self.mirror("slow"), self.mirror("missing")
        before = self.write_list(slow.url, missing.url)
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.list.read_text(), before)
        self.assertIn("no mirror is faster", done.stdout)

    def test_list_that_cannot_be_written_stays_and_the_step_passes(self):
        slow, fast = self.mirror("slow"), self.mirror("fast")
        before = self.write_list(slow.url, fast.url)
        done = self.run_script(APT_MIRROR_SUDO="false")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.list.read_text(), before)
        self.assertIn("could not be written", done.stdout)

    def test_no_list_file_is_not_an_error(self):
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertFalse(self.list.exists())
        self.assertIn("not a hosted runner image", done.stdout)

    def test_list_without_a_mirror_stays(self):
        self.list.write_text("# nothing here\n\n")
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.list.read_text(), "# nothing here\n\n")

    def test_other_fields_of_a_line_do_not_confuse_it(self):
        slow, fast = self.mirror("slow"), self.mirror("fast")
        self.list.write_text(f"{slow.url}\tpriority:1\tlang:en\n\n{fast.url}  priority:2\n")
        self.run_script()
        self.assertEqual(self.list.read_text(), f"{fast.url}\tpriority:1\n{slow.url}\tpriority:2\n")


if __name__ == "__main__":
    unittest.main()
