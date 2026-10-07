"""ROM-free checks of the gate's scheduling and generation helpers
(tools/build_recomp.py): which routes share one session, the longest-first
start order, results returned in item order, and the sync that rewrites only
generated files whose bytes changed."""
import glob
import os
import sys
import tempfile
import threading
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import build_recomp as gate  # noqa: E402


class SessionGroups(unittest.TestCase):
    def test_identical_sessions_share_a_run(self):
        routes = sorted(glob.glob(os.path.join(ROOT, "tools", "routes", "*.args")))
        groups = gate.session_groups(routes)
        self.assertEqual(sorted(r for g in groups for r in g), routes, "every route in exactly one group")
        names = [[os.path.basename(r) for r in g] for g in groups if len(g) > 1]
        self.assertIn(["recon_career.args", "recon_return.args"], names)

    def test_different_options_do_not_share(self):
        with tempfile.TemporaryDirectory() as d:
            def route(name, steps, comment=""):
                path = os.path.join(d, name)
                with open(path, "w") as f:
                    f.write(comment + "--steps\n%d\n--time-us\n1\n" % steps)
                return path
            a, b, c = route("a.args", 5, "# expect-world LB\n"), route("b.args", 5), route("c.args", 6)
            groups = gate.session_groups([a, b, c])
            self.assertEqual([[a, b], [c]], groups)


class Scheduling(unittest.TestCase):
    def test_results_in_item_order_and_longest_started_first(self):
        started, lock = [], threading.Lock()

        def work(item):
            with lock:
                started.append(item)
            time.sleep(0.01)
            return item * 10
        items = [1, 5, 3, 9, 2]
        self.assertEqual([10, 50, 30, 90, 20], gate.parallel(1, items, work, cost=lambda i: i))
        self.assertEqual([9, 5, 3, 2, 1], started)

    def test_failure_is_raised_after_the_others_finish(self):
        done = []

        def work(item):
            if item == 2:
                sys.exit("boom")
            done.append(item)
        with self.assertRaises(SystemExit):
            gate.parallel(2, [1, 2, 3], work)
        self.assertEqual([1, 3], sorted(done))

    def test_interpreter_costs_more_than_recomp(self):
        route = os.path.join(ROOT, "tools", "routes", "strike.args")
        self.assertEqual(3 * gate.expected_cost(route), gate.expected_cost(route, "interp"))


class SyncTree(unittest.TestCase):
    def test_only_changed_files_are_rewritten(self):
        with tempfile.TemporaryDirectory() as new, tempfile.TemporaryDirectory() as gen:
            def put(d, name, text):
                with open(os.path.join(d, name), "w") as f:
                    f.write(text)
            put(new, "a.c", "one"); put(new, "b.c", "two")
            self.assertTrue(gate.sync_tree(new, gen))
            old = os.path.getmtime(os.path.join(gen, "a.c"))
            os.utime(os.path.join(gen, "a.c"), (old - 100, old - 100))
            stamp = os.path.getmtime(os.path.join(gen, "a.c"))
            self.assertFalse(gate.sync_tree(new, gen), "identical input changes nothing")
            self.assertEqual(stamp, os.path.getmtime(os.path.join(gen, "a.c")), "an unchanged file keeps its time")
            put(new, "b.c", "three"); put(gen, "stale.c", "x")
            self.assertTrue(gate.sync_tree(new, gen))
            self.assertEqual(stamp, os.path.getmtime(os.path.join(gen, "a.c")))
            self.assertEqual(["a.c", "b.c"], sorted(os.listdir(gen)), "a file the new tree lacks is removed")
            with open(os.path.join(gen, "b.c")) as f:
                self.assertEqual("three", f.read())


class InterpreterCache(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.root, self.data = os.path.join(t, "repo"), os.path.join(t, "data")
        for d in ("src/cpu", "src/matched", "src/fixes", self.data):
            os.makedirs(d if os.path.isabs(d) else os.path.join(self.root, d))
        self.write(os.path.join(self.root, "CMakeLists.txt"), "project")
        self.write(os.path.join(self.root, "src/cpu/cpu.c"), "cpu v1")
        self.write(os.path.join(self.root, "src/matched/matched.c"), "matched v1")
        self.write(os.path.join(self.data, "F117.COM"), "game")
        self.saved_root = gate.ROOT
        gate.ROOT = self.root

    def tearDown(self):
        gate.ROOT = self.saved_root
        self.tmp.cleanup()

    @staticmethod
    def write(path, text):
        with open(path, "w") as f:
            f.write(text)

    def test_key_ignores_matched_but_not_the_interpreter_or_data(self):
        base = gate.interpreter_key(self.data)
        self.write(os.path.join(self.root, "src/matched/matched.c"), "matched v2")
        self.assertEqual(base, gate.interpreter_key(self.data), "a matched routine cannot change an interpreter result")
        self.write(os.path.join(self.root, "src/cpu/cpu.c"), "cpu v2")
        self.assertNotEqual(base, gate.interpreter_key(self.data))
        self.write(os.path.join(self.root, "src/cpu/cpu.c"), "cpu v1")
        self.assertEqual(base, gate.interpreter_key(self.data))
        self.write(os.path.join(self.data, "F117.COM"), "game changed")
        self.assertNotEqual(base, gate.interpreter_key(self.data))
        self.write(os.path.join(self.data, "F117.COM"), "game")
        self.write(os.path.join(self.root, "src/fixes/fixes.c"), "a fix")
        self.assertNotEqual(base, gate.interpreter_key(self.data), "fixes run under the interpreter too")

    def test_route_key_follows_the_route_text_and_replay_file(self):
        route = os.path.join(self.tmp.name, "r.args")
        replay = os.path.join(self.tmp.name, "r.input")
        self.write(replay, "keys")
        self.write(route, "# expect-world LB\n--replay\nr.input\n--steps\n5\n")
        k1 = gate.route_cache_key(route, "base")
        self.write(route, "# expect-world CU\n--replay\nr.input\n--steps\n5\n")
        self.assertNotEqual(k1, gate.route_cache_key(route, "base"), "a milestone edit invalidates the entry")
        self.write(route, "# expect-world LB\n--replay\nr.input\n--steps\n5\n")
        self.assertEqual(k1, gate.route_cache_key(route, "base"))
        self.write(replay, "other keys")
        self.assertNotEqual(k1, gate.route_cache_key(route, "base"), "a replayed input file is part of the key")
        self.assertNotEqual(k1, gate.route_cache_key(route, "another base"))

    def test_coverage_key_ignores_matched_but_not_the_translator_routes_or_data(self):
        route = os.path.join(self.tmp.name, "r.args")
        self.write(route, "--steps\n5\n")
        os.makedirs(os.path.join(self.root, "recompiler"))
        self.write(os.path.join(self.root, "recompiler", "recomp.py"), "translator v1")
        base = gate.coverage_key(self.data, [route])
        self.write(os.path.join(self.root, "src/matched/matched.c"), "matched v3")
        self.assertEqual(base, gate.coverage_key(self.data, [route]), "a matched routine cannot add code to cover")
        self.write(os.path.join(self.root, "recompiler", "recomp.py"), "translator v2")
        self.assertNotEqual(base, gate.coverage_key(self.data, [route]))
        self.write(os.path.join(self.root, "recompiler", "recomp.py"), "translator v1")
        self.write(route, "--steps\n6\n")
        self.assertNotEqual(base, gate.coverage_key(self.data, [route]), "a route edit changes what is interpreted")
        self.write(route, "--steps\n5\n")
        self.write(os.path.join(self.root, "src/cpu/cpu.c"), "cpu v9")
        self.assertNotEqual(base, gate.coverage_key(self.data, [route]), "the engine decides what is interpreted")

    def test_coverage_skip_needs_the_same_key_and_the_same_generated_code(self):
        record = os.path.join(self.tmp.name, "sub", "coverage-verified.json")
        self.assertFalse(gate.coverage_verified(record, "k", "g"), "nothing recorded yet")
        gate.record_coverage_verified(record, "k", "g")
        self.assertTrue(gate.coverage_verified(record, "k", "g"))
        self.assertFalse(gate.coverage_verified(record, "k2", "g"))
        self.assertFalse(gate.coverage_verified(record, "k", "g2"), "other generated code was not verified")
        self.write(record, "not json")
        self.assertFalse(gate.coverage_verified(record, "k", "g"))

    def test_a_stored_result_comes_back_intact_and_expires(self):
        cache = os.path.join(self.tmp.name, "cache")
        rundir = os.path.join(self.tmp.name, "run")
        save, log = os.path.join(self.tmp.name, "save"), os.path.join(self.tmp.name, "run.log")
        os.makedirs(save)
        self.write(os.path.join(save, "Roster.Fil"), "roster")
        self.write(log, "[exec] log")
        route = os.path.join(self.tmp.name, "r.args")
        self.write(route, "--steps\n5\n")
        result = (123, "abcd", 7, (("50000000", "ff", "START.EXE"),), save, log)
        self.assertIsNone(gate.cached_interp(cache, "k", "r", rundir, route))
        gate.store_interp(cache, "k", result)
        got = gate.cached_interp(cache, "k", "r", rundir, route)
        self.assertEqual(result[:4], got[:4])
        with open(os.path.join(got[4], "Roster.Fil")) as f:
            self.assertEqual("roster", f.read())
        meta = os.path.join(cache, "k", "result.json")
        info = gate.json.load(open(meta))
        info["stored"] -= (gate.CACHE_MAX_AGE_DAYS + 1) * 86400
        gate.json.dump(info, open(meta, "w"))
        self.assertIsNone(gate.cached_interp(cache, "k", "r", rundir, route), "an old entry is not trusted")


if __name__ == "__main__":
    unittest.main()
