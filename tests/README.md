luna-backupagent test and hardening harness
===========================================

Four layers: unit tests on the build host, the sanitizers and valgrind over
them, static analysis and a warning gate over the sources, and a run against
the daemon on a device.

Every script takes a Yocto recipe workdir (`tmp/work/<arch>/<recipe>/<ver>`)
for the toolchain and the sysroot. Any recipe whose sysroot has glib, json-c,
luna-service2 and googletest will do. With `rm_work`, luna-backupagent's own
workdir is gone once it is built; luna-displaymanager's has all four. The
corei7-64 one is the one the host can run.

Unit tests (`unit/`, `fakes/`)
------------------------------

gtest, built from the daemon's own objects (the `backupagent-core` object
library, everything but `main()`) and linked with:

- `fakes/fakebus.cpp`: luna-service2, in process. The test executable is not
  linked with the real library. It records registrations and replies, calls
  the daemon's methods as a client would (`callMethod`), fails any LS call on
  request (`failNext`), and counts the LSErrors and handles that are still
  outstanding. Every test's TearDown checks that both counts are back to
  zero.
- `fakes/fakejson.cpp`: the json-c calls the daemon makes that can fail
  (constructors, adds, serialisation), forwarded to the real json-c. The
  n-th call can be made to fail (`fakejson::failAt`) the way json-c fails:
  NULL, or -1 with the value left with the caller.

Every test also fails on any critical or warning from GLib itself. A
`g_return_if_fail()` tripping inside glib is the only sign of a misuse
there: glib is not built with the sanitizers, so ASan does not see a freed
pointer that is read inside it.

| File | What it pins |
|---|---|
| `test_files.cpp` | only regular files are listed, in list order; missing files, directories, FIFOs, symlinks (dangling or not) are left out |
| `test_prebackup.cpp` | the reply woce-backup reads (version, description, files; no returnValue:false, no hasMore); every json-c failure gives NULL rather than a reply missing files; the callback replies exactly once, and validly, on every failure and every payload |
| `test_postrestore.cpp` | `{"files": array}` accepted; anything else (no payload, `[]`, `null`, wrong types, truncated, 100000-deep nesting) refused, with errorText, but still answered |
| `test_daemon.cpp` | `runBackupAgent()`, all of `main()`: serves until a real SIGTERM or SIGINT and exits 0; exits 1 when init fails at any step, with the partial registration dropped; leaves no signal source behind to fire on a freed main loop |
| `test_lifecycle.cpp` | registration of both methods; every init() step failing cleanly; destroy() unregistering exactly once; the installed `files/` (backup registration, API, role, groups, perm, service) naming exactly what the code registers |

Build and run on the host:

    cmake -DBUILD_TESTING=ON ...      # with the recipe's toolchain.cmake
    ninja backupagent-tests
    <sysroot>/usr/lib/ld-linux-x86-64.so.2 --library-path <sysroot>/usr/lib \
        tests/backupagent-tests

On a device, copy the armv7 or aarch64 build (gtest is linked statically)
together with `files/`, and point the tests at the copy:

    BA_SOURCE_DIR=<dir with files/> ./backupagent-tests

There, the preBackup test runs against the device's real preference files.

Hardening (`hardening/`)
------------------------

- `run-sanitizers.sh <corei7-64 workdir>`: the tests under ASan+UBSan and
  TSan, each built with gcc and with clang, and under valgrind memcheck on
  the image toolchain's own build. clang also gets its integer and
  nullability checks; its unsigned wrap-around check is left out because
  libstdc++ relies on unsigned wrap-around, which is defined behaviour. The
  image toolchain ships no sanitizer runtimes, so the host's gcc and clang
  build against the sysroot (`host-sysroot.cmake`); the host's glibc is the
  sysroot's. The allocation-failure tests are what make the leak checkers
  worth running here: each early exit has to free what it built.
- `static-analysis.sh <corei7-64 workdir>`: gcc `-fanalyzer` at -O0 and -O2,
  the clang analyzer with the alpha checkers, clang-tidy, cppcheck
  (exhaustive, inconclusive), flawfinder and Coccinelle (`cocci/`). It fails
  on any finding not in `baseline.txt`, where each entry that was looked at
  and left has its reason. cppcheck's style notes are listed but do not
  gate. sparse is not run: it parses C only.
- `build-all.py <workdir>...`: the warning gate. The daemon and the tests
  are built for each architecture given, with the image's gcc and with the
  host's clang, at `-Wall -Wextra`; a warning in `Src/` or `tests/` fails it.
  Run it with the armv7, aarch64 and corei7-64 workdirs.
- `mutation-check.py <corei7-64 workdir>`: shows the tests can fail. It
  builds thirteen mutants of `Src/`: following symlinks, dropping a json-c
  failure check, leaking or double-freeing a reply, accepting any
  postRestore payload, leaking an LSError or the bus handle, main()'s old
  signal handling, and a signal that does not stop the loop. Each must be
  caught by a test, by LeakSanitizer, by a timeout, or, where the fault is
  inside uninstrumented code, by json-c's refcount assertion or GLib's own
  critical.

The Coccinelle rules are checked against `cocci/selftest.cpp`, which has the
patterns they look for, so a rule that silently matches nothing fails the
gate instead of passing it.

On the device (`device/`)
-------------------------

`run-on-device.sh [iterations]`, as root on the device, against the installed
daemon. It changes nothing: preBackup only lists and postRestore only
acknowledges. It checks the `/etc/palm/backup` registration, that every file
preBackup lists is absolute, regular, not a symlink and listed once, that
postRestore accepts the protocol payload and refuses hostile ones while
still answering, and that it never restarts.

For memory, it runs a warm-up batch, then two equal batches of calls, and
judges the second: a leak of even one 8-byte allocation per call adds at
least `3 * iterations * 8` bytes to it. With gdb on the device the measure
is malloc's own count of bytes in use (`malloc_stats()`, read back from the
journal), not RSS. RSS only moves in 4 kB pages and also rises once while
glib and luna-service2 size their buffers to the traffic.

On a TouchPad (tenderloin, armv7), the daemon settled within its first few
hundred calls and then stayed within about ±150 bytes over about 45,000
more. A build that leaks the reply object fails the check with about 1 kB
per call. valgrind is installed in that image but cannot run any dynamically
linked program there (the loader gets EBADF stat'ing its libraries), so
memcheck on the device is not an option yet.
