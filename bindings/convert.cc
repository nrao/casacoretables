//# convert.cc: numpy <-> casacore conversion implementations.

#include "convert.h"

#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/Utilities/DataType.h>

#include <complex>

namespace py = pybind11;
using namespace casacore;

namespace cct {

// ---------------------------------------------------------------------------
// String arrays
// ---------------------------------------------------------------------------
py::object string_array_to_py(const Array<String>& arr)
{
    // Flat list of python str in casacore (column-major) linear order.
    Bool deleteIt;
    const String* src = arr.getStorage(deleteIt);
    size_t n = arr.size();
    py::list flat;
    for (size_t i = 0; i < n; ++i) flat.append(py::str(std::string(src[i])));
    arr.freeStorage(src, deleteIt);

    if (arr.ndim() <= 1) {
        return std::move(flat);
    }
    py::dict d;
    d["shape"] = iposition_to_py(arr.shape());     // reversed (numpy) order
    d["array"] = flat;                             // flat list in casacore order
    return std::move(d);
}

// {"shape": [...], "array": [flat list]} -> N-D Array<String> (native order).
casacore::ValueHolder string_array_from_dict(const py::dict& d)
{
    IPosition shp = py_to_iposition(d["shape"]);
    py::sequence arr = py::reinterpret_borrow<py::sequence>(py::object(d["array"]));
    size_t n = py::len(arr);
    Vector<String> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = py::str(arr[i]).cast<std::string>();
    if (static_cast<ssize_t>(v.size()) != shp.product()) {
        throw AipsError("casacoretables: array size mismatches the shape");
    }
    return ValueHolder(Array<String>(v).reform(shp));
}

casacore::Array<String> numpy_to_string_array(const py::array& a)
{
    py::array arr = py::array::ensure(a, py::array::c_style);
    IPosition shape(std::max<py::ssize_t>(arr.ndim(), 1), 1);
    for (py::ssize_t i = 0; i < arr.ndim(); ++i) {
        shape[i] = static_cast<ssize_t>(arr.shape(arr.ndim() - 1 - i));
    }
    Array<String> out(shape);
    String* dst = out.data();
    size_t n = out.size();
    auto flat = arr.attr("ravel")();           // C-order flatten
    py::sequence seq = py::reinterpret_borrow<py::sequence>(flat);
    for (size_t i = 0; i < n; ++i) {
        dst[i] = py::str(seq[i]).cast<std::string>();
    }
    return out;
}

// ---------------------------------------------------------------------------
// numpy ndarray -> ValueHolder (dispatch on dtype)
// ---------------------------------------------------------------------------
static ValueHolder ndarray_to_vh(const py::array& a)
{
    py::dtype dt = a.dtype();
    char kind = dt.kind();
    int   sz   = dt.itemsize();
    // share == true: zero-copy where dtype already matches (large put/get-into
    // paths). casacore reads/writes the numpy buffer directly during the call.
    switch (kind) {
    case 'b':                                   // bool
        return ValueHolder(numpy_to_array<Bool>(a, true));
    case 'i':
        if (sz <= 2) return ValueHolder(numpy_to_array<Short>(a, true));
        if (sz == 4) return ValueHolder(numpy_to_array<Int>(a, true));
        return ValueHolder(numpy_to_array<Int64>(a, true));
    case 'u':
        if (sz == 1) return ValueHolder(numpy_to_array<uChar>(a, true));
        if (sz == 2) return ValueHolder(numpy_to_array<uShort>(a, true));
        if (sz == 4) return ValueHolder(numpy_to_array<uInt>(a, true));
        return ValueHolder(numpy_to_array<Int64>(a, true));
    case 'f':
        if (sz <= 4) return ValueHolder(numpy_to_array<Float>(a, true));
        return ValueHolder(numpy_to_array<Double>(a, true));
    case 'c':
        if (sz <= 8) return ValueHolder(numpy_to_array<Complex>(a, true));
        return ValueHolder(numpy_to_array<DComplex>(a, true));
    case 'S':
    case 'U':
    case 'O':
        return ValueHolder(numpy_to_string_array(a));
    default:
        throw AipsError("casacoretables: unsupported numpy dtype kind '" +
                        std::string(1, kind) + "'");
    }
}

// numpy scalar (np.int32, np.float64, ...) -> scalar ValueHolder
static ValueHolder npscalar_to_vh(const py::handle& obj)
{
    py::dtype dt = obj.attr("dtype").cast<py::dtype>();
    char kind = dt.kind();
    int   sz  = dt.itemsize();
    switch (kind) {
    case 'b': return ValueHolder(obj.cast<bool>());
    case 'i':
        if (sz == 4) return ValueHolder(obj.cast<Int>());
        return ValueHolder(obj.cast<Int64>());
    case 'u':
        if (sz <= 4) return ValueHolder(static_cast<Int64>(obj.cast<uInt>()));
        return ValueHolder(obj.cast<Int64>());
    case 'f': return ValueHolder(obj.cast<Double>());
    case 'c': return ValueHolder(obj.cast<std::complex<double>>());
    default:  return ValueHolder(obj.cast<std::string>());
    }
}

// homogeneous python sequence -> casacore Vector ValueHolder
static ValueHolder sequence_to_vh(const py::sequence& seq)
{
    size_t n = py::len(seq);
    if (n == 0) {
        return ValueHolder(1, True);            // empty 1-dim array
    }
    // Determine the "highest" element type, numeric < string/bool exclusivity
    // mirrors casacore's PycValueHolder::checkDataType.
    enum Kind { K_NONE, K_BOOL, K_INT, K_DOUBLE, K_COMPLEX, K_STRING } k = K_NONE;
    auto promote = [&](Kind nk) {
        if (k == K_NONE) { k = nk; return; }
        if (k == nk) return;
        if (k == K_BOOL || k == K_STRING || nk == K_BOOL || nk == K_STRING) {
            throw AipsError("casacoretables: incompatible types in sequence");
        }
        k = std::max(k, nk);                    // INT < DOUBLE < COMPLEX
    };
    for (auto item : seq) {
        if (py::isinstance<py::bool_>(item))         promote(K_BOOL);
        else if (py::isinstance<py::int_>(item))     promote(K_INT);
        else if (py::isinstance<py::float_>(item))   promote(K_DOUBLE);
        else if (PyComplex_Check(item.ptr()))        promote(K_COMPLEX);
        else if (py::isinstance<py::str>(item))      promote(K_STRING);
        else throw AipsError("casacoretables: unsupported element in sequence");
    }
    switch (k) {
    case K_BOOL: {
        Vector<Bool> v(n); for (size_t i=0;i<n;++i) v[i]=seq[i].cast<bool>();
        return ValueHolder(v); }
    case K_INT: {
        Vector<Int64> v(n); for (size_t i=0;i<n;++i) v[i]=seq[i].cast<Int64>();
        return ValueHolder(v); }
    case K_DOUBLE: {
        Vector<Double> v(n); for (size_t i=0;i<n;++i) v[i]=seq[i].cast<double>();
        return ValueHolder(v); }
    case K_COMPLEX: {
        Vector<DComplex> v(n); for (size_t i=0;i<n;++i) v[i]=seq[i].cast<std::complex<double>>();
        return ValueHolder(v); }
    case K_STRING: {
        Vector<String> v(n); for (size_t i=0;i<n;++i) v[i]=seq[i].cast<std::string>();
        return ValueHolder(v); }
    default:
        return ValueHolder(1, True);
    }
}

// ---------------------------------------------------------------------------
// ValueHolder -> python
// ---------------------------------------------------------------------------
py::object valueholder_to_py(const ValueHolder& vh)
{
    if (vh.isNull()) return py::none();
    switch (vh.dataType()) {
    case TpBool:        return py::bool_(vh.asBool());
    case TpChar:
    case TpShort:
    case TpInt:         return py::int_(vh.asInt());
    case TpUChar:
    case TpUShort:
    case TpUInt:        return py::int_(vh.asuInt());
    case TpInt64:       return py::int_(vh.asInt64());
    case TpFloat:
    case TpDouble:      return py::float_(vh.asDouble());
    case TpComplex:
    case TpDComplex:    return py::cast(vh.asDComplex());
    case TpString:      return py::str(std::string(vh.asString()));
    case TpArrayBool:    return array_to_numpy<Bool>(vh.asArrayBool());
    case TpArrayUChar:   return array_to_numpy<uChar>(vh.asArrayuChar());
    case TpArrayShort:   return array_to_numpy<Short>(vh.asArrayShort());
    case TpArrayInt:     return array_to_numpy<Int>(vh.asArrayInt());
    case TpArrayUInt:    return array_to_numpy<uInt>(vh.asArrayuInt());
    case TpArrayInt64:   return array_to_numpy<Int64>(vh.asArrayInt64());
    case TpArrayFloat:   return array_to_numpy<Float>(vh.asArrayFloat());
    case TpArrayDouble:  return array_to_numpy<Double>(vh.asArrayDouble());
    case TpArrayComplex: return array_to_numpy<Complex>(vh.asArrayComplex());
    case TpArrayDComplex:return array_to_numpy<DComplex>(vh.asArrayDComplex());
    case TpArrayString:  return string_array_to_py(vh.asArrayString());
    case TpRecord:       return record_to_py(vh.asRecord());
    default:
        throw AipsError("casacoretables: unknown casa data type " +
                        String::toString(vh.dataType()));
    }
}

// ---------------------------------------------------------------------------
// python -> ValueHolder  (mirrors PycValueHolder::makeValueHolder)
// ---------------------------------------------------------------------------
casacore::ValueHolder py_to_valueholder(const py::handle& obj)
{
    if (obj.is_none()) return ValueHolder(0, True);     // empty 0-dim

    // numpy scalar (do before python int/float so np.int64 stays Int64).
    static const py::object np_generic =
        py::module_::import("numpy").attr("generic");
    if (py::isinstance(obj, np_generic)) {
        return npscalar_to_vh(obj);
    }
    if (py::isinstance<py::array>(obj)) {
        return ndarray_to_vh(py::reinterpret_borrow<py::array>(obj));
    }
    if (py::isinstance<py::bool_>(obj)) {
        return ValueHolder(obj.cast<bool>());
    }
    if (py::isinstance<py::int_>(obj)) {
        Int64 v = obj.cast<Int64>();
        if (static_cast<Int>(v) == v) return ValueHolder(static_cast<Int>(v));
        return ValueHolder(v);
    }
    if (py::isinstance<py::float_>(obj)) {
        return ValueHolder(obj.cast<double>());
    }
    if (PyComplex_Check(obj.ptr())) {
        return ValueHolder(obj.cast<std::complex<double>>());
    }
    if (py::isinstance<py::str>(obj)) {
        return ValueHolder(String(obj.cast<std::string>()));
    }
    if (py::isinstance<py::dict>(obj)) {
        py::dict d = py::reinterpret_borrow<py::dict>(obj);
        if (d.contains("shape") && d.contains("array")) {
            // {shape, array} dict: python-casacore's N-D string-array form
            // (array is a flat list). A numeric N-D array is passed as a real
            // ndarray, so route those through the ndarray path.
            if (py::isinstance<py::array>(d["array"])) {
                return ndarray_to_vh(py::reinterpret_borrow<py::array>(d["array"]));
            }
            return string_array_from_dict(d);
        }
        return ValueHolder(py_to_record(obj));
    }
    if (py::isinstance<py::sequence>(obj) && !py::isinstance<py::bytes>(obj)) {
        return sequence_to_vh(py::reinterpret_borrow<py::sequence>(obj));
    }
    throw AipsError("casacoretables: unknown python data type");
}

// ---------------------------------------------------------------------------
// Record <-> dict
// ---------------------------------------------------------------------------
py::dict record_to_py(const Record& rec)
{
    py::dict d;
    uInt nf = rec.nfields();
    for (uInt i = 0; i < nf; ++i) {
        d[py::str(std::string(rec.name(i)))] = valueholder_to_py(rec.asValueHolder(i));
    }
    return d;
}

casacore::Record py_to_record(const py::handle& obj)
{
    if (!py::isinstance<py::dict>(obj)) {
        throw AipsError("casacoretables: expected a dict for a casacore Record");
    }
    py::dict d = py::reinterpret_borrow<py::dict>(obj);
    Record result;
    for (auto item : d) {
        result.defineFromValueHolder(String(item.first.cast<std::string>()),
                                     py_to_valueholder(item.second));
    }
    return result;
}

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------
py::list string_vector_to_py(const Vector<String>& v)
{
    py::list out;
    for (uInt i = 0; i < v.size(); ++i) out.append(py::str(std::string(v[i])));
    return out;
}

casacore::Vector<String> py_to_string_vector(const py::handle& obj)
{
    py::sequence seq = py::reinterpret_borrow<py::sequence>(py::object(obj, true));
    size_t n = py::len(seq);
    Vector<String> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = seq[i].cast<std::string>();
    return v;
}

// IPosition values are reversed at the python boundary so that shapes and
// slice corners (blc/trc/inc) are expressed in numpy (C) axis order, matching
// the array-axis reversal and python-casacore's behaviour.
py::tuple iposition_to_py(const IPosition& ip)
{
    size_t n = ip.size();
    py::tuple t(n);
    for (size_t i = 0; i < n; ++i) t[i] = py::int_(ip[n - 1 - i]);
    return t;
}

casacore::IPosition py_to_iposition(const py::handle& obj)
{
    py::sequence seq = py::reinterpret_borrow<py::sequence>(py::object(obj, true));
    size_t n = py::len(seq);
    IPosition ip(n);
    for (size_t i = 0; i < n; ++i) ip[i] = seq[n - 1 - i].cast<ssize_t>();
    return ip;
}

}  // namespace cct
