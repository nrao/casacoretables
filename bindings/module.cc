//# module.cc: the casacoretables._tables pybind11 extension module.
//#
//# Binds casacore's TableProxy / TableRowProxy / TableIterProxy /
//# TableIndexProxy with the same underscore-prefixed method names that
//# python-casacore's boost.python bindings expose, so the python wrapper layer
//# (table.py, tablerow.py, ...) ports across almost unchanged.
//#
//# All casacore<->python data conversion (numpy arrays, records, value
//# holders, strings) is handled by the type_casters in casters.h.
//#
//# Thread-safety & the GIL (see AGENTS.md "Thread safety"):
//#   casacore's table system is NOT safe for concurrent access. A Table's
//#   ColumnCache and storage-manager buffers have no internal locking, the TaQL
//#   parsers keep file-scope global state, and the global table cache hands the
//#   SAME underlying PlainTable to every TableProxy opened on a given path, so
//#   per-object locking would be unsound. We therefore (a) RELEASE THE GIL and
//#   (b) serialize every casacore call behind a single process-global mutex,
//#   via  py::call_guard<py::gil_scoped_release, table_lock>  on every bound
//#   method and constructor. pybind11 wraps ONLY the C++ call with the guard:
//#   the GIL is released first and the mutex taken second, while argument
//#   loading (Python->C++) and result casting (C++->Python) keep the GIL and do
//#   NOT hold the mutex. The net effect: while one thread is inside a long
//#   casacore operation, other Python threads run freely; casacore itself stays
//#   single-threaded for correctness.

#include "casters.h"

#include <pybind11/pybind11.h>

#include <casacore/tables/Tables/TableProxy.h>
#include <casacore/tables/Tables/TableRowProxy.h>
#include <casacore/tables/Tables/TableIterProxy.h>
#include <casacore/tables/Tables/TableIndexProxy.h>
#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/Containers/IterError.h>

#include <memory>
#include <mutex>

namespace py = pybind11;
using namespace casacore;

namespace {

// Process-global mutex serializing every casacore table operation. See the
// file header for why a single global lock (rather than per-table) is required.
//
// Recursive so that a (future) bound method whose C++ path re-enters another
// bound path on the same thread cannot self-deadlock. Intentionally leaked
// (never destroyed) so a Python object finalized late during interpreter or
// static-destruction shutdown can still lock it safely.
std::recursive_mutex& table_mutex()
{
    static std::recursive_mutex* m = new std::recursive_mutex();
    return *m;
}

// Default-constructible RAII guard used as the INNER pybind11 call_guard,
// paired with py::gil_scoped_release as
//     py::call_guard<py::gil_scoped_release, table_lock>
// pybind11 constructs the guards left-to-right around only the C++ call, so the
// GIL is released first and the mutex acquired second; on unwind the mutex is
// released first and the GIL re-acquired. Argument loading and result casting
// run outside this guard (GIL held, mutex not held) and touch only thread-local
// casacore objects, so they are safe to leave unserialized.
struct table_lock {
    std::lock_guard<std::recursive_mutex> lk{table_mutex()};
};

// Holder deleter. Destroying a proxy runs casacore table teardown
// (flush/close, table-cache and storage-manager mutation) which would race
// with a concurrent GIL-released table op on another thread. tp_dealloc runs
// with the GIL held; we additionally take the table mutex so destruction is
// serialized with every other casacore op. Stateless => trivially
// move-constructible, which pybind11 requires of a holder.
template <typename T>
struct casa_deleter {
    void operator()(T* p) const noexcept {
        std::lock_guard<std::recursive_mutex> lk(table_mutex());
        delete p;
    }
};

template <typename T>
using casa_holder = std::unique_ptr<T, casa_deleter<T>>;

}  // namespace

// Applied to every bound method/constructor: drop the GIL, then serialize the
// C++ call behind the global table mutex.
using guarded = py::call_guard<py::gil_scoped_release, table_lock>;

PYBIND11_MODULE(_tables, m)
{
    m.doc() = "Minimal-dependency casacore tables (pybind11 bindings)";

    // Translate casacore exceptions. IterError signals the end of a table
    // iteration and must become StopIteration (it derives from AipsError, so
    // it has to be caught first); all other AipsErrors map to RuntimeError.
    // (The translator runs in pybind11's dispatcher catch block, after the
    // call_guard has unwound: the GIL is held and the mutex released, so
    // PyErr_SetString is safe.)
    py::register_exception_translator([](std::exception_ptr p) {
        try { if (p) std::rethrow_exception(p); }
        catch (const casacore::IterError& e) {
            PyErr_SetString(PyExc_StopIteration, e.what());
        }
        catch (const casacore::AipsError& e) {
            PyErr_SetString(PyExc_RuntimeError, e.what());
        }
    });

    // -----------------------------------------------------------------------
    // Table  (casacore::TableProxy)
    // -----------------------------------------------------------------------
    py::class_<TableProxy, casa_holder<TableProxy>>(m, "Table")
        .def(py::init<>(), guarded())
        .def(py::init<const TableProxy&>(), guarded())
        // taql: command + list of tables
        .def(py::init<String, std::vector<TableProxy>>(), guarded())
        // open existing: name, lockoptions, option
        .def(py::init<String, Record, int>(), guarded())
        // open multiple as concatenation
        .def(py::init<Vector<String>, Vector<String>, Record, int>(), guarded())
        // concatenate already-open tables
        .def(py::init<std::vector<TableProxy>, Vector<String>, int, int, int>(), guarded())
        // create new: name, lockopts, endian, memtype, nrow, tabdesc, dminfo
        .def(py::init<String, Record, String, String, Int64, Record, Record>(), guarded())
        // read from ascii
        .def(py::init<String, String, String, Bool, IPosition, String, String,
                      Int64, Int64, Vector<String>, Vector<String>>(), guarded())

        .def("_flush", &TableProxy::flush, py::arg("recursive"), guarded())
        .def("_resync", &TableProxy::resync, guarded())
        .def("_close", &TableProxy::close, guarded())
        .def("_toascii", &TableProxy::toAscii,
             py::arg("asciifile"), py::arg("headerfile"), py::arg("columnnames"),
             py::arg("sep"), py::arg("precision"), py::arg("usebrackets"), guarded())
        .def("_rename", &TableProxy::rename, py::arg("newtablename"), guarded())
        .def("_copy", &TableProxy::copy,
             py::arg("newtablename"), py::arg("memorytable"), py::arg("deep"),
             py::arg("valuecopy"), py::arg("endian"), py::arg("dminfo"),
             py::arg("copynorows"), guarded())
        .def("_copyrows", &TableProxy::copyRows,
             py::arg("outtable"), py::arg("startrowin"), py::arg("startrowout"),
             py::arg("nrow"), guarded())
        .def("_selectrows", &TableProxy::selectRows,
             py::arg("rownrs"), py::arg("name"), guarded())
        .def("_iswritable", &TableProxy::isWritable, guarded())
        .def("_endianformat", &TableProxy::endianFormat, guarded())
        .def("_lock", &TableProxy::lock, py::arg("write"), py::arg("nattempts"), guarded())
        .def("_unlock", &TableProxy::unlock, guarded())
        .def("_haslock", &TableProxy::hasLock, py::arg("write"), guarded())
        .def("_lockoptions", &TableProxy::lockOptions, guarded())
        .def("_datachanged", &TableProxy::hasDataChanged, guarded())
        .def("_ismultiused", &TableProxy::isMultiUsed, py::arg("checksubtables"), guarded())
        .def("_name", &TableProxy::tableName, guarded())
        .def("_partnames", &TableProxy::getPartNames, py::arg("recursive"), guarded())
        .def("_info", &TableProxy::tableInfo, guarded())
        .def("_putinfo", &TableProxy::putTableInfo, py::arg("value"), guarded())
        .def("_addreadmeline", &TableProxy::addReadmeLine, py::arg("value"), guarded())
        .def("_setmaxcachesize", &TableProxy::setMaximumCacheSize,
             py::arg("columnname"), py::arg("nbytes"), guarded())
        .def("_rownumbers", &TableProxy::rowNumbers, py::arg("table"), guarded())
        .def("_colnames", &TableProxy::columnNames, guarded())
        .def("_isscalarcol", &TableProxy::isScalarColumn, py::arg("columnname"), guarded())
        .def("_coldatatype", &TableProxy::columnDataType, py::arg("columnname"), guarded())
        .def("_colarraytype", &TableProxy::columnArrayType, py::arg("columnname"), guarded())
        .def("_ncols", &TableProxy::ncolumns, guarded())
        .def("_nrows", &TableProxy::nrows, guarded())
        .def("_addcols", &TableProxy::addColumns,
             py::arg("desc"), py::arg("dminfo"), py::arg("addtoparent"), guarded())
        .def("_renamecol", &TableProxy::renameColumn,
             py::arg("oldname"), py::arg("newname"), guarded())
        .def("_removecols", &TableProxy::removeColumns, py::arg("columnnames"), guarded())
        .def("_addrows", &TableProxy::addRow, py::arg("nrows"), guarded())
        .def("_removerows", &TableProxy::removeRow, py::arg("rownrs"), guarded())
        .def("_iscelldefined", &TableProxy::cellContentsDefined,
             py::arg("columnname"), py::arg("rownr"), guarded())
        .def("_getcell", &TableProxy::getCell,
             py::arg("columnname"), py::arg("rownr"), guarded())
        .def("_getcellvh", &TableProxy::getCellVH,
             py::arg("columnname"), py::arg("rownr"), py::arg("value"), guarded())
        .def("_getcellslice", &TableProxy::getCellSliceIP,
             py::arg("columnname"), py::arg("rownr"), py::arg("blc"),
             py::arg("trc"), py::arg("inc"), guarded())
        .def("_getcellslicevh", &TableProxy::getCellSliceVHIP,
             py::arg("columnname"), py::arg("rownr"), py::arg("blc"),
             py::arg("trc"), py::arg("inc"), py::arg("value"), guarded())
        .def("_getcol", &TableProxy::getColumn,
             py::arg("columnname"), py::arg("startrow"), py::arg("nrow"),
             py::arg("rowincr"), guarded())
        .def("_getcolvh", &TableProxy::getColumnVH,
             py::arg("columnname"), py::arg("startrow"), py::arg("nrow"),
             py::arg("rowincr"), py::arg("value"), guarded())
        .def("_getvarcol", &TableProxy::getVarColumn,
             py::arg("columnname"), py::arg("startrow"), py::arg("nrow"),
             py::arg("rowincr"), guarded())
        .def("_getcolslice", &TableProxy::getColumnSliceIP,
             py::arg("columnname"), py::arg("blc"), py::arg("trc"), py::arg("inc"),
             py::arg("startrow"), py::arg("nrow"), py::arg("rowincr"), guarded())
        .def("_getcolslicevh", &TableProxy::getColumnSliceVHIP,
             py::arg("columnname"), py::arg("blc"), py::arg("trc"), py::arg("inc"),
             py::arg("startrow"), py::arg("nrow"), py::arg("rowincr"),
             py::arg("value"), guarded())
        .def("_putcell", &TableProxy::putCell,
             py::arg("columnname"), py::arg("rownr"), py::arg("value"), guarded())
        .def("_putcellslice", &TableProxy::putCellSliceIP,
             py::arg("columnname"), py::arg("rownr"), py::arg("value"),
             py::arg("blc"), py::arg("trc"), py::arg("inc"), guarded())
        .def("_putcol", &TableProxy::putColumn,
             py::arg("columnname"), py::arg("startrow"), py::arg("nrow"),
             py::arg("rowincr"), py::arg("value"), guarded())
        .def("_putvarcol", &TableProxy::putVarColumn,
             py::arg("columnname"), py::arg("startrow"), py::arg("nrow"),
             py::arg("rowincr"), py::arg("value"), guarded())
        .def("_putcolslice", &TableProxy::putColumnSliceIP,
             py::arg("columnname"), py::arg("value"), py::arg("blc"),
             py::arg("trc"), py::arg("inc"), py::arg("startrow"),
             py::arg("nrow"), py::arg("rowincr"), guarded())
        .def("_getcolshapestring", &TableProxy::getColumnShapeString,
             py::arg("columnname"), py::arg("startrow"), py::arg("nrow"),
             py::arg("rowincr"), py::arg("reverseaxes"), guarded())
        .def("_getkeyword", &TableProxy::getKeyword,
             py::arg("columnname"), py::arg("keyword"), py::arg("keywordindex"), guarded())
        .def("_getkeywords", &TableProxy::getKeywordSet, py::arg("columnname"), guarded())
        .def("_putkeyword", &TableProxy::putKeyword,
             py::arg("columnname"), py::arg("keyword"), py::arg("keywordindex"),
             py::arg("makesubrecord"), py::arg("value"), guarded())
        .def("_putkeywords", &TableProxy::putKeywordSet,
             py::arg("columnname"), py::arg("value"), guarded())
        .def("_removekeyword", &TableProxy::removeKeyword,
             py::arg("columnname"), py::arg("keyword"), py::arg("keywordindex"), guarded())
        .def("_getfieldnames", &TableProxy::getFieldNames,
             py::arg("columnname"), py::arg("keyword"), py::arg("keywordindex"), guarded())
        .def("_getdminfo", &TableProxy::getDataManagerInfo, guarded())
        .def("_getdmprop", &TableProxy::getProperties,
             py::arg("name"), py::arg("bycolumn"), guarded())
        .def("_setdmprop", &TableProxy::setProperties,
             py::arg("name"), py::arg("properties"), py::arg("bycolumn"), guarded())
        .def("_getdesc", &TableProxy::getTableDescription,
             py::arg("actual"), py::arg("_cOrder") = true, guarded())
        .def("_getcoldesc", &TableProxy::getColumnDescription,
             py::arg("columnname"), py::arg("actual"), py::arg("_cOrder") = true, guarded())
        .def("_showstructure", &TableProxy::showStructure,
             py::arg("dataman"), py::arg("column"), py::arg("subtable"),
             py::arg("sort"), guarded())
        .def("_getasciiformat", &TableProxy::getAsciiFormat, guarded())
        .def("_getcalcresult", &TableProxy::getCalcResult, guarded());

    // -----------------------------------------------------------------------
    // TableRow  (casacore::TableRowProxy)
    // -----------------------------------------------------------------------
    py::class_<TableRowProxy, casa_holder<TableRowProxy>>(m, "TableRow")
        .def(py::init<TableProxy, Vector<String>, Bool>(), guarded())
        .def("_iswritable", &TableRowProxy::isWritable, guarded())
        .def("_get", &TableRowProxy::get, py::arg("rownr"), guarded())
        .def("_put", &TableRowProxy::put,
             py::arg("rownr"), py::arg("value"), py::arg("matchingfields"), guarded());

    // -----------------------------------------------------------------------
    // TableIter  (casacore::TableIterProxy)
    // -----------------------------------------------------------------------
    py::class_<TableIterProxy, casa_holder<TableIterProxy>>(m, "TableIter")
        .def(py::init<TableProxy, Vector<String>, String, String>(), guarded())
        .def("_reset", &TableIterProxy::reset, guarded())
        .def("_next", &TableIterProxy::next, guarded());

    // -----------------------------------------------------------------------
    // TableIndex  (casacore::TableIndexProxy)
    // -----------------------------------------------------------------------
    py::class_<TableIndexProxy, casa_holder<TableIndexProxy>>(m, "TableIndex")
        .def(py::init<TableProxy, Vector<String>, Bool>(), guarded())
        .def("_isunique", &TableIndexProxy::isUnique, guarded())
        .def("_colnames", &TableIndexProxy::columnNames, guarded())
        .def("_setchanged", &TableIndexProxy::setChanged, guarded())
        .def("_rownr", &TableIndexProxy::getRowNumber, guarded())
        .def("_rownrs", &TableIndexProxy::getRowNumbers, guarded())
        .def("_rownrsrange", &TableIndexProxy::getRowNumbersRange, guarded());
}
