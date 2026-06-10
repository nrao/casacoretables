//# convert.h: numpy <-> casacore conversion helpers for the pybind11 bindings.
//#
//# These functions mirror the semantics of casacore's own boost.python
//# Converters (PycArray / PycValueHolder / PycRecord) used by python-casacore,
//# but are written for pybind11 and made zero-copy on the read path.
//#
//# Axis order: casacore Arrays are column-major (first axis varies fastest),
//# numpy arrays are row-major. A C-contiguous numpy array whose shape is the
//# reverse of the casacore shape, laid over the same linear buffer, is exactly
//# the array python-casacore produces - so we reverse the shape and share the
//# buffer instead of copying.

#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/complex.h>

#include <casacore/casa/Arrays/Array.h>
#include <casacore/casa/Arrays/Vector.h>
#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/casa/Containers/ValueHolder.h>
#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/BasicSL/String.h>

#include <vector>

namespace py = pybind11;

namespace cct {

// ---- numpy <-> casacore::Array<T> (numeric T) ------------------------------

// casacore Array<T> -> numpy, ZERO-COPY: the returned ndarray shares the
// casacore storage and keeps it alive through a capsule. Falls back to one
// contiguous copy only when the source array is not contiguously stored.
template <typename T>
py::array array_to_numpy(const casacore::Array<T>& arr)
{
    casacore::Array<T>* holder =
        arr.contiguousStorage() ? new casacore::Array<T>(arr)            // share (ref-counted)
                                : new casacore::Array<T>(arr.copy());     // contiguous copy
    const casacore::IPosition& s = holder->shape();
    std::vector<py::ssize_t> shape(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        shape[i] = s[s.size() - 1 - i];        // reverse: column-major -> C-major
    }
    py::capsule owner(holder, [](void* p) {
        delete static_cast<casacore::Array<T>*>(p);
    });
    // shape + data ptr + base owner => C-contiguous view, no copy.
    return py::array_t<T>(shape, holder->data(), owner);
}

namespace detail {
template <typename ArrLike>
casacore::IPosition reversed_shape(const ArrLike& a)
{
    casacore::IPosition shape(std::max<py::ssize_t>(a.ndim(), 1), 1);
    for (py::ssize_t i = 0; i < a.ndim(); ++i) {
        shape[i] = static_cast<ssize_t>(a.shape(a.ndim() - 1 - i));
    }
    return shape;
}
}  // namespace detail

// numpy -> casacore::Array<T> (numeric T).
//   share == true  : if the source is already dtype-T and C-contiguous, wrap
//                    its buffer in place (no copy). casacore then reads/writes
//                    that buffer directly - valid because the source numpy
//                    object is alive for the duration of the binding call.
//                    Otherwise falls back to a single converting copy.
//                    NOTE: the binding releases the GIL while casacore touches
//                    this shared buffer, so a caller must not mutate the numpy
//                    array it passed to put*/getcolnp from another thread until
//                    the call returns (the usual rule for GIL-releasing C
//                    calls that share a buffer).
//   share == false : always copy into a fresh casacore-owned Array.
template <typename T>
casacore::Array<T> numpy_to_array(const py::array& a, bool share)
{
    if (share && a.dtype().is(py::dtype::of<T>()) &&
        (a.flags() & py::array::c_style)) {
        return casacore::Array<T>(detail::reversed_shape(a),
                                  const_cast<T*>(static_cast<const T*>(a.data())),
                                  casacore::SHARE);
    }
    auto arr = py::array_t<T, py::array::c_style | py::array::forcecast>::ensure(a);
    if (!arr) {
        throw py::type_error("could not convert array to the required numeric type");
    }
    return casacore::Array<T>(detail::reversed_shape(arr), arr.data());  // copy
}

// ---- String arrays --------------------------------------------------------
// Matches python-casacore: a 1-D Array<String> becomes a python list[str];
// an N-D one becomes {"shape": [...native casacore shape...], "array": [flat
// list in casacore order]}. (No axis reversal, unlike numeric arrays.)
py::object string_array_to_py(const casacore::Array<casacore::String>& arr);
casacore::Array<casacore::String> numpy_to_string_array(const py::array& a);
casacore::ValueHolder string_array_from_dict(const py::dict& d);

// ---- ValueHolder <-> python -----------------------------------------------
py::object        valueholder_to_py(const casacore::ValueHolder& vh);
casacore::ValueHolder py_to_valueholder(const py::handle& obj);

// ---- Record <-> dict -------------------------------------------------------
py::dict          record_to_py(const casacore::Record& rec);
casacore::Record  py_to_record(const py::handle& obj);

// ---- small helpers ---------------------------------------------------------
py::list                       string_vector_to_py(const casacore::Vector<casacore::String>& v);
casacore::Vector<casacore::String> py_to_string_vector(const py::handle& obj);
py::tuple                      iposition_to_py(const casacore::IPosition& ip);
casacore::IPosition            py_to_iposition(const py::handle& obj);

}  // namespace cct
