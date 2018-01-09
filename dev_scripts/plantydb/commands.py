import logging
import shutil
from itertools import repeat, product

from pathlib import Path
from typing import List

import os

from plantydb.common import plantydb_root, dev_scripts_root, auto_tests
from plantydb.generate import generate_fullscan_case, generate_interval_case, generate_multicolumn_case
from plantydb.perf_commands import accumulate_performance_data, diff_perf_mins
from plantydb.shcall import shcall
from plantydb.shops import sh_grep_pref, sh_copy, sh_diff, sh_make, git_record

executable_cmd = "./executable.e {testname}/csv < {testname}/in 1> {testname}/user.out " +\
        "2> {testname}/user.err"
full_cg_args = "valgrind --dump-instr=yes --collect-jumps=yes --tool=callgrind {cg_args} " +\
    "--callgrind-out-file={testname}/callgrind.log"

def run_perf(testname: Path, ntimes):
    open(str(testname / "user.perf"), "w").close()
    for i in range(1, ntimes + 1):
        print("run number {}/{}".format(i, ntimes))
        shcall(executable_cmd.format(testname=testname), info_lines=10)
        sh_grep_pref("perf", testname / "user.err", testname / "user.perf")
    if (testname / "user.perf.min").is_file():
        sh_copy(testname / "user.perf.min", testname / "user.perf.min.prev")
    accumulate_performance_data(testname / "user.perf", testname / "user.perf.min")
    diff_perf_mins(testname / "user.perf.min", testname / "user.perf.min.prev")

def run_cg(testname: Path, cg_args):
    shcall(" ".join([full_cg_args, executable_cmd]).format(testname=testname, cg_args=cg_args),
           info_lines=10)

families = ["info", "plan", "debug", "perf", "query"]

def run_test(testname: Path, apply=False):
    shcall(executable_cmd.format(testname=testname), info_lines=10)
    for family in families:
        sh_grep_pref(family, inp=testname / "user.err", outp=testname / ("user.%s" % family))
    checked = ["plan", "out"]
    for family in families + ["out"]:
        userpath = testname / ("user.%s" % family)
        diffpath = testname / ("diff.%s" % family)
        testpath = testname / family
        if apply:
            sh_copy(userpath, testpath)
        elif family in checked:
            if not testpath.exists():
                logging.info("skipping .%s because reference file does not exist", family)
            else:
                logging.info("comparing .%s", family)
                sh_diff(testpath, userpath, diffpath)

def readlist(s):
    return list(map(int, s.split("-")))

def writelist(l):
    return "-".join(map(str, l))

def try_find_generator(testname: Path):
    testname_normalized = testname.parts[-1]
    generator_name, *args = testname_normalized.split(".")
    if generator_name == "fullscan" and len(args) == 3:
        return generate_fullscan_case, (testname, int(args[0]), int(args[1]), int(args[2]))
    if generator_name == "interval" and len(args) == 1:
        return generate_interval_case, (testname, int(args[0]))
    if generator_name == "multi" and len(args) == 3:
        return generate_multicolumn_case, (testname, readlist(args[0]), readlist(args[1]), int(args[2]))

def symlink_latest(testname):
    p = Path("latest")
    if os.path.islink("latest"):
        p.unlink()
    p.symlink_to(testname)

def run_tests(testnames: List[Path], apply=False):
    if not testnames:
        testnames = list(map(Path, auto_tests()))
    symlink_latest(testnames[0])
    for t in testnames:
        g = try_find_generator(t)
        if g:
            f, a = g
            logging.info("generating %s%s", f.__name__, a)
            f(*a)
        if not t.is_dir():
            logging.error("No dir %s", t)
            continue
        run_test(t, apply=apply)

def run_multi_tests():
    fs = "multi.%s.%s.%d"
    tests = []
    for n in range(1, 5):
        for k in range(0, n + 1):
            args = list(product(*repeat([0, 1], n)))
            for arg1 in args:
                arg2 = [1 - a for a in arg1]
                tests.append(Path(fs % (writelist(arg1), writelist(arg2), k)))
    run_tests(tests)

def build(target):
    sh_make(target, plantydb_root() / "src", "executable.e", Path("compilation.log"))

def new(path: Path):
    path.mkdir(exist_ok=True)
    shutil.copy(str(dev_scripts_root() / "workspace_gitignore"), str(path / ".gitignore"))
    shcall("git init", cwd=path)
    git_record("init", cwd=path)
