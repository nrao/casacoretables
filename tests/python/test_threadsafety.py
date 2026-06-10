"""Thread-safety and GIL-release tests for casacoretables.

The pybind11 bindings release the GIL around every casacore call (so other
Python threads can run during long table I/O) and serialize all casacore access
behind a single process-global mutex (because casacore's table system is not
safe for concurrent access to a table object, and the TaQL parser globals and
parts of the open/close path are not internally synchronized).

These tests verify both properties:

  * correctness under concurrency -- many threads hammering reads, writes,
    TaQL queries and table create/open/close at once must never crash, corrupt
    data, or return wrong results;
  * the GIL is genuinely released during a casacore call -- a background Python
    thread keeps making progress while the main thread is inside a heavy
    casacore operation.

All tables are created with absolute paths under a private temp directory
(``tempfile.mkdtemp`` -> ``$TMPDIR``), never inside the Dropbox-synced source
tree, which interferes with casacore's table locking.
"""

import os
import shutil
import tempfile
import threading
import time
import unittest

import numpy as np

from casacoretables.tables import (
    table,
    maketabdesc,
    makescacoldesc,
    makearrcoldesc,
    taql,
    tabledelete,
)


def _make_table(path, nrow, ncell, fill=True):
    """Create a table with a scalar int column ``X`` and a square 2-D float
    array column ``DATA`` of cell shape (ncell, ncell). Row i gets X[i]=i and
    DATA[i] filled with float(i)."""
    desc = maketabdesc(
        [
            makescacoldesc("X", 0),
            makearrcoldesc("DATA", 0.0, ndim=2, shape=[ncell, ncell]),
        ]
    )
    t = table(path, desc, nrow=nrow, ack=False)
    if fill:
        x = np.arange(nrow, dtype=np.int32)
        # data[i] == full((ncell, ncell), float(i)); vectorised so large tables
        # (used by the GIL-release timing tests) build quickly.
        data = np.arange(nrow, dtype=np.float64)[:, None, None] * np.ones(
            (1, ncell, ncell), dtype=np.float64
        )
        t.putcol("X", x)
        t.putcol("DATA", data)
        t.flush()
    return t


class ThreadSafetyBase(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="cct_ts_")

    def tearDown(self):
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def path(self, name):
        return os.path.join(self.tmpdir, name)

    def run_threads(self, target, nthreads, *args):
        """Run ``target(idx, barrier, errors, *args)`` on ``nthreads`` threads
        started as simultaneously as possible (a Barrier maximises overlap),
        and return the collected list of (thread, exception) errors."""
        errors = []
        barrier = threading.Barrier(nthreads)

        def wrapper(idx):
            try:
                target(idx, barrier, errors, *args)
            except Exception as e:  # noqa: BLE001 - record, don't crash the run
                errors.append((idx, repr(e)))

        threads = [threading.Thread(target=wrapper, args=(i,)) for i in range(nthreads)]
        for th in threads:
            th.start()
        for th in threads:
            th.join()
        return errors


class TestConcurrentReads(ThreadSafetyBase):
    def test_shared_table_concurrent_getcol(self):
        """Many threads reading the SAME open table concurrently must all get
        correct data and never crash."""
        nrow, ncell = 60, 8
        t = _make_table(self.path("reads.tab"), nrow, ncell)
        expected = np.empty((nrow, ncell, ncell), dtype=np.float64)
        for i in range(nrow):
            expected[i] = float(i)

        def worker(idx, barrier, errors):
            barrier.wait()
            for _ in range(100):
                got = t.getcol("DATA")
                if not np.array_equal(got, expected):
                    raise AssertionError("getcol returned corrupt data")
                gx = t.getcol("X")
                if not np.array_equal(gx, np.arange(nrow, dtype=np.int32)):
                    raise AssertionError("getcol(X) returned corrupt data")

        errors = self.run_threads(worker, 8)
        t.close()
        self.assertEqual(errors, [], "concurrent reads produced errors")

    def test_concurrent_cell_and_slice_reads(self):
        """Mixed cell / column / slice reads on a shared table."""
        nrow, ncell = 40, 6
        t = _make_table(self.path("cells.tab"), nrow, ncell)

        def worker(idx, barrier, errors):
            barrier.wait()
            for r in range(nrow):
                cell = t.getcell("DATA", r)
                if not np.array_equal(cell, np.full((ncell, ncell), float(r))):
                    raise AssertionError("getcell corrupt at row %d" % r)
                if int(t.getcell("X", r)) != r:
                    raise AssertionError("getcell(X) corrupt at row %d" % r)

        errors = self.run_threads(worker, 8)
        t.close()
        self.assertEqual(errors, [])


class TestConcurrentTaQL(ThreadSafetyBase):
    def test_concurrent_taql_queries(self):
        """TaQL parsing uses process-global parser state (bison/flex globals,
        RecordGram/TableGram). Many concurrent parses must serialize correctly
        and return the right row counts -- without serialization this corrupts
        the parser and returns wrong results or crashes."""
        nrow, ncell = 100, 4
        t = _make_table(self.path("taql.tab"), nrow, ncell)

        def worker(idx, barrier, errors):
            barrier.wait()
            for k in range(0, nrow, 3):
                # pass the table explicitly: $t cannot be resolved from a
                # closure free-variable (it is only named inside a string).
                sub = taql("select from $t where X >= %d" % k, locals={"t": t})
                n = sub.nrows()
                if n != nrow - k:
                    raise AssertionError(
                        "taql wrong count: got %d expected %d (k=%d)" % (n, nrow - k, k)
                    )
                sub.close()

        errors = self.run_threads(worker, 8)
        t.close()
        self.assertEqual(errors, [], "concurrent TaQL produced errors")


class TestConcurrentTableLifecycle(ThreadSafetyBase):
    def test_concurrent_create_write_read_close(self):
        """Each thread creates, fills, reads back, verifies, closes and deletes
        its OWN table. Stresses the global table cache and the open/close path
        concurrently."""
        nrow, ncell = 30, 5

        def worker(idx, barrier, errors):
            path = self.path("life_%d.tab" % idx)
            barrier.wait()
            for rep in range(10):
                t = _make_table(path, nrow, ncell)
                got = t.getcol("DATA")
                exp = np.empty((nrow, ncell, ncell), dtype=np.float64)
                for i in range(nrow):
                    exp[i] = float(i)
                if not np.array_equal(got, exp):
                    raise AssertionError("lifecycle data corrupt")
                t.close()
                tabledelete(path)

        errors = self.run_threads(worker, 8)
        self.assertEqual(errors, [])


class TestConcurrentReadWrite(ThreadSafetyBase):
    def test_writers_and_readers_disjoint_rows(self):
        """Writers each own a disjoint block of rows and write a known marker;
        readers read other (already-written) rows. After join, the whole column
        must hold every writer's markers -- verifying writes are not lost or
        torn under concurrency."""
        nthreads = 8
        rows_per = 20
        nrow = nthreads * rows_per
        ncell = 4
        t = _make_table(self.path("rw.tab"), nrow, ncell, fill=False)
        # seed with -1 so we can detect unwritten cells
        t.putcol("DATA", np.full((nrow, ncell, ncell), -1.0))
        t.flush()

        def worker(idx, barrier, errors):
            start = idx * rows_per
            block = np.empty((rows_per, ncell, ncell), dtype=np.float64)
            for j in range(rows_per):
                block[j] = float(start + j)
            barrier.wait()
            for _ in range(20):
                t.putcol("DATA", block, startrow=start, nrow=rows_per)
                back = t.getcol("DATA", startrow=start, nrow=rows_per)
                if not np.array_equal(back, block):
                    raise AssertionError("own block read-back mismatch")

        errors = self.run_threads(worker, nthreads)
        # final whole-column check: every row must equal float(row index)
        whole = t.getcol("DATA")
        t.close()
        self.assertEqual(errors, [])
        for i in range(nrow):
            self.assertTrue(
                np.array_equal(whole[i], np.full((ncell, ncell), float(i))),
                "row %d not consistent after concurrent writes" % i,
            )


class TestMixedSoak(ThreadSafetyBase):
    def test_mixed_workload_soak(self):
        """A short soak mixing getcol / getcell / taql / nrows from many
        threads on a shared table. Pure fuzz: must not crash or raise."""
        nrow, ncell = 80, 5
        t = _make_table(self.path("soak.tab"), nrow, ncell)
        deadline = time.time() + 1.5

        def worker(idx, barrier, errors):
            barrier.wait()
            ops = 0
            while time.time() < deadline:
                m = (idx + ops) % 4
                if m == 0:
                    t.getcol("DATA")
                elif m == 1:
                    t.getcell("DATA", ops % nrow)
                elif m == 2:
                    sub = taql(
                        "select from $t where X < %d" % (ops % nrow + 1),
                        locals={"t": t},
                    )
                    sub.nrows()
                    sub.close()
                else:
                    t.nrows()
                ops += 1

        errors = self.run_threads(worker, 8)
        t.close()
        self.assertEqual(errors, [], "soak workload produced errors")


class TestGILReleased(ThreadSafetyBase):
    """Verify the GIL is genuinely released during a casacore call.

    A background "sleeping ticker" thread loops doing ``time.sleep(0.001)`` then
    incrementing a counter. Because it spends almost all its time asleep it uses
    essentially no CPU, so -- unlike a busy-loop counter -- its rate is NOT
    reduced by the main thread's CPU-bound C++ work; it can advance only when it
    is able to RE-ACQUIRE the GIL after each 1 ms sleep.

    We compare its rate while the main thread runs a heavy casacore call against
    its rate while the main thread is in ``time.sleep`` (which always releases
    the GIL):

      * GIL released during the C++ work -> the ticker re-acquires the GIL after
        every sleep and runs at close to its free rate;
      * GIL held for the whole call       -> the ticker blocks on the GIL after
        its first sleep and barely advances.

    ``rate_work / rate_free`` is a machine-independent ratio; >0.3 means the GIL
    was available (released) for the bulk of the call. (Validated to read ~1.0
    for a GIL-released C call and ~0.02 for a GIL-holding one.)
    """

    def _sleeping_tick_rate(self, action):
        stop = threading.Event()
        count = [0]

        def ticker():
            while not stop.is_set():
                time.sleep(0.001)
                count[0] += 1

        th = threading.Thread(target=ticker)
        th.start()
        try:
            time.sleep(0.1)  # let the ticker reach steady state
            c0 = count[0]
            t0 = time.perf_counter()
            action()
            dt = time.perf_counter() - t0
            ticks = count[0] - c0
        finally:
            stop.set()
            th.join()
        return (ticks / dt if dt else 0.0), dt

    def _assert_gil_released(self, heavy_op, label):
        rate_work, dt_work = self._sleeping_tick_rate(heavy_op)
        rate_free, _ = self._sleeping_tick_rate(lambda: time.sleep(max(dt_work, 0.03)))
        if dt_work < 0.02:
            self.skipTest(
                "%s op too fast (%.4fs) to measure GIL release" % (label, dt_work)
            )
        ratio = rate_work / rate_free if rate_free else 0.0
        self.assertGreater(
            ratio,
            0.3,
            "a sleeping background thread ran at only %.0f%% of its GIL-free "
            "rate during a %.3fs %s call -- the GIL appears NOT to be released "
            "during %s" % (100 * ratio, dt_work, label, label),
        )

    def test_gil_released_during_getcol(self):
        # ~160 MB column so one getcol's C++ assembly is comfortably long.
        nrow, ncell = 80, 512
        t = _make_table(self.path("gil.tab"), nrow, ncell)
        t.getcol("DATA")  # warm the storage-manager cache
        try:
            self._assert_gil_released(lambda: t.getcol("DATA"), "getcol")
        finally:
            t.close()

    def test_gil_released_during_putcol(self):
        nrow, ncell = 80, 512
        t = _make_table(self.path("gilput.tab"), nrow, ncell, fill=False)
        block = np.ones((nrow, ncell, ncell), dtype=np.float64)
        t.putcol("DATA", block)  # warm up
        try:
            self._assert_gil_released(lambda: t.putcol("DATA", block), "putcol")
        finally:
            t.close()

    def test_gil_released_during_taql(self):
        # A TaQL sort over a large table is one heavy C++ call.
        nrow, ncell = 1000000, 2
        t = _make_table(self.path("giltaql.tab"), nrow, ncell)
        taql("select from $t orderby X desc", locals={"t": t}).close()  # warm
        try:
            self._assert_gil_released(
                lambda: taql("select from $t orderby X desc", locals={"t": t}).close(),
                "taql",
            )
        finally:
            t.close()


if __name__ == "__main__":
    unittest.main()
