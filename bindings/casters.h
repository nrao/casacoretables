//# casters.h: pybind11 type_caster specializations for the casacore types that
//# cross the Python boundary in TableProxy's API. With these in scope the raw
//# TableProxy methods can be bound directly (mirroring how python-casacore
//# relies on registered boost.python converters).

#pragma once

#include "convert.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>          // std::vector<TableProxy> <-> list

#include <casacore/casa/BasicSL/String.h>
#include <casacore/casa/Arrays/Vector.h>
#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/Containers/ValueHolder.h>

#include <complex>
#include <type_traits>

namespace pybind11 { namespace detail {

template <typename T> struct cct_is_complex : std::false_type {};
template <typename T> struct cct_is_complex<std::complex<T>> : std::true_type {};

// ---- casacore::String <-> str ---------------------------------------------
template <> struct type_caster<casacore::String> {
    PYBIND11_TYPE_CASTER(casacore::String, const_name("str"));
    bool load(handle src, bool) {
        if (src.is_none()) return false;
        PyObject* s = PyObject_Str(src.ptr());
        if (!s) { PyErr_Clear(); return false; }
        object held = reinterpret_steal<object>(s);
        try { value = casacore::String(held.cast<std::string>()); }
        catch (...) { return false; }
        return true;
    }
    static handle cast(const casacore::String& s, return_value_policy, handle) {
        return py::str(std::string(s)).release();
    }
};

// ---- casacore::ValueHolder <-> python object ------------------------------
template <> struct type_caster<casacore::ValueHolder> {
    PYBIND11_TYPE_CASTER(casacore::ValueHolder, const_name("object"));
    bool load(handle src, bool) {
        value = cct::py_to_valueholder(src);
        return true;
    }
    static handle cast(const casacore::ValueHolder& vh, return_value_policy, handle) {
        return cct::valueholder_to_py(vh).release();
    }
};

// ---- casacore::Record <-> dict --------------------------------------------
template <> struct type_caster<casacore::Record> {
    PYBIND11_TYPE_CASTER(casacore::Record, const_name("dict"));
    bool load(handle src, bool) {
        if (!isinstance<dict>(src)) return false;
        value = cct::py_to_record(src);
        return true;
    }
    static handle cast(const casacore::Record& r, return_value_policy, handle) {
        return cct::record_to_py(r).release();
    }
};

// ---- casacore::IPosition <-> sequence[int] --------------------------------
template <> struct type_caster<casacore::IPosition> {
    PYBIND11_TYPE_CASTER(casacore::IPosition, const_name("Sequence[int]"));
    bool load(handle src, bool) {
        if (!isinstance<sequence>(src) || isinstance<str>(src)) return false;
        value = cct::py_to_iposition(src);
        return true;
    }
    static handle cast(const casacore::IPosition& ip, return_value_policy, handle) {
        return cct::iposition_to_py(ip).release();
    }
};

// ---- casacore::Vector<String> <-> list[str] (a bare str is one element) ---
template <> struct type_caster<casacore::Vector<casacore::String>> {
    PYBIND11_TYPE_CASTER(casacore::Vector<casacore::String>, const_name("list[str]"));
    bool load(handle src, bool) {
        if (isinstance<str>(src)) {                 // single column name
            value = casacore::Vector<casacore::String>(
                1, casacore::String(src.cast<std::string>()));
            return true;
        }
        if (!isinstance<sequence>(src)) return false;
        value = cct::py_to_string_vector(src);
        return true;
    }
    static handle cast(const casacore::Vector<casacore::String>& v, return_value_policy, handle) {
        return cct::string_vector_to_py(v).release();
    }
};

// ---- casacore::Vector<T> (numeric) <-> 1-D numpy (scalar -> 1 element) -----
template <typename T>
struct type_caster<casacore::Vector<T>,
                   enable_if_t<std::is_arithmetic<T>::value || cct_is_complex<T>::value>> {
    PYBIND11_TYPE_CASTER(casacore::Vector<T>, const_name("numpy.ndarray"));
    bool load(handle src, bool) {
        if (isinstance<str>(src)) return false;
        if (isinstance<array>(src) ||
            (isinstance<sequence>(src) && !isinstance<bytes>(src))) {
            casacore::Array<T> arr =
                cct::numpy_to_array<T>(reinterpret_borrow<object>(src), false);
            value = casacore::Vector<T>(arr.reform(casacore::IPosition(1, arr.size())));
            return true;
        }
        // accept a python/numpy scalar as a 1-element vector (e.g. a single rownr)
        try {
            value = casacore::Vector<T>(1, src.cast<T>());
            return true;
        } catch (...) {
            return false;
        }
    }
    static handle cast(const casacore::Vector<T>& v, return_value_policy, handle) {
        return cct::array_to_numpy<T>(v).release();
    }
};

}}  // namespace pybind11::detail
