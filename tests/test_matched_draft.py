"""Static evidence must expose bounded walks and preserve segmented coordinates."""
import sys
import unittest
import contextlib
import io
import shutil
import subprocess
import tempfile
import os
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from matched_draft import dossier, draft, walk
from modules import Module
import matched_draft


class DossierTests(unittest.TestCase):
    def test_disjoint_ranges_and_calls(self):
        # JMP over data; near CALL; far CALL with loader-relocated segment; RETF.
        image = b"\xeb\x02\x00\x00\xe8\x10\x00\x9a\x34\x12\x78\x56\xcb"
        mod = Module("FIXTURE.EXE", 123, "exe", image, {10})
        insns = walk(mod, 0, 0)
        result = dossier(mod, 0, 0, insns)
        self.assertEqual(result["image_ranges"], [
            {"start": 0, "end_exclusive": 2}, {"start": 4, "end_exclusive": 13}])
        self.assertEqual(result["observed_bytes"], 11)
        self.assertEqual(result["calls"], [
            {"ip": 4, "kind": "call", "segment": 0, "offset": 23, "relocated_operand": False},
            {"ip": 7, "kind": "callfar", "segment": 0x5678, "offset": 0x1234,
             "relocated_operand": True}])
        self.assertEqual(result["unwalked_successors"], [])
        self.assertEqual(result["exits"], [{"ip": 12, "kind": "ret"}])
        self.assertEqual(draft(mod, 0, 0, "fixture"), draft(mod, 0, 0, "fixture", insns))

    @unittest.skipUnless(shutil.which("cmake"), "CMake is needed for the draft compile check")
    def test_draft_compiles_in_matched_context(self):
        # MOV, ADD, branch, CALL and RET are synthetic, not game bytes.
        mod = Module("FIXTURE.EXE", 0, "exe",
                     bytes.fromhex("b8030083c0017503e80100c3c3"), set())
        body, _ = draft(mod, 0, 0, "fixture")
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="f117-draft-compile-") as folder:
            tmp = Path(folder)
            (tmp / "fixture.c").write_text(
                '#include "machine.h"\n#include "x86_sem.h"\n'
                'static int room(cpu_t *c, unsigned n) { return c->icount + n <= c->stop_at; }\n'
                'static int guest_call(machine_t *m, uint16_t at, uint16_t ret) { return 0; }\n'
                + body, encoding="utf-8")
            includes = " ".join('"' + (root / "src" / d).as_posix() + '"'
                                for d in ("cpu", "machine", "recomp"))
            (tmp / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.20)\nproject(DraftCompile C)\n'
                'add_library(draft_compile OBJECT fixture.c)\n'
                'set_property(TARGET draft_compile PROPERTY C_STANDARD 11)\n'
                'target_include_directories(draft_compile PRIVATE ' + includes + ')\n'
                'if(MSVC)\n target_compile_options(draft_compile PRIVATE /WX)\n'
                'else()\n target_compile_options(draft_compile PRIVATE -Werror=implicit-function-declaration)\n'
                'endif()\n', encoding="utf-8")
            env = dict(os.environ)
            # CTest need not inherit a developer shell (build.cmd is setlocal).
            # Ask the installed toolchain for its environment without logging it.
            vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio/Installer/vswhere.exe"
            if os.name == "nt" and not shutil.which("cl") and vswhere.is_file():
                install = subprocess.check_output([str(vswhere), "-latest", "-products", "*",
                    "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                    "-property", "installationPath"], text=True).strip()
                setup = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
                env_script = tmp / "compiler-env.cmd"
                env_script.write_text('@echo off\ncall "' + str(setup) + '" >nul\nif errorlevel 1 exit /b 1\nset\n', encoding="mbcs")
                output = subprocess.check_output(["cmd", "/d", "/c", str(env_script)], text=True,
                    env=dict(env, PATH=str(vswhere.parent) + os.pathsep + env.get("PATH", "")))
                env.update(line.split("=", 1) for line in output.splitlines() if "=" in line and not line.startswith("="))
            for command in (["cmake", "-S", str(tmp), "-B", str(tmp / "build")],
                            ["cmake", "--build", str(tmp / "build"), "--config", "Release", "--parallel", "1"]):
                result = subprocess.run(command, capture_output=True, text=True, env=env)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_limited_walk_retains_frontier(self):
        mod = Module("FIXTURE.EXE", 0, "exe", b"\x75\x01\xc3\xc3", set())
        result = dossier(mod, 0, 0, walk(mod, 0, 0, limit=1))
        self.assertEqual(result["unwalked_successors"], [2, 3])

    def test_outside_image_and_indirect_exit(self):
        mod = Module("FIXTURE.EXE", 0, "exe", b"\xff\xe0", set())
        result = dossier(mod, 0, 0, walk(mod, 0, 0))
        self.assertEqual(result["exits"], [{"ip": 0, "kind": "jmpind"}])
        self.assertEqual(result["unwalked_successors"], [])
        self.assertIn("indirect targets are unresolved", result["limitations"][0])
        result = dossier(mod, 0, 5, walk(mod, 0, 5))
        self.assertEqual(result["unwalked_successors"], [5])
        self.assertEqual(result["observed_bytes"], 0)

    def test_com_origin_and_segment_aliases(self):
        mod = Module("FIXTURE.COM", 0, "com", b"\xc3", set(), origin=0x100)
        a = dossier(mod, 0, 0x100, walk(mod, 0, 0x100))
        b = dossier(mod, 0x10, 0, walk(mod, 0x10, 0))
        self.assertEqual(a["image_offset"], 0)
        self.assertEqual(a["image_ranges"], b["image_ranges"])
        self.assertEqual(a["image_sha256"], b["image_sha256"])
        self.assertNotEqual((a["segment"], a["offset"]), (b["segment"], b["offset"]))

    def test_rejects_public_output_and_path_names_before_loading_game(self):
        root = str(Path(__file__).resolve().parents[1])
        base = ["matched_draft.py", "--data", "unused", "--module", "FIXTURE.EXE", "--ip", "0"]
        for extra in (["--out", root], ["--name", "../escape"], ["--seg", "0x10000"]):
            with self.subTest(extra=extra), patch.object(sys, "argv", base + extra), \
                    patch.object(matched_draft, "load_all") as loader, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                matched_draft.main()
            self.assertEqual(error.exception.code, 2)
            loader.assert_not_called()


if __name__ == "__main__":
    unittest.main()
