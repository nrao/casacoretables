# AGENTS.md — casacoretables

Guidance for AI agents (and humans) working on **casacoretables**. Read this
before making changes; it captures the architecture and the non-obvious
gotchas that are easy to get wrong.

---

## 1. What this package is

A **standalone, minimal-dependency build of casacore's table system** with a
**pybind11** Python interface that mirrors `python-casacore`'s `casacore.tables`.

It bundles **only** casacore's `casa` (base) and `tables` modules — nothing
else — and links them as one static library inside a single Python extension.
The goal is "few dependencies, easy to build/install," and a drop-in-ish
replacement for `python-casacore`'s table API that does **not** drag in Boost or
the rest of casacore.

Origin: vendored/trimmed from `/Users/jsteeb/Dropbox/viper_dev/casacore`
(casacore 3.8.0). The Python layer is ported from
`/Users/jsteeb/Dropbox/viper_dev/python-casacore` (which uses Boost.Python; we
reimplemented the bindings in pybind11).

---

## 2. Repository layout

```
casacoretables/
├── pyproject.toml              # scikit-build-core + pybind11 packaging
├── CMakeLists.txt              # top-level: extern lib + module + (optional) C++ tests
├── README.md
├── AGENTS.md                   # this file
├── PROMPTS.md                  # the prompts that produced this package
├── extern/
│   ├── CMakeLists.txt          # builds the casacore_tables STATIC lib
│   └── casacore/               # VENDORED, TRIMMED casa/ + tables/ sources
│       ├── casa/   (~198 .cc)  # base module: Arrays, Containers, IO, OS, Quanta, ...
│       └── tables/ (~181 .cc)  # Tables/, DataMan/, TaQL/, LogTables/, AlternateMans/
├── bindings/                   # the pybind11 extension (C++)
│   ├── CMakeLists.txt
│   ├── module.cc               # PYBIND11_MODULE(_tables): Table/TableRow/TableIter/TableIndex
│   ├── convert.h / convert.cc  # numpy<->casacore (Array/ValueHolder/Record/String/IPosition)
│   └── casters.h               # pybind11 type_caster specializations
├── src/casacoretables/
│   ├── __init__.py
│   ├── tables/                 # Python layer ported from python-casacore
│   │   ├── __init__.py table.py tablecolumn.py tablerow.py
│   │   ├── tableiter.py tableindex.py tableutil.py tablehelper.py
│   │   └── _tables*.so         # the compiled extension is installed here
│   └── util/                   # substitute.py (TaQL $var substitution) + __init__
├── tests/
│   ├── cpp/                    # vendored casacore C++ tests + assay harness
│   │   ├── CMakeLists.txt
│   │   ├── run_assay.sh        # per-test temp-dir runner (see gotchas)
│   │   ├── casacore_assay casacore_floatcheck   # vendored from casacore build-tools
│   │   └── Tables/ DataMan/ TaQL/ LogTables/    # *.cc + *.run/.in/.out helpers
│   └── python/                 # ported python-casacore tests
│       └── test_table.py test_unicode.py test_util.py
├── docs/
│   └── casacoretables_api_tour.ipynb   # full Python API tour (runnable)
└── .github/workflows/          # black, linux/macos pytest, C++ ctest, cibuildwheel publish
```

---

## 3. Build environment & how to build

**Always use the conda `zinc` environment** (`conda activate zinc`).

**Build prerequisites** (not Python packages):
- A C++17 compiler.
- CMake ≥ 3.18 and Ninja (scikit-build-core fetches these from PyPI if absent).
- **`bison` ≥ 3 and `flex`** — generate the TaQL (`RecordGram`, `TableGram`) and
  Json (`JsonGram`) parsers.

**Platforms.** The package builds on **Linux and macOS** (the CMake is portable;
the only platform-conditional is the ELF `--exclude-libs,ALL` link option,
guarded for non-Apple). The CI in `.github/workflows/` builds it on both. The
only differences are how you get `bison`/`flex` and the exact tool flags:
- **Linux:** `apt-get install -y bison flex` (Ubuntu's bison is 3.8). It lands on
  `PATH`, so CMake's `find_package(BISON 3.0)` finds it — **do not pass
  `-DBISON_EXECUTABLE`**. This is exactly what `python-testing-linux.yml` /
  `cpp-tests.yml` do.
- **macOS:** the *system* `bison` is **2.3 — too old**. Use a conda/Homebrew/
  MacPorts bison 3.x: `conda install -n zinc bison flex`, or point CMake at
  `/opt/homebrew/opt/bison/bin/bison` (Homebrew) / `/opt/local/bin/bison`
  (MacPorts). `zinc` already has conda bison 3.8.

### Wheel / install
```bash
conda activate zinc                       # has bison 3.8 + flex
cd casacoretables
pip install .                             # builds the wheel via scikit-build-core
# or a redistributable wheel:
pip wheel . --no-deps -w dist_wheel
```

### Dev / iterating on the C++ extension
```bash
conda activate zinc
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_PYTHON_MODULE=ON -DBUILD_CPP_TESTS=OFF \
  -Dpybind11_DIR=$(python -m pybind11 --cmakedir)
# On macOS, if the system bison 2.3 gets picked up, add e.g.
#   -DBISON_EXECUTABLE=/opt/homebrew/opt/bison/bin/bison
# On Linux the apt/conda bison on PATH is found automatically (no flag needed).
ninja -C build _tables
cp build/bindings/_tables*.so src/casacoretables/tables/   # use in-place via PYTHONPATH=src
```
The in-place `.so` is gitignored; the wheel is the real install path.

### C++ tests
```bash
cmake -S . -B build -G Ninja -DBUILD_CPP_TESTS=ON -DBUILD_PYTHON_MODULE=OFF
cmake --build build --parallel
cd build && ctest --output-on-failure -j4    # 100/100 pass
```

CMake options (top-level `CMakeLists.txt`): `BUILD_PYTHON_MODULE` (default ON),
`BUILD_CPP_TESTS` (default OFF).

---

## 4. CRITICAL gotchas (most bugs live here)

### 4.1 Dropbox breaks casacore table locking
The repo lives under **Dropbox**, whose file-sync interferes with casacore's
table lock files → errors like *"table … is in use in another process"* and
`rm -rf` reporting *"Directory not empty"*. Symptom: C++ tests fail in the build
tree but pass in `/tmp`. **Fix already in place:** `tests/cpp/run_assay.sh` runs
every C++ test in a private `$TMPDIR` directory. Never run casacore table I/O
directly inside the Dropbox-synced tree during tests. (See also the runtime
lock options `lockoptions="autonoread"` and the `table.nolocking` aipsrc switch.)

### 4.2 `conda activate <env>` does NOT switch Python in non-interactive shells here
In the Bash tool, `conda activate casa` often leaves `python` pointing at
`zinc`'s 3.13. **Always use absolute interpreter paths** when targeting a
specific env, e.g. `/Users/jsteeb/miniforge3/envs/casa/bin/python`. Verify with
`which python` / `python --version` before trusting an activation.

### 4.3 Symbol visibility — MUST stay hidden (CASA coexistence)
The extension statically links ~16k casacore symbols. They are **hidden** so the
`.so` exports only `_PyInit__tables`. This lets `casacoretables` and CASA's
`casatools` (which embeds a *different, ABI-incompatible* casacore) live in the
same process without symbol interposition/crashes. **Do not remove** the
visibility settings:
- `extern/CMakeLists.txt`: `CXX_VISIBILITY_PRESET hidden`, `VISIBILITY_INLINES_HIDDEN ON`.
- `bindings/CMakeLists.txt`: same on `_tables`, plus `LINKER:--exclude-libs,ALL` on ELF.

Verify after changes (must print 0 exported casacore symbols). Exported symbols
live in different places per platform, so the command differs:
```bash
# macOS (BSD nm: external symbols)
nm -gU build/bindings/_tables*.so | grep -c casacore                 # must be 0
# Linux (ELF: check the DYNAMIC symbol table, not .symtab)
nm -D  build/bindings/_tables*.so | grep -E ' [TSDB] ' | grep -c casacore   # must be 0
#   (equivalently: readelf --dyn-syms … | grep -c casacore, or objdump -T)
```

### 4.4 Axis order — column-major ↔ row-major reversal
casacore Arrays are **column-major** (first axis fastest); numpy is row-major.
The bindings **reverse axes at the boundary** so users work in numpy convention,
matching python-casacore. This applies to:
- Array data (`array_to_numpy`/`numpy_to_array` reverse the shape), and
- **every `IPosition`** — shapes *and* slice corners `blc`/`trc`/`inc` — via
  `py_to_iposition`/`iposition_to_py` (they reverse). This mirrors casacore's
  `casa_reversed_variable_capacity_policy`. Getting this wrong silently
  transposes data or throws "Slicer error: endResult>=shape". Don't "simplify"
  the reversal away.

### 4.5 String columns are NOT numpy arrays
To match python-casacore, a String column/cell crosses as a **Python `list`**
(1-D) or a `{"shape": [...], "array": [flat list]}` **dict** (N-D) — *not* a
numpy array, and **without** axis reversal (native casacore order, see
`string_array_to_py`/`string_array_from_dict`). Tests rely on this
(`test_unicode` compares `getcol` results with `==`).

### 4.6 C++17, and the dropped Sisco files
casacore selects C++20 only when `BUILD_SISCO=ON`; we build **C++17**. The
`tables/AlternateMans/Sisco*.{cc,h}` and `BitFloat.h` use C++20 (`std::bit_cast`,
`std::span`) and were **deleted from the vendored tree** (Sisco is optional and
needs libdeflate). Don't re-add them without bumping to C++20.

### 4.7 Three parsers, not two
bison/flex generate **`JsonGram` (casa/Json)**, `RecordGram` and `TableGram`
(tables/TaQL). The hand-written `*.cc` wrappers `#include` the generated
`.ycc`/`.lcc`. See the parser loop in `extern/CMakeLists.txt`.

### 4.8 Testing the rebuilt extension: which `_tables.so` actually loads
**This bit hard.** This dev box has *both* an editable install
(`_casacoretables_editable.pth` → `src/`) **and** a leftover regular install
(`site-packages/casacoretables/` + a `dist-info`). That makes
`casacoretables.tables.__path__` a two-element list `[src…, site-packages…]`,
and the `_tables` extension is resolved from **site-packages**, *not* `src/`,
even with `PYTHONPATH=src`. So `cp …so src/…` + `PYTHONPATH=src pytest` silently
keeps running the **old** site-packages `.so`. Symptoms: code changes seem to
have no effect (e.g. GIL still held). To iterate, install the freshly-built `.so`
to the **site-packages** copy (or `pip install .`); confirm with
`python -c "from casacoretables.tables import _tables; print(_tables.__file__)"`.

On **Apple Silicon macOS**, *overwriting* a `.so` that was already loaded once
(its cdhash is cached) makes the next load die with **`Killed: 9` (SIGKILL)** —
a code-signing integrity kill, *not* a crash in your code. Always **`rm` the old
file first then copy** (fresh inode), and re-sign ad-hoc:
`rm -f dst.so && cp new.so dst.so && codesign -f -s - dst.so`.

---

## 5. Bindings & conversion design

- The Python `table`/`tablerow`/`tableiter`/`tableindex` classes subclass the
  C++ `Table`/`TableRow`/`TableIter`/`TableIndex` (pybind11 classes wrapping
  casacore's `TableProxy`/`TableRowProxy`/`TableIterProxy`/`TableIndexProxy`).
  The C++ side exposes the same underscore-prefixed methods python-casacore uses
  (`_getcol`, `_putcol`, …), so the ported `.py` layer works almost unchanged.
- Conversions go through **`casters.h`** type_casters so the raw `TableProxy`
  methods can be bound directly: `casacore::String`↔str, `Vector<String>`↔list
  (a bare str = 1 element), numeric `Vector<T>`↔1-D numpy (a scalar = 1 element),
  `IPosition`↔sequence (reversed), `Record`↔dict, `ValueHolder`↔py object.
  Everything funnels through `ValueHolder` (and `Record::asValueHolder` /
  `defineFromValueHolder`).
- **Exceptions:** `module.cc` maps `casacore::IterError` → `StopIteration`
  (must be caught *before* `AipsError`, since it derives from it) and other
  `AipsError` → `RuntimeError`.

### Zero-copy ("no unnecessary copies of large arrays")
- **Reads** (`getcol`): `array_to_numpy` returns a numpy **view** over casacore
  storage via a `py::capsule` that owns the casacore `Array` (`OWNDATA == False`,
  `base` is a capsule). No copy.
- **Writes** (`putcol`) and **fill** paths (`getcolnp`/`getcellnp`): `numpy_to_array`
  with `share=true` wraps the numpy buffer in place (SHARE) when its dtype
  already matches; otherwise one converting copy. Verify zero-copy with
  `arr.flags['OWNDATA'] is False`.

### Thread safety & the GIL

Every bound method **and constructor** carries
`py::call_guard<py::gil_scoped_release, table_lock>` (the `guarded` alias in
`module.cc`). pybind11 wraps **only the C++ call** with the guard, so it:

1. **releases the GIL** (outer guard) — other Python threads run while one
   thread is inside a long casacore op (read/write/TaQL/open/close); and
2. **takes a single process-global recursive mutex** (`table_mutex()`, inner
   guard) — serializing all casacore access.

Argument loading (Python→C++) and result casting (C++→Python) happen *outside*
the guard: GIL held, mutex not held. That is safe because the conversion code
(`convert.cc`) only touches per-call, thread-local casacore objects.

**Why a single global mutex (not per-table).** casacore's table system is *not*
safe for concurrent access. The global table cache (`PlainTable::tableCache()`)
hands the **same** underlying `PlainTable` to every `TableProxy` opened on a
given path, and the inline `ColumnCache` fast path, storage-manager buffers, and
the TaQL parser file-scope globals (`strp/posTableGram`, `strp/posRecordGram`)
have **no** internal locking. Per-`TableProxy` locking would therefore be
unsound; one global lock is the only correct conservative choice. (casacore 3.x
*does* internally lock the table cache, the DataManager/ColumnDesc registries
and `RecordGram`, and per-op auto-locking serializes same-table ops, so it is
more robust in practice than its docs suggest — but that is incidental, not a
guarantee, and is bypassed by `autonoread`/the inline caches.)

**Safe destruction.** A proxy's destructor closes/flushes its table and mutates
the global cache, which would race with another thread's GIL-released op. So all
four classes use a custom holder `std::unique_ptr<T, casa_deleter<T>>` whose
deleter takes `table_mutex()` before `delete`. `tp_dealloc` runs with the GIL
held; acquiring the mutex there is deadlock-free (no path ever holds the mutex
while waiting for the GIL — the normal path *releases* the GIL before taking the
mutex).

**Lock ordering / deadlock-freedom.** Multi-guard order is guaranteed by
pybind11: `gil_scoped_release` is constructed first (GIL released), `table_lock`
second (mutex taken); unwind is the reverse. No bound C++ path calls back into
Python, so the mutex is never held across a GIL acquire on the normal path. The
mutex is recursive (defensive against future re-entrancy) and intentionally
leaked (`new`, never destroyed) so a late finalizer at interpreter shutdown can
still lock it.

**Caveat — SHARE buffers.** Because the GIL is released while casacore reads the
in-place numpy buffer on the `put*` SHARE path, a caller must not mutate that
numpy array from another thread until the call returns (the usual rule for
GIL-releasing C calls over a shared buffer).

Tests live in `tests/python/test_threadsafety.py`: concurrent reads, cells,
TaQL, table create/open/close, disjoint read/write, a mixed soak (correctness
under concurrency), and three GIL-release tests that assert a sleeping
background thread keeps running during a heavy `getcol`/`putcol`/`taql` (they
**fail** if the GIL is held — verified by temporarily dropping
`gil_scoped_release`).

---

## 6. What is intentionally excluded

- **MeasurementSet helpers** (`default_ms`, `msconcat`, `addImagingColumns`,
  `required_ms_desc`, …) — they live in casacore's `ms` module. `table.py` no
  longer imports them; the 3 MS tests in `test_table.py` are
  `@unittest.skipUnless(HAVE_MS, …)`.
- **`quanta`** — `tablehelper.py` imports it lazily (`quantity = None` if absent)
  and degrades date/quantity pretty-printing to `str(val)`. Because of this,
  the `tablehelper` doctests would fail under `--doctest-modules`; the test
  workflows run plain `pytest tests/python/` (no `--doctest-modules`).
- All other casacore modules, Boost, HDF5, ADIOS2, Dysco, BLAS/LAPACK, FFTW,
  CFITSIO, WCSLIB, MPI, OpenMP, readline. The installed `.so` has **no** external
  shared-lib deps beyond libSystem/libc++.

---

## 7. Tests & expected results

- **C++:** `ctest` → **100/100 pass**. Run via `tests/cpp/run_assay.sh` (honours
  `.run`/`.in`/`.out` like casacore's `assay`; runs each test in `$TMPDIR`).
- **Python:** `pytest tests/python/` → **21 passed, 3 skipped** (the skipped 3
  are MS-module tests). The python tests need the package built+installed
  (`pip install ".[test]"`).

Regenerate the API notebook (kept un-executed/clean in `docs/`): the builder
script that produced it lives outside the repo; the notebook itself is the
source of truth — edit it directly and re-run with
`jupyter nbconvert --to notebook --execute` (with bison on PATH) to validate.

---

## 8. CI workflows (`.github/workflows/`)

- `black.yml` — calls the shared reusable lint workflow
  (`nrao/gh-actions-templates-public/.github/workflows/black-template.yml@main`).
- `python-testing-linux.yml` / `python-testing-macos.yml` — **standalone** (not
  template-based) because the package needs bison/flex (apt on Linux, conda on
  macOS). Run `pytest tests/python/`.
- `cpp-tests.yml` — **standalone**: configure `-DBUILD_CPP_TESTS=ON`, build, `ctest`.
- `python-publish-cpp.yml` — **standalone** `cibuildwheel` (cp311–313,
  manylinux_2_28 x86_64 + macOS arm64) + sdist, publishes on GitHub `release`.
  Installs bison/flex via `CIBW_BEFORE_ALL` (dnf in the container, brew on macOS
  with the keg-only bison prepended to `PATH`); tests each wheel with
  `pytest {project}/tests/python`. Needs a `PYPI_TOKEN` secret + `release` env.

Standalone (rather than calling the shared testing/publish templates) because a
reusable workflow can't inject the bison/flex install step, and the shared macOS
template installs python-casacore + pins Boost, which this package must not do.

---

## 9. Stage C (NOT done, deliberately deferred)

The original task asked to "replace foundation types (Array/Record) with
std/xtensor." We delivered the working package first (Stage A/B) and **deferred
Stage C** because a naive swap **breaks the on-disk table format**: storage
managers copy raw element bytes in casacore's **column-major** order through
`ArrayBase::get/putVStorage`, and `Record`/keywords serialize via **AipsIO**.
Replacing the types means reproducing column-major ordering + the AipsIO byte
stream exactly, or you silently write files CASA can't read. `casacore::String`
already *is* a `std::string` subclass, so that part is moot. If you attempt
Stage C, do it incrementally and keep the (passing) round-trip test suite green.

---

## 10. Quick reference

```bash
conda activate zinc

# build + install
pip install .

# dev rebuild of the extension only
ninja -C build _tables && cp build/bindings/_tables*.so src/casacoretables/tables/

# python tests (needs PYTHONPATH=src if using the in-place .so)
PYTHONPATH=src pytest -v tests/python/

# C++ tests
cmake -S . -B build -G Ninja -DBUILD_CPP_TESTS=ON -DBUILD_PYTHON_MODULE=OFF
cmake --build build --parallel && (cd build && ctest -j4 --output-on-failure)

# verify CASA-coexistence symbol hiding (must print 0)
nm -gU build/bindings/_tables*.so | grep -c casacore                       # macOS
nm -D  build/bindings/_tables*.so | grep -E ' [TSDB] ' | grep -c casacore   # Linux
```

Related agent memory: `project_casacoretables` in
`~/.claude/projects/-Users-jsteeb-Dropbox-viper-dev/memory/`.
