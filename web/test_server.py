import json
import copy
import math
from pathlib import Path
import subprocess
import sys
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from server import ROOT, Server, PROJECT_LIMIT, REQUEST_LIMIT

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

    def project(self, name="voltage_divider"):
        return json.loads((ROOT / f"examples/{name}.microspice").read_text())

    def bridge(self, request):
        result = subprocess.run([BRIDGE], input=json.dumps(request), text=True,
                                capture_output=True, check=True, timeout=30)
        return json.loads(result.stdout)

    def solve_project(self, project, operation="run", settings=None):
        request = dict(project=project, operation=operation)
        if settings is not None:
            request["settings"] = settings
        return self.bridge(request)

    def trace(self, result, label):
        return next(trace["data"] for trace in result["results"][0]["observations"]
                    if trace["label"] == label)

    def test_project_examples(self):
        examples = self.request("/api/project-examples")
        self.assertEqual(len(examples), 5)
        for example in examples:
            with self.subTest(example=example["id"]):
                result = self.solve_project(example["project"])
                self.assertTrue(result["ok"], result)
                self.assertEqual(result["normalizedProject"], example["project"])
                self.assertEqual(result["analyses"], [dict(name="schematic", kind="dc")])
                self.assertEqual(set(result["terminals"]),
                                 {item["reference"] for item in example["project"]["components"]})
                self.assertEqual(self.trace(result, "voltage(ground)"), [0])
                self.assertTrue(all(not name.startswith("script:") for name in result["nodes"]))
        divider = self.solve_project(self.project())
        output = divider["terminals"]["R1"][1]
        self.assertEqual(self.trace(divider, f"voltage({output})"), [5])
        self.assertEqual(self.trace(divider, "current(V1)"), [-0.005])

    def test_project_transient(self):
        project = self.project("rc_charging")
        settings = dict(kind="transient", step=0.0001, stop=0.005, initialVoltages={"C1": 0})
        result = self.solve_project(project, settings=settings)
        self.assertTrue(result["ok"], result)
        self.assertEqual(len(result["results"][0]["axis"]), 50)
        output = result["terminals"]["C1"][0]
        samples = self.trace(result, f"voltage({output})")
        self.assertAlmostEqual(samples[0], 10 / 11)
        self.assertAlmostEqual(samples[-1], 10 * (1 - (10 / 11) ** 50))
        settings["initialVoltages"] = {"C1": 4}
        charged = self.solve_project(project, settings=settings)
        self.assertTrue(charged["ok"], charged)
        self.assertAlmostEqual(self.trace(charged, f"voltage({output})")[0], 10 - 6 * 10 / 11)
        for update in (dict(step=0), dict(stop=0), dict(step=True), dict(stop=math.inf),
                       dict(step=1e-9), dict(initialVoltages={"C2": 0}),
                       dict(initialVoltages={"C1": False}), dict(initialVoltages=[]),
                       dict(initialVoltages={'C1\")\ndc("bad': 0})):
            with self.subTest(update=update):
                self.assertFalse(self.solve_project(project, settings={**settings, **update})["ok"])
        nonlinear = self.solve_project(self.project("diode_bias"), settings=settings)
        self.assertFalse(nonlinear["ok"])

    def test_project_diode_sweep(self):
        project = self.project("diode_bias")
        result = self.solve_project(project)
        self.assertTrue(result["ok"], result)
        diode_node = result["terminals"]["D1"][0]
        voltage = self.trace(result, f"voltage({diode_node})")[0]
        self.assertGreater(voltage, 0.5)
        self.assertLess(voltage, 0.8)
        settings = dict(kind="sweep", source="V1", start=0, stop=5, step=0.1)
        result = self.solve_project(project, settings=settings)
        self.assertTrue(result["ok"], result)
        self.assertEqual(len(result["results"][0]["axis"]), 51)
        self.assertAlmostEqual(self.trace(result, f"voltage({diode_node})")[-1], voltage, places=6)
        for update in (dict(source="V2"), dict(source="R1"), dict(source='V1\")\ndc("bad'),
                       dict(step=0), dict(step=-0.1), dict(start=True), dict(stop=math.inf)):
            self.assertFalse(self.solve_project(project, settings={**settings, **update})["ok"])
        descending = self.solve_project(project, settings={**settings, "start": 5, "stop": 0, "step": -1})
        self.assertTrue(descending["ok"], descending)

    def test_validate_project(self):
        empty = dict(format="microspice.project", version=1, counters=[0] * 6, components=[], wires=[])
        result = self.solve_project(empty, "validate_project")
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["normalizedProject"], empty)
        self.assertNotIn("results", result)
        self.assertFalse(self.solve_project(empty, "compile")["ok"])
        project = self.project()
        project["components"][0]["value"] = -1.2345678901234567
        project["counters"] = [999999999] * 6
        normalized = self.solve_project(project, "validate_project")
        self.assertEqual(normalized["normalizedProject"], project)

    def test_project_malformed_fields(self):
        project = self.project()
        changes = [("version", True), ("version", 2), ("version", "1"),
                   ("format", "other"), ("components", {}), ("wires", {}),
                   ("counters", [0] * 5), ("counters", [False] * 6),
                   ("counters", [-1] * 6), ("counters", [1.5] * 6),
                   ("counters", [1000000000] * 6)]
        for field, value in changes:
            with self.subTest(field=field, value=value):
                self.assertFalse(self.solve_project({**project, field: value}, "validate_project")["ok"])
        changes = [("kind", True), ("kind", -1), ("kind", 6), ("kind", 0.5),
                   ("reference", "R0"), ("reference", "V1000000000"), ("reference", "R1"),
                   ("reference", "V1\n"), ("reference", 'V1\")\ndc("bad'),
                   ("position", [0]), ("position", [True, 0]), ("position", [1, 0]),
                   ("position", [1000020, 0]), ("position", [math.inf, 0]),
                   ("rotation", 45), ("rotation", 360), ("rotation", -90),
                   ("rotation", True), ("value", False), ("value", math.nan),
                   ("ideality", 0), ("ideality", True)]
        for field, value in changes:
            invalid = copy.deepcopy(project)
            invalid["components"][0][field] = value
            with self.subTest(field=field, value=value):
                self.assertFalse(self.solve_project(invalid, "validate_project")["ok"])
        for index, value in ((1, 0), (1, -1), (3, 1)):
            invalid = copy.deepcopy(project)
            invalid["components"][index]["value"] = value
            self.assertFalse(self.solve_project(invalid, "validate_project")["ok"])
        duplicate = copy.deepcopy(project)
        duplicate["components"].append(duplicate["components"][0])
        self.assertFalse(self.solve_project(duplicate, "validate_project")["ok"])
        for wire in (None, {}, dict(start=[0, 0], end=[0, 0]),
                     dict(start=[0, 0], end=[20, 20]), dict(start=[0, 0], end=[0, 1])):
            self.assertFalse(self.solve_project({**project, "wires": [wire]}, "validate_project")["ok"])
        no_ground = copy.deepcopy(project)
        no_ground["components"] = no_ground["components"][:-1]
        self.assertTrue(self.solve_project(no_ground, "validate_project")["ok"])
        self.assertFalse(self.solve_project(no_ground, "compile")["ok"])

    def test_project_exclusivity_and_jobs(self):
        for request in (dict(operation="compile"), dict(project=self.project(), source="", operation="compile"),
                        dict(source="", operation="validate_project")):
            self.assertFalse(self.bridge(request)["ok"])
            with self.assertRaises(HTTPError) as error:
                self.request("/api/jobs", request)
            self.assertEqual(error.exception.code, 400)
        for operation in ("compile", "run", "validate_project"):
            job = self.request("/api/jobs", dict(project=self.project(), operation=operation))
            self.server.jobs.pool.submit(lambda: None).result(timeout=30)
            response = self.request("/api/jobs/" + job["id"])
            self.assertEqual(response["state"], "done")
            self.assertTrue(response["result"]["ok"], response)

    def test_project_geometry_and_orientation(self):
        def component(kind, reference, position, rotation=0, value=1000):
            return dict(kind=kind, reference=reference, position=position,
                        rotation=rotation, value=value, ideality=1)

        project = dict(format="microspice.project", version=1, counters=[0] * 6,
                       components=[component(0, "R1", [0, 0]),
                                   component(5, "G1", [0, -100], value=0)],
                       wires=[dict(start=[-100, 0], end=[100, 0]),
                              dict(start=[0, -100], end=[0, 100])])
        separate = self.solve_project(project, "compile")
        self.assertTrue(separate["ok"], separate)
        self.assertEqual(separate["terminals"]["R1"], ["n1", "n1"])
        project["wires"][1]["end"] = [0, 0]
        joined = self.solve_project(project, "compile")
        self.assertTrue(joined["ok"], joined)
        self.assertEqual(joined["terminals"]["R1"], ["0", "0"])
        project["components"].append(component(5, "G2", [500, 500], value=0))
        grounded = self.solve_project(project, "compile")
        self.assertEqual(grounded["terminals"]["G2"], ["0"])
        for kind, prefix in ((3, "V"), (4, "I")):
            for rotation, positive in ((0, [-60, 0]), (90, [0, -60]),
                                       (180, [60, 0]), (270, [0, 60])):
                with self.subTest(kind=kind, rotation=rotation):
                    source = component(kind, prefix + "1", [0, 0], rotation, 2 if kind == 3 else 0.002)
                    resistor = component(0, "R1", [0, 0], rotation)
                    ground = component(5, "G1", positive, value=0)
                    circuit = dict(format="microspice.project", version=1, counters=[0] * 6,
                                   components=[source, resistor, ground], wires=[])
                    result = self.solve_project(circuit)
                    self.assertTrue(result["ok"], result)
                    self.assertEqual(result["terminals"][prefix + "1"], ["0", "n1"])
                    voltage = self.trace(result, "voltage(n1)")[0]
                    self.assertAlmostEqual(voltage, -2 if kind == 3 else 2)
                    if kind == 3:
                        self.assertAlmostEqual(self.trace(result, "current(V1)")[0], -0.002)
                    else:
                        self.assertAlmostEqual(self.trace(result, "current(I1)")[0], 0.002)

    def test_project_budgets(self):
        project = self.project()
        wire = dict(start=[0, 0], end=[0, 20])
        for field, entries in (("components", [project["components"][0]] * 10001),
                               ("wires", [wire] * 50001)):
            self.assertFalse(self.solve_project({**project, field: entries}, "validate_project")["ok"])
        too_many_wires = {**project, "wires": [wire] * 2001}
        self.assertTrue(self.solve_project(too_many_wires, "validate_project")["ok"])
        self.assertIn("geometry budget", self.solve_project(too_many_wires, "compile")["diagnostics"][0]["message"])
        components = [dict(kind=0, reference=f"R{index + 1}", position=[index * 140, 0],
                           rotation=0, value=1000, ideality=1) for index in range(1000)]
        ground = dict(kind=5, reference="G1", position=[0, 100], rotation=0, value=0, ideality=1)
        oversized = {**project, "components": components + [ground], "wires": []}
        self.assertTrue(self.solve_project(oversized, "validate_project")["ok"])
        self.assertIn("geometry budget", self.solve_project(oversized, "compile")["diagnostics"][0]["message"])
        unknowns = {**oversized, "components": components[:260] + [ground]}
        self.assertIn("unknown limit", self.solve_project(unknowns, "compile")["diagnostics"][0]["message"])
        huge = {**project, "extra": "x" * PROJECT_LIMIT}
        self.assertFalse(self.solve_project(huge, "validate_project")["ok"])
        with self.assertRaises(ValueError):
            self.server.jobs.start(dict(project=huge, operation="validate_project"))
        request = Request(self.url + "/api/jobs", data=b"{}", method="POST",
                          headers={"Content-Type": "application/json", "Content-Length": str(REQUEST_LIMIT + 1)})
        with self.assertRaises(HTTPError) as error:
            urlopen(request)
        self.assertEqual(error.exception.code, 413)

    def test_project_http_nonfinite(self):
        for literal in ("NaN", "Infinity", "-Infinity", "1e999"):
            body = json.dumps(dict(project=self.project(), operation="compile"))
            body = body.replace('"version": 1', '"version": ' + literal)
            request = Request(self.url + "/api/jobs", data=body.encode(),
                              headers={"Content-Type": "application/json"})
            with self.assertRaises(HTTPError) as error:
                urlopen(request)
            self.assertEqual(error.exception.code, 400)

    def test_project_cancel_preserves_active_slot(self):
        blocker = threading.Event()
        self.server.jobs.pool.submit(blocker.wait)
        try:
            job = self.request("/api/jobs", dict(project=self.project(), operation="run"))
            request = Request(self.url + f'/api/jobs/{job["id"]}/cancel', method="POST")
            with urlopen(request) as response:
                self.assertEqual(json.load(response)["state"], "cancelled")
            for operation in ("compile", "run", "validate_project"):
                with self.assertRaises(HTTPError) as error:
                    self.request("/api/jobs", dict(project=self.project(), operation=operation))
                self.assertEqual(error.exception.code, 409)
        finally:
            blocker.set()
            self.server.jobs.pool.submit(lambda: None).result(timeout=30)
        self.assertEqual(self.request("/api/jobs/" + job["id"])["state"], "cancelled")
        next_job = self.request("/api/jobs", dict(project=self.project(), operation="compile"))
        self.server.jobs.pool.submit(lambda: None).result(timeout=30)
        self.assertTrue(self.request("/api/jobs/" + next_job["id"])["result"]["ok"])

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