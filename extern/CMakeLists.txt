# ---------------------------------------------------------------------------
# casacore_tables : a self-contained, minimal-dependency static library that
# bundles just the casacore `casa` (base) and `tables` modules.
#
# Removed relative to upstream casacore:
#   * all other casacore modules (scimath, measures, ms, images, ...)
#   * heavy third-party deps: HDF5, ADIOS2, Dysco, BLAS/LAPACK/FFTW,
#     CFITSIO, WCSLIB, readline, MPI, OpenMP, Boost
#
# Remaining requirements:
#   * a C++17 compiler and the C++ standard library
#   * libdl, libm, pthreads  (system libraries, always present)
#   * bison >= 3 and flex     (build-time only, to generate the TaQL parsers)
# ---------------------------------------------------------------------------

set(CC_ROOT   ${CMAKE_CURRENT_SOURCE_DIR})          # .../extern  (holds casacore/)
set(VENDOR    ${CC_ROOT}/casacore)                  # vendored casa/ + tables/

# Version used to stamp casacore/casa/version.h (matches the vendored source).
set(CASACORE_VERSION       3.8.0)
set(PROJECT_VERSION        3.8.0)
set(PROJECT_VERSION_MAJOR  3)
set(PROJECT_VERSION_MINOR  8)
set(PROJECT_VERSION_PATCH  0)
set(CASA_DEFAULT_ALIGNMENT 32)
# SOVERSION is only used to build the name of optional TaQL/StMan plugin shared
# libraries (meas/derivedmscal), which this package does not ship; value is
# inert but must be defined for DataManager.cc / UDFBase.cc to compile.
set(CASACORE_SOVERSION 9)

# ---------------------------------------------------------------------------
# Generated headers: casacore/casa/config.h and version.h
# Placed under <build>/gen/casacore/casa so that the canonical
# <casacore/casa/config.h> include path resolves to them.
# ---------------------------------------------------------------------------
set(GEN_INC ${CMAKE_CURRENT_BINARY_DIR}/gen)
configure_file(${VENDOR}/casa/config.h.in  ${GEN_INC}/casacore/casa/config.h  @ONLY)
configure_file(${VENDOR}/casa/version.h.in ${GEN_INC}/casacore/casa/version.h @ONLY)

# ---------------------------------------------------------------------------
# Parsers: bison + flex generate three grammars, each #include'd by a
# hand-written wrapper .cc:
#     casa/Json/JsonGram      <- JsonParser.cc
#     tables/TaQL/RecordGram  <- RecordGram.cc
#     tables/TaQL/TableGram   <- TableGram.cc
# All generated outputs land in <build>/parsers, which is added to the include
# path so the quoted "<name>.ycc" / "<name>.lcc" includes resolve.
# ---------------------------------------------------------------------------
find_package(BISON 3.0 REQUIRED)
find_package(FLEX REQUIRED)

set(PARSER_GEN ${CMAKE_CURRENT_BINARY_DIR}/parsers)
file(MAKE_DIRECTORY ${PARSER_GEN})

# name  -> (subdir of vendored tree, wrapper .cc that #includes the generated files)
set(JsonGram_DIR     casa/Json)
set(JsonGram_WRAP    casa/Json/JsonParser.cc)
set(RecordGram_DIR   tables/TaQL)
set(RecordGram_WRAP  tables/TaQL/RecordGram.cc)
set(TableGram_DIR    tables/TaQL)
set(TableGram_WRAP   tables/TaQL/TableGram.cc)

foreach(g JsonGram RecordGram TableGram)
    bison_target(${g} ${VENDOR}/${${g}_DIR}/${g}.yy ${PARSER_GEN}/${g}.ycc
                 COMPILE_FLAGS "-Dapi.prefix={${g}}")
    flex_target(${g}  ${VENDOR}/${${g}_DIR}/${g}.ll ${PARSER_GEN}/${g}.lcc
                 COMPILE_FLAGS "-P${g}")
    # The wrapper #include's the generated .ycc/.lcc; make their generation an
    # explicit prerequisite so build order is correct for any generator.
    set_source_files_properties(${VENDOR}/${${g}_WRAP} PROPERTIES
        OBJECT_DEPENDS  "${PARSER_GEN}/${g}.ycc;${PARSER_GEN}/${g}.lcc"
        COMPILE_OPTIONS "-Wno-unused-but-set-variable")
endforeach()
# DataManager.cc and UDFBase.cc stringify SOVERSION for plugin lookup.
set_source_files_properties(
    ${VENDOR}/tables/DataMan/DataManager.cc
    ${VENDOR}/tables/TaQL/UDFBase.cc
    PROPERTIES COMPILE_DEFINITIONS "SOVERSION=${CASACORE_SOVERSION}")

# ---------------------------------------------------------------------------
# Sources: every .cc under the vendored casa/ and tables/ trees.
# (test/, apps/, Dysco/ and Adios2* were already dropped while vendoring;
#  HDF5/*.cc remain - they compile to "HDF5 not available" stubs without libhdf5.)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE CASA_SOURCES   CONFIGURE_DEPENDS ${VENDOR}/casa/*.cc)
file(GLOB_RECURSE TABLES_SOURCES CONFIGURE_DEPENDS ${VENDOR}/tables/*.cc)

add_library(casacore_tables STATIC ${CASA_SOURCES} ${TABLES_SOURCES})

set_target_properties(casacore_tables PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    # Hide all casacore symbols. When this static lib is linked into the
    # pybind11 module, its ~16k casacore symbols become local instead of being
    # exported, so they cannot collide/interpose with another casacore that may
    # already be loaded in the process (e.g. CASA's casatools, which embeds its
    # own, ABI-incompatible casacore). Only the module init symbol is exported.
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

target_include_directories(casacore_tables PUBLIC
    ${CC_ROOT}     # resolves  <casacore/casa/...>  and  <casacore/tables/...>
    ${GEN_INC}     # resolves  <casacore/casa/config.h> and version.h
    ${PARSER_GEN}) # resolves  "JsonGram.ycc" / "RecordGram.ycc" / "TableGram.ycc"

# Threading (std::mutex backed); matches the upstream USE_THREADS=ON default.
find_package(Threads REQUIRED)
target_compile_definitions(casacore_tables PUBLIC USE_THREADS)
target_link_libraries(casacore_tables PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

# Quiet the (very many) upstream warnings; they are not actionable here.
target_compile_options(casacore_tables PRIVATE -w)
