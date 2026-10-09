# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Campus print-shop scheduler: an OS course project (C++17 + Qt 6 Widgets + CMake) built by three people. Project docs are in Chinese. `docs/interfaces.md` is the binding contract; if you change fields, state transitions, IPC messages or exit codes, update it in the same commit.

## Build and test

Toolchain: CMake >= 3.24, Ninja, Qt 6.10.2 (CMake accepts >= 6.5), Python >= 3.10 (tests use only the standard library). On Windows, use MSVC x64 from a VS 2022 Developer shell with the matching MSVC Qt kit. Don't mix in MinGW.

```sh
python tools/setup_pdfium.py            # once per checkout: pinned PDFium into .tools/pdfium; without it configure warns and PDF preview is off
cmake --preset dev -DCMAKE_PREFIX_PATH="<Qt dir, e.g. C:/Qt/6.10.2/msvc2022_64>"
cmake --build --preset dev --parallel 4
ctest --preset dev                      # Windows: put <Qt>/bin on PATH first; headless Linux: QT_QPA_PLATFORM=offscreen
cmake --preset core && cmake --build --preset core && ctest --preset core   # pure core, no Qt/Python
```

- Presets: `dev` (Debug), `core` (sets `CPS_BUILD_QT_APPS=OFF`), `ci` (Release). Each builds into `build/<preset>/` and puts every executable in `build/<preset>/bin/`. **All presets set `CPS_WARNINGS_AS_ERRORS=ON`** (`/W4 /permissive-` or `-Wall -Wextra -Wpedantic`).
- Put a personal Qt path in `CMakeUserPresets.json` (gitignored), never in `CMakeLists.txt`.
- Run one CTest test with `ctest --preset dev -R '^<name>$'`. `-R` is a regex, so anchor it: a bare `shop` also matches `shop_integration` and `shop_experiments`. The `core` preset has `scheduler`, `scheduler_advanced`, `shop`, `estimate` and `metrics_replay`. Qt builds add `runtime_state`, `shop_integration`, `coordinator_integration`, `gui`, `merchant`, `merchant_dispatch`, `classroom_web`, `shop_experiments`, `document_workload`, `package_layout`, `native_test_runner`, `workload_tools`, `process_integration`, `control_integration`, and `windows_console_audit` (Windows only).
- `gui_tests` and `merchant_tests` use QtTest, so you can run a single slot: `QT_QPA_PLATFORM=offscreen build/dev/bin/gui_tests shopWorkflow`. The other C++ tests are plain executables that throw on the first failed check (`test::require`/`near`/`throws` in `tests/core_test_support.hpp`; `shop_tests.cpp` and `merchant_dispatch_tests.cpp` define their own `check`). They can't be filtered and always run the whole binary.
- The Python tests are driven by CTest, which passes them the built binaries (`--supervisor "$<TARGET_FILE:...>"` etc.). If you run one by hand, pass those paths yourself.
- Office preview uses a LibreOffice headless converter found via `CPS_OFFICE_EXECUTABLE`, then a packaged `office/` dir, then a system install, then PATH. `python tools/setup_office_preview.py` puts a pinned private copy in `.tools/libreoffice` on Windows/macOS. Most Office tests use `office_converter_stub`. The real-format `gui_tests` slots skip unless `CPS_OFFICE_FIXTURES` points at the output of `tools/generate_office_fixtures.py --office <soffice> --output-dir runtime/office-fixtures`.
- CI (`.github/workflows/ci.yml`) builds and tests the `ci` preset on Windows, macOS and Ubuntu with Qt 6.10.2, after installing PDFium and LibreOffice and generating the Office fixtures. On Windows and macOS it then runs `tools/run_native_gui_tests.py`, which reruns a hardcoded list of `gui_tests` slots (plus `merchant_tests` slots on Windows) on the native platform plugin. It packages the app with `tools/package_app.py --office-dir ...` and checks the package with `tests/package_smoke.py` and, on a clean runner, `tools/verify_package.py`.
- Tests must still work in Release builds. Use explicit check functions, not `assert()` (which `NDEBUG` removes). Use injected or explicit times instead of real sleeps to check timing.

Running things:

```sh
./build/dev/bin/campus-print --data-dir runtime/shop-demo                                  # owner console (--role boss, default)
./build/dev/bin/campus-print --merchant-showcase                                           # owner UI on in-memory sample data, no backend
./build/dev/bin/campus-print --demo-mode --demo-workload --data-dir runtime/shop-demo      # classic owner console with demo orders
./build/dev/bin/campus-print --role student --student 2024000001 --data-dir runtime/shop-demo   # student client, same backend
./build/dev/bin/print-coordinator --shop-mode --state-dir runtime/service --socket-name campus-print-dev
./build/dev/bin/print-coordinator --state-dir <dir> --request '{"type":"snapshot"}'   # send one IPC command
./build/dev/bin/print-supervisor --job examples/demo-job.json --output-dir runtime/demo
./build/dev/bin/shop-experiments --seed 20260923 --customers 600 --hours 8 --output runtime/shop-experiments.json
./build/dev/bin/shop-experiments --input <workload.json> --hours 8 --output <report.json>   # fixed orders, no customer-choice model
python tools/classroom_web.py --coordinator build/dev/bin/print-coordinator --state-dir runtime/classroom-demo   # phone web gateway
```

- Window selection in `src/gui/main.cpp`: `--role boss` opens `MerchantWindow` (the 纸间 owner console) unless you pass `--classic-owner`, `--legacy-mode` or `--demo-workload`, which open the older `PrintWindow`. `--role student` rejects `--legacy-mode` and `--demo-workload`, and `--demo-workload` requires `--demo-mode`. On Windows, `campus-print-merchant` is a second executable built from the same `main.cpp` that differs only in its icon.
- `--page-ms` (default 150) sets the simulated time per printed side. It only applies when this launch starts the coordinator; a coordinator that is already running keeps its own value.
- Keep runtime data under `runtime/` (gitignored). Without `--data-dir`, the GUI stores data under `QStandardPaths::AppLocalDataLocation` in `shop/` (or `queue/` with `--legacy-mode`). On Windows, `--daemon` is intentionally unsupported and exits with 69; POSIX daemon mode needs absolute paths.
- Optional Python dependencies: `qrcode` for the classroom gateway (`tools/classroom/requirements.txt`; `start-classroom-demo.ps1` looks for it in `runtime/classroom-python`), `pypdf` for `tools/run_document_experiments.py`, and matplotlib for `tools/plot_experiments.py` (`experiments/requirements.txt`).

## Architecture

Libraries are layered, and each layer must only depend on the ones below it:

- **`cps_core`** (`include/cps/core`, `src/core`, `src/metrics`): pure C++17 with no Qt and no OS headers. It contains `Scheduler` (thread-safe queue plus the job state machine), shop pricing, printer matching and inventory (`shop.hpp`), the wait forecast (`estimate.hpp`), replay, and statistics.
- **`cps_runtime`** (`include/cps/runtime`, `src/runtime`, `src/platform`): depends on Qt Core. It handles Job JSON I/O, the persistent `state.json` store (`state_store`, `shop_io`), and platform code. POSIX `fork`/`setsid` daemonization and the Windows binary-stdio setup live only in `src/platform/`.
- **`cps_gui`** (`src/gui`): Qt Widgets. It contains `PrintController` (the IPC client on a worker thread), the three windows, document/Office preview, and the merchant (`merchant_*`) and student/editorial (`editorial_*`, `print_proof`, `paper_*`) views.
- **Executables**: `print-worker` (simulated printing with atomic output), `print-supervisor` (runs one job and restarts the worker on crash or timeout), `print-coordinator` (a persistent background service, `src/coordinator/main.cpp`), `campus-print` (the GUI), and the tools `scheduler-replay` and `shop-experiments`.

Runtime process chain: `campus-print` → `QLocalSocket` JSONL → `print-coordinator` → one `print-supervisor` per running job → `print-worker`. The supervisor and worker talk over stdout JSONL. The GUI starts the coordinator automatically, and the coordinator keeps running after the window closes. The local server name comes from the SHA-256 of the canonical state-dir path, compared case-insensitively on Windows. `tools/classroom_web.py` is a second client: a stdlib HTTP server that lets phones upload, get quotes and order. It reaches the same coordinator by calling `print-coordinator --request`, exposes only student operations (no general IPC forwarding), and doesn't change the persisted schema.

Each process finds the next one with `cps::siblingExecutable()` (in `job_io`), which looks in its own directory. That's why every target goes into one flat `bin/` directory (no macOS `.app` bundle in dev builds), and why packaging has to keep them together. A coordinator state dir contains `state.json`, `jobs/`, `output/`, `control/` and `.coordinator.lock`.

IPC command types (in `src/coordinator/main.cpp`): `snapshot`, `stats`, `quote`, `submit`, `submit_batch` (all-or-nothing), `cancel`, `set_policy`, `set_paused`, `set_printer`, `restock`, `refund`, `shutdown`, plus `crash_worker`, which only works when the coordinator runs with `--demo-mode`. Replies echo `request_id`. Asynchronous `job_state` and forwarded supervisor/worker events share the same connection and have no `request_id`.

These invariants cut across files, so keep them intact:

- **The coordinator is the single source of truth.** Clients only hold snapshots and send commands; they never write `state.json`. Every change is persisted (atomically, with `QSaveFile`) before the coordinator replies `accepted`, and clients don't automatically resend a command whose reply was lost. On `claim`, the coordinator saves Running plus the attempt count *before* spawning the supervisor.
- **Attempt tokens:** `Job::attempts` goes up by one on every claim. `finish`, `retry` and `acknowledgeCancel` must pass that token, and stale tokens are rejected. This coordinator attempt count is separate from the supervisor's internal worker-restart count.
- **Recovery reconciliation:** after a restart, the coordinator never re-runs an old Running or Cancelling job blindly. It first checks for final output and for the `.<job>.supervisor.lock` and `.<job>.output.lock` locks (`QLockFile`), and only then calls `retry()` or `acknowledgeCancel()`. Running-job cancel intent is stored in `control/<job_id>.<attempt>.cancel`.
- **Shared rules:** `claimFor(policy, now, fleet, idle, supplied)` does device matching, busy checks and inventory reservation under one mutex. `waitingOrder()` uses the same comparator as `claim()`, so the GUI must not re-implement policy ordering.
- **Forecasts come from the backend.** `estimateWaits()` restores the snapshot into a throwaway `Scheduler` and replays the real `claim`/`claimFor`, so estimated start/finish times follow the same rules as dispatch. The coordinator attaches them to `snapshot` rows and `quote` replies; the GUIs only word them (`src/gui/view_text.hpp`). They are estimates (no new arrivals, failures or pauses assumed), so never present them as promises.
- **Several clients, one state.** Every window except `--merchant-showcase` owns a `PrintController` on a worker thread (the merchant console through `MerchantData`) and may start the coordinator. Clients share orders only when they use the same `--data-dir`/`--state-dir`. The student client filters orders by `Job::customer` and hides other orders' titles, but that is presentation only: the IPC has no authentication. Payment (`Job::payment`) is simulated and only recorded, even where real QR-code images are shown; revenue is still recognised on completion.
- **Documents stay in the client.** The student window reads PDFs, images and text in-process through PDFium (loaded dynamically by `document_preview`). `office_preview` converts Office files to a temporary PDF in a separate LibreOffice process, asynchronously, with a 90 s timeout and cancellation. Document bytes and temp paths never enter IPC; the backend receives only the file name, page count, copies and print options.
- **The merchant 调度工作台 is a teaching sandbox.** It runs the five policies through the core scheduler on in-memory sample orders. It doesn't read the live queue or switch the real dispatch policy (that is `set_policy` from the shop menu).
- **Units:** times are integer milliseconds on one persisted timeline (injectable `Clock`). Money is integer cents, and the backend computes quotes and deadlines; client-supplied prices are ignored (the `shop-experiments --input` workloads too).
- **Versioning:** the IPC envelope and Job JSON are `schema_version: 1`. Persisted `state.json` is schema 3 in shop mode (schema 2 plus `payment`/`customer`) and schema 1 in the legacy homogeneous-queue mode (`--legacy-mode` in the GUI, or a coordinator without `--shop-mode`). Readers accept 1–3. Older binaries refuse a newer schema so they can't drop fields they don't know; bump it when you add persisted fields.
- Success means a worker exit code of 0 *plus* committed output; progress reaching 100% does not count. Exit codes (64/69/70/73/74/75/78/130) are listed in `docs/interfaces.md`.

Policies: `fcfs`, `sjf`, `priority` (priority with aging), `deadline` and `revenue`. `docs/shop.md` covers pricing, service tiers, paper vs. printed-side accounting, how revenue is scored, and the fixed-workload experiment format.

## Docs to read before changing an area (from AGENTS.md)

- Student UI, document preview, payment display or animation: `docs/student-handoff.md` (module map, accepted behaviour, animation timings and caches to keep), with visual evidence in `docs/editorial-frontend.md`.
- Office preview: `docs/office-preview.md` (converter discovery, sandboxing, packaging, acceptance).
- Owner console: `docs/merchant-frontend.md`. Classroom web demo: `docs/classroom-demo.md`.
- Scheduling, persistence or IPC: `docs/team.md` and `docs/interfaces.md`. Keep the student and owner clients protocol-compatible.
- Packaging and release: `docs/release.md`; background-process cleanup: `docs/clean-up.md`.

## Conventions (from CONTRIBUTING.md, README and AGENTS.md)

- The team commits directly to `main` unless the user names another branch. Check `git status`, the current branch and the remotes first, `git pull --rebase` before pushing, never force-push, and use scoped conventional messages such as `feat(scheduler): ...` or `fix(gui): ...`.
- The WeChat/Alipay receipt images in `assets/editorial/payment-codes/` are committed publicly with the user's authorization; runtime data and build outputs stay in ignored dirs (`runtime/`, `build/`, `dist/`, `.tools/`).
- Module ownership: A = core scheduling, metrics and replay tools; B = runtime, worker, supervisor, coordinator and platform; C = GUI, packaging and cross-platform integration. Keep changes to public interfaces, the top-level CMake and CI small, and don't reformat other modules along the way.
- Cross-platform rules:
  - Keep `windows.h`, `unistd.h` and Qt GUI headers out of the core.
  - Launch child processes with `QProcess` and an argument list, and apply `hideProcessConsole()` (`include/cps/platform/process.hpp`) so no console window flashes on Windows. Never use `system()` or shell kill/sleep, and never hardcode `.exe`.
  - Build paths with `QDir`/`QStandardPaths`; no hardcoded drives or `/tmp`.
  - `#include` case must match the real filename (Linux CI catches mismatches).
  - Use UTF-8 and LF everywhere (MSVC uses `/utf-8`).
  - When touching file or process code, test with paths that contain Chinese characters and spaces.
- Never describe experiments or CI as passing unless they actually ran. Keep simulated guarantees (atomic output, retry, payment) separate from real-printer or real-payment claims.
