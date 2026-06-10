# casacoretables

A **standalone, minimal-dependency** build of [casacore](https://github.com/casacore/casacore)'s
table system, with a [pybind11](https://github.com/pybind/pybind11) Python
interface that mirrors [python-casacore](https://github.com/casacore/python-casacore)'s
`casacore.tables`.

It bundles **only** casacore's `casa` (base) and `tables` modules — nothing
else — so it builds and installs quickly with very few dependencies.

## What was removed relative to casacore / python-casacore

* All other casacore modules: `scimath`, `measures`, `ms`, `images`,
  `coordinates`, `fits`, `lattices`, `derivedmscal`, ...
* Heavy third-party libraries: HDF5, ADIOS2, Dysco, BLAS/LAPACK, FFTW, CFITSIO,
  WCSLIB, MPI, OpenMP, and **Boost** (python-casacore uses Boost.Python; this
  package uses pybind11 instead).

## Remaining dependencies

* **Runtime:** the C++ standard library, `libdl`, `libm`, `pthreads`, and
  `numpy`. (Nothing else.)
* **Build:** a C++17 compiler, CMake ≥ 3.18, and `bison` ≥ 3 + `flex`
  (build-time only, to generate the TaQL/Json parsers).

The MeasurementSet helpers (`default_ms`, `msconcat`, `addImagingColumns`, ...)
are intentionally absent: they belong to casacore's `ms` module.

## Install

```bash
pip install casacoretables
```

## Use

```python
import numpy as np
from casacoretables.tables import table, maketabdesc, makescacoldesc, makearrcoldesc, taql

desc = maketabdesc([makescacoldesc("ANT", 0),
                    makearrcoldesc("DATA", 0.0, ndim=2, shape=[2, 4])])
t = table("my.tab", desc, nrow=5)
t.putcol("DATA", np.zeros((5, 2, 4)))
data = t.getcol("DATA")            # zero-copy: numpy view over casacore storage
sub  = taql("select from $t where ANT > 2")
t.close()
```

## Documentation

A runnable notebook tours the **entire Python API** — table creation, columns,
cells and slices, keywords, `tablecolumn`/`tablerow`, TaQL queries, iteration,
indexed lookups, copy/rename, and ASCII import/export:
[docs/casacoretables_api_tour.ipynb](docs/casacoretables_api_tour.ipynb).

## Using alongside CASA

`casacoretables` statically links its own copy of casacore and **hides all of
those symbols** (only the module init symbol is exported), so it can be imported
into the same Python process as CASA's `casatools` — which embeds a different,
ABI-incompatible casacore — without symbol clashes or crashes.

## Zero-copy

Large column reads (`getcol`) return a numpy array that *shares* casacore's
storage (no copy) via a capsule that keeps the buffer alive. Writes (`putcol`)
and the in-place `getcolnp`/`getcellnp` fill paths wrap the numpy buffer
directly when its dtype already matches the column, avoiding an intermediate
copy. casacore's column-major arrays are exposed in numpy's row-major
convention by reversing axes — exactly as python-casacore does.

## Tests

* C++ unit tests (vendored from casacore) — configure with
  `-DBUILD_CPP_TESTS=ON` and run with `ctest`.
* Python tests (ported from python-casacore) — `pytest tests/python`.
