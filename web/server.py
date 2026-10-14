"""Loopback API: 20 MiB HTTP bodies, 16 MiB compact UTF-8 projects, 1 MiB scripts.

Project simulations additionally cap geometry at 1000 components/2000 wires in
the bridge before quadratic connectivity; validate_project uses desktop limits.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import threading
import time
from urllib.parse import urlsplit
import uuid


ROOT = Path(__file__).resolve().parents[1]
SOURCE_LIMIT = 1024 * 1024
PROJECT_LIMIT = 16 * 1024 * 1024
REQUEST_LIMIT = 20 * 1024 * 1024


def reject_constant(value):
    raise ValueError(f"Non-finite JSON number: {value}")


class Jobs:
    def __init__(self, bridge):
        self.bridge = str(bridge)
        self.lock = threading.Lock()
        self.entries = {}
        self.pool = ThreadPoolExecutor(max_workers=1)

    def start(self, request):
        if ("source" in request) == ("project" in request):
            raise ValueError("Provide exactly one of project or source")
        project_request = "project" in request
        if project_request:
            project = request["project"]
            if not isinstance(project, dict):
                raise ValueError("Project must be a JSON object")
            if len(json.dumps(project, ensure_ascii=False, separators=(",", ":"),
                              allow_nan=False).encode("utf-8")) > PROJECT_LIMIT:
                raise ValueError("Project exceeds the 16 MiB serialized UTF-8 limit")
        else:
            source = request["source"]
            if not isinstance(source, str) or len(source.encode("utf-8")) > SOURCE_LIMIT:
                raise ValueError("Source must be UTF-8 text of at most 1 MiB")
        operations = ("compile", "run", "validate_project") if project_request else ("compile", "run")
        if request.get("operation") not in operations:
            raise ValueError("Operation must be compile or run, or validate_project with a project")
        if not isinstance(request.get("analysis", ""), str):
            raise ValueError("Analysis must be a name")
        payload = json.dumps(request, ensure_ascii=False, separators=(",", ":"),
                             allow_nan=False).encode("utf-8")
        with self.lock:
            if any(entry["active"] for entry in self.entries.values()):
                raise RuntimeError("Another simulation is still running")
            now = time.monotonic()
            self.entries = {key: value for key, value in self.entries.items()
                            if now - value["created"] < 600}
            while len(self.entries) >= 32:
                del self.entries[next(iter(self.entries))]
            job_id = uuid.uuid4().hex
            self.entries[job_id] = dict(state="running", created=now, process=None, active=True)
        self.pool.submit(self.execute, job_id, payload)
        return job_id

    def execute(self, job_id, payload):
        process = None
        try:
            with self.lock:
                entry = self.entries[job_id]
                if entry["state"] == "cancelled":
                    entry["active"] = False
                    return
                process = subprocess.Popen([self.bridge], stdin=subprocess.PIPE,
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                entry["process"] = process
            output, error = process.communicate(payload, timeout=20)
            if process.returncode:
                raise RuntimeError("Solver process failed")
            result = json.loads(output)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            result = dict(ok=False, diagnostics=[dict(message="Simulation exceeded 20 seconds", warning=False)])
        except (OSError, RuntimeError, ValueError) as error:
            result = dict(ok=False, diagnostics=[dict(message=str(error), warning=False)])
        with self.lock:
            entry = self.entries[job_id]
            entry["process"] = None
            entry["active"] = False
            if entry["state"] != "cancelled":
                entry.update(state="done", result=result)

    def get(self, job_id):
        with self.lock:
            entry = self.entries[job_id]
            result = dict(state=entry["state"])
            if "result" in entry:
                result["result"] = entry["result"]
            return result

    def cancel(self, job_id):
        with self.lock:
            entry = self.entries[job_id]
            if entry["state"] == "running":
                entry["state"] = "cancelled"
                if entry["process"] is not None:
                    entry["process"].kill()
        return self.get(job_id)

    def close(self):
        for job_id in list(self.entries):
            self.cancel(job_id)
        self.pool.shutdown(wait=True)


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, bridge):
        super().__init__(address, Handler)
        self.jobs = Jobs(bridge)

    def server_close(self):
        super().server_close()
        self.jobs.close()


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def allowed(self):
        expected = f"127.0.0.1:{self.server.server_port}"
        if self.headers.get("Host") != expected:
            self.send_json(dict(error="Use the loopback URL"), 403)
            return False
        origin = self.headers.get("Origin")
        if origin and origin != f"http://{expected}":
            self.send_json(dict(error="Cross-origin requests are not allowed"), 403)
            return False
        return True

    def send_bytes(self, content, content_type, status=200):
        if content_type.startswith("text/"):
            content_type += "; charset=utf-8"
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
        self.end_headers()
        try:
            self.wfile.write(content)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def send_json(self, content, status=200):
        self.send_bytes(json.dumps(content).encode("utf-8"), "application/json; charset=utf-8", status)

    def do_GET(self):
        if not self.allowed():
            return
        route = urlsplit(self.path).path
        if route == "/api/health":
            self.send_json(dict(ready=True))
        elif route == "/api/examples":
            self.send_json([dict(id=filename.stem, title=filename.stem.replace("_", " ").title(),
                                 source=filename.read_text(encoding="utf-8"))
                            for filename in sorted((ROOT / "examples/scripts").glob("*.msim"))])
        elif route == "/api/project-examples":
            self.send_json([dict(id=filename.stem, title=filename.stem.replace("_", " ").title(),
                                 project=json.loads(filename.read_text(encoding="utf-8")))
                            for filename in sorted((ROOT / "examples").glob("*.microspice"))])
        elif route.startswith("/api/jobs/"):
            try:
                self.send_json(self.server.jobs.get(route.removeprefix("/api/jobs/")))
            except KeyError:
                self.send_json(dict(error="Job not found"), 404)
        else:
            static = {"/": (ROOT / "web/static/index.html", "text/html"),
                      "/app.js": (ROOT / "web/static/app.js", "text/javascript"),
                      "/schematic.js": (ROOT / "web/static/schematic.js", "text/javascript"),
                      "/style.css": (ROOT / "web/static/style.css", "text/css")}
            static["/tests/schematic.test.html"] = (ROOT / "web/static/tests/schematic.test.html", "text/html")
            static["/tests/schematic.test.js"] = (ROOT / "web/static/tests/schematic.test.js", "text/javascript")
            static["/tests/schematic.test.css"] = (ROOT / "web/static/tests/schematic.test.css", "text/css")
            for name in ("user-guide", "simulation-language", "web-workspace"):
                static[f"/docs/{name}.md"] = (ROOT / f"docs/{name}.md", "text/plain")
            static["/notices"] = (ROOT / "web/THIRD-PARTY-NOTICES.md", "text/plain")
            static["/licenses/json"] = (ROOT / "web/licenses/json-LICENSE.MIT", "text/plain")
            static["/licenses/lucide"] = (ROOT / "gui/licenses/Lucide-LICENSE.txt", "text/plain")
            for filename in (ROOT / "gui/icons").glob("*.png"):
                static[f"/icons/{filename.name}"] = (filename, "image/png")
            item = static.get(route)
            if item and item[0].is_file():
                self.send_bytes(item[0].read_bytes(), item[1])
            else:
                self.send_json(dict(error="Not found"), 404)

    def do_POST(self):
        if not self.allowed():
            return
        route = urlsplit(self.path).path
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length == 0 and route.startswith("/api/jobs/") and route.endswith("/cancel"):
                job_id = route[len("/api/jobs/"):-len("/cancel")]
                self.send_json(self.server.jobs.cancel(job_id))
                return
            if length < 1 or length > REQUEST_LIMIT:
                self.send_json(dict(error="Request too large or empty"), 413)
                return
            if self.headers.get_content_type() != "application/json":
                self.send_json(dict(error="Expected application/json"), 415)
                return
            request = json.loads(self.rfile.read(length).decode("utf-8"), parse_constant=reject_constant)
            if not isinstance(request, dict):
                raise ValueError("Expected a JSON object")
            if route == "/api/jobs":
                self.send_json(dict(id=self.server.jobs.start(request)), 202)
            elif route.startswith("/api/jobs/") and route.endswith("/cancel"):
                job_id = route[len("/api/jobs/"):-len("/cancel")]
                self.send_json(self.server.jobs.cancel(job_id))
            else:
                self.send_json(dict(error="Not found"), 404)
        except KeyError:
            self.send_json(dict(error="Job not found"), 404)
        except (ValueError, UnicodeError) as error:
            self.send_json(dict(error=str(error)), 400)
        except RuntimeError as error:
            self.send_json(dict(error=str(error)), 409)

    def log_message(self, format_string, *arguments):
        pass


def main():
    parser = argparse.ArgumentParser(description="Local microspice browser workspace")
    parser.add_argument("--bridge", type=Path, default=ROOT / "build-web/web/microspice_web_bridge")
    parser.add_argument("--port", type=int, default=8765)
    arguments = parser.parse_args()
    bridge = arguments.bridge.resolve()
    if not bridge.is_file():
        parser.error("Build microspice_web_bridge first, or provide --bridge")
    with Server(("127.0.0.1", arguments.port), bridge) as server:
        print(f"microspice: http://127.0.0.1:{server.server_port}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()