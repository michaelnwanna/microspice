import json
from pathlib import Path
import subprocess
import sys
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from server import ROOT, Server

BRIDGE = Path(sys.argv.pop(1)).resolve()


class WebTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = Server(("127.0.0.1", 0), BRIDGE)
        cls.thread = threading.Thread(target=cls.server.serve_forever)
        cls.thread.start()
        cls.url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.thread.join()
        cls.server.server_close()

    def request(self, route, data=None, headers=None):
        request = Request(self.url + route, headers=headers or {})
        if data is not None:
            request.data = json.dumps(data).encode()
            request.add_header("Content-Type", "application/json")
        with urlopen(request, timeout=30) as response:
            return json.load(response)

    def solve(self, source, operation="run", analysis=""):
        result = subprocess.run([BRIDGE], input=json.dumps(dict(source=source, operation=operation,
                                analysis=analysis)), text=True, capture_output=True, check=True)
        return json.loads(result.stdout)

    def test_bridge_examples(self):
        for filename in (ROOT / "examples/scripts").glob("*.msim"):
            with self.subTest(example=filename.stem):
                result = self.solve(filename.read_text())
                self.assertTrue(result["ok"], result)
                self.assertTrue(result["checksPassed"])
        result = self.solve((ROOT / "examples/scripts/voltage_divider.msim").read_text())
        self.assertEqual(result["results"][0]["observations"][0]["data"], [5])

    def test_errors_and_analysis_selection(self):
        invalid = self.solve('resistor("R1", a, ground, 1V)', "compile")
        self.assertFalse(invalid["ok"])
        self.assertEqual(invalid["diagnostics"][0]["line"], 1)
        source = (ROOT / "examples/scripts/diode_bias.msim").read_text()
        compiled = self.solve(source, "compile")
        selected = self.solve(source, analysis=compiled["analyses"][0]["name"])
        self.assertEqual(len(selected["results"]), 1)
        self.assertIn("csv", selected["results"][0])

    def test_static_and_origin_guards(self):
        self.assertTrue(self.request("/api/health")["ready"])
        self.assertEqual(len(self.request("/api/examples")), 6)
        with urlopen(self.url) as response:
            self.assertIn(b"microspice", response.read())
            self.assertIn("frame-ancestors 'none'", response.headers["Content-Security-Policy"])
        for route in ("/docs/web-workspace.md", "/notices", "/licenses/json", "/licenses/lucide"):
            with urlopen(self.url + route) as response:
                self.assertEqual(response.status, 200)
                self.assertIn("charset=utf-8", response.headers["Content-Type"])
        for route, headers, code in [("/api/health", {"Host": "evil.test"}, 403),
                                     ("/api/health", {"Origin": "https://evil.test"}, 403),
                                     ("/../CMakeLists.txt", {}, 404)]:
            with self.assertRaises(HTTPError) as error:
                self.request(route, headers=headers)
            self.assertEqual(error.exception.code, code)

    def test_job_lifecycle(self):
        source = (ROOT / "examples/scripts/rc_charging.msim").read_text()
        job = self.request("/api/jobs", dict(source=source, operation="run"))
        self.server.jobs.pool.submit(lambda: None).result(timeout=30)
        result = self.request("/api/jobs/" + job["id"])
        self.assertEqual(result["state"], "done")
        self.assertEqual(len(result["result"]["results"][0]["axis"]), 50)
        self.assertTrue(result["result"]["checksPassed"])

    def test_cancel_and_busy(self):
        blocker = threading.Event()
        self.server.jobs.pool.submit(blocker.wait)
        try:
            job = self.request("/api/jobs", dict(source='dc("bias")', operation="compile"))
            with self.assertRaises(HTTPError) as error:
                self.request("/api/jobs", dict(source="", operation="compile"))
            self.assertEqual(error.exception.code, 409)
            request = Request(self.url + f'/api/jobs/{job["id"]}/cancel', method="POST")
            with urlopen(request) as response:
                result = json.load(response)
            self.assertEqual(result["state"], "cancelled")
            with self.assertRaises(HTTPError) as error:
                self.request("/api/jobs", dict(source="", operation="compile"))
            self.assertEqual(error.exception.code, 409)
        finally:
            blocker.set()
            self.server.jobs.pool.submit(lambda: None).result(timeout=30)

    def test_limits(self):
        with self.assertRaises(HTTPError) as error:
            self.request("/api/jobs", dict(source="x" * (1024 * 1024 + 1), operation="run"))
        self.assertEqual(error.exception.code, 400)
        with self.assertRaises(HTTPError):
            self.request("/api/jobs", dict(source="", operation="unknown"))


if __name__ == "__main__":
    unittest.main()