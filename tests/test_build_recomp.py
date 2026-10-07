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


if __name__ == "__main__":
    unittest.main()
