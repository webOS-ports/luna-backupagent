#!/usr/bin/env python3
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# mutation-check.py [--work DIR] <corei7-64 recipe workdir>
#
# Shows the tests can fail: each mutant below breaks one behaviour they are
# meant to pin, in a copy of the tree, and the ASan+UBSan build (host gcc on
# the recipe's sysroot, as run-sanitizers.sh builds it) has to fail on it -
# through an assertion, or through ASan/LSan for the ownership mutants. The
# unmutated copy has to pass first, or nothing below means anything.

import argparse, glob, os, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--work", default=os.path.expanduser("~/claude-scratch/luna-backupagent/mutants"))
ap.add_argument("workdir")
opts = ap.parse_args()

SRC = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CPP = "Src/BackupAgent.cpp"
DAEMON = "Src/Daemon.cpp"

# name, file, text, replacement, what should catch it (text and replacement
# may be parallel lists, for a mutant that needs more than one edit)
MUTANTS = [
    ("stat-follows-links", CPP, "lstat(candidates[i], &st)", "stat(candidates[i], &st)",
     "FilesForBackup.SymlinkToRegularFileIsLeftOut"),
    ("entry-failure-ignored", CPP,
     "                json_object_put(entry);\n            ok = false;",
     "                json_object_put(entry);",
     "PreBackup.EveryAllocationFailureGivesNull"),
    ("add-failure-leaks-value", CPP,
     "    if (json_object_object_add(object, key, value) != 0) {\n        json_object_put(value);",
     "    if (json_object_object_add(object, key, value) != 0) {",
     "LeakSanitizer"),
    ("files-put-twice", CPP,
     "    if (!ok) {\n        json_object_put(response);\n        return NULL;\n    }\n    return response;",
     "    if (!ok) {\n        json_object_put(files);\n        json_object_put(response);\n        return NULL;\n    }\n    return response;",
     # The second put happens inside json-c, which is not instrumented, so
     # ASan does not see it; json-c's own refcount assertion does.
     "json_object_put: Assertion"),
    ("reply-leaks-response", CPP,
     "    if (response)\n        json_object_put(response);\n}",
     "}",
     "LeakSanitizer"),
    ("no-fallback-on-null-text", CPP,
     "    if (!payload)\n        payload = fallback;\n", "",
     "PreBackup.CallbackRepliesOnceOnEveryAllocationFailure"),
    ("files-type-unchecked", CPP,
     "\n             && json_object_is_type(files, json_type_array);", ";",
     "PostRestore.RefusesAnythingElse"),
    ("category-error-not-freed", CPP,
     "        g_warning(\"Failed registering with service bus category: %s\", error.message);\n        LSErrorFree(&error);",
     "        g_warning(\"Failed registering with service bus category: %s\", error.message);",
     "InitFailure.ReportedAndCleanedUp"),
    ("destructor-keeps-handle", CPP, "    if (m_service) {\n        LSError error;",
     "    if (false && m_service) {\n        LSError error;",
     "Lifecycle.DestroyUnregistersOnce"),
    ("postrestore-always-true", CPP,
     "json_object_new_boolean(valid)", "json_object_new_boolean(true)",
     "PostRestore.RefusesAnythingElse"),
    # What main() did before: each signal source removed only when it fires,
    # so the other one outlives its main loop.
    ("signal-source-outlives-loop", DAEMON,
     ["    return G_SOURCE_CONTINUE;\n", "    g_source_remove(sigterm);\n    g_source_remove(sigint);\n"],
     ["    return G_SOURCE_REMOVE;\n", ""],
     # The stale source reads its freed loop inside glib, which ASan does
     # not see; glib's own ref_count check does, and the fixture fails on it.
     "GLib complained: g_main_loop_quit"),
    ("failed-start-keeps-handle", DAEMON,
     "    BackupAgent::destroy();\n\n    g_source_remove(sigterm);",
     "    if (status == 0)\n        BackupAgent::destroy();\n\n    g_source_remove(sigterm);",
     "DaemonInitFailure.ExitsOneAndCleansUp"),
    ("signal-does-not-quit", DAEMON,
     "    g_main_loop_quit(static_cast<GMainLoop*>(user_data));\n", "",
     "timeout"),
]

S = os.path.join(opts.workdir, "recipe-sysroot")
N = os.path.join(opts.workdir, "recipe-sysroot-native")
env = dict(os.environ, PATH=os.path.join(N, "usr/bin") + os.pathsep + os.environ["PATH"],
           PKG_CONFIG_SYSROOT_DIR=S,
           PKG_CONFIG_LIBDIR=S + "/usr/lib/pkgconfig:" + S + "/usr/share/pkgconfig",
           PKG_CONFIG_PATH=S + "/usr/lib/pkgconfig",
           ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:fast_unwind_on_malloc=0",
           UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1",
           G_SLICE="always-malloc", DEBUGINFOD_URLS="")

rt = os.path.join(opts.work, "hostrt")
os.makedirs(rt, exist_ok=True)
for lib in ("libasan.so.8", "libubsan.so.1", "libstdc++.so.6", "libgcc_s.so.1"):
    f = subprocess.run(["gcc", "-print-file-name=" + lib], capture_output=True, text=True).stdout.strip()
    if os.path.exists(f):
        dst = os.path.join(rt, lib)
        if os.path.lexists(dst):
            os.remove(dst)
        os.symlink(os.path.realpath(f), dst)


def build_and_run(name, mutate=None):
    tree = os.path.join(opts.work, name, "src")
    build = os.path.join(opts.work, name, "build")
    shutil.rmtree(tree, ignore_errors=True)
    shutil.copytree(SRC, tree, ignore=shutil.ignore_patterns(".git"))
    if mutate:
        path, olds, news = mutate
        # A mutant is one edit, or several given as parallel lists.
        if isinstance(olds, str):
            olds, news = [olds], [news]
        p = os.path.join(tree, path)
        text = open(p).read()
        for old, new in zip(olds, news):
            if text.count(old) != 1:
                return None, "mutation does not apply (%d matches)" % text.count(old)
            text = text.replace(old, new)
        open(p, "w").write(text)
    cfg = subprocess.run(["cmake", "-S", tree, "-B", build, "-G", "Ninja",
                          "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(tree, "tests/hardening/host-sysroot.cmake"),
                          "-DBA_SYSROOT=" + S, "-DBA_HOST_CC=gcc", "-DWEBOS_INSTALL_ROOT=/",
                          "-DBUILD_TESTING=ON",
                          "-DBA_SANITIZE=address,undefined", "-DCMAKE_CXX_FLAGS=-g -O1",
                          "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--dynamic-linker=%s/usr/lib/ld-linux-x86-64.so.2 "
                          "-Wl,--disable-new-dtags -Wl,-rpath,%s:%s/usr/lib" % (S, rt, S)],
                         capture_output=True, text=True, env=env)
    if cfg.returncode:
        return None, "configure failed:\n" + cfg.stdout[-2000:] + cfg.stderr[-2000:]
    b = subprocess.run(["ninja", "-C", build, "backupagent-tests"], capture_output=True, text=True, env=env)
    if b.returncode:
        return None, "build failed:\n" + b.stdout[-2000:]
    try:
        r = subprocess.run([os.path.join(build, "tests/backupagent-tests")], capture_output=True, text=True,
                           env=env, timeout=120)
    except subprocess.TimeoutExpired:
        return -1, "timeout: the tests did not finish"
    return r.returncode, r.stdout + r.stderr


fail = False
rc, out = build_and_run("unmutated")
if rc != 0:
    print("FAIL the unmutated tree does not pass:\n" + (out or "")[-3000:])
    sys.exit(1)
print("ok   unmutated tree passes")

for name, path, old, new, catcher in MUTANTS:
    rc, out = build_and_run(name, (path, old, new))
    if rc is None:
        print("FAIL %-26s %s" % (name, out))
        fail = True
    elif rc == 0:
        print("FAIL %-26s survived: no test failed" % name)
        fail = True
    elif catcher not in out:
        print("ok   %-26s killed, but not by %s" % (name, catcher))
    else:
        print("ok   %-26s killed by %s" % (name, catcher))

sys.exit(1 if fail else 0)
