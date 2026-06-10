# The casacoretables._tables pybind11 extension module.
# Links the standalone casacore_tables static library; numpy is accessed
# through pybind11/numpy.h (loaded at runtime), so no numpy link is needed.

find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_tables
    module.cc
    convert.cc)

target_link_libraries(_tables PRIVATE casacore_tables)
target_compile_features(_tables PRIVATE cxx_std_17)

# Keep the embedded casacore private to this module so it cannot clash with a
# different casacore loaded elsewhere in the process (e.g. CASA's casatools).
# pybind11 builds the module with hidden visibility; in addition, on ELF
# platforms strip the static lib's symbols from the module's dynamic table.
set_target_properties(_tables PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(NOT APPLE AND NOT WIN32)
    target_link_options(_tables PRIVATE "LINKER:--exclude-libs,ALL")
endif()

# Installed next to the python wrapper layer so it imports as
# casacoretables.tables._tables (mirrors python-casacore's casacore.tables._tables).
install(TARGETS _tables DESTINATION casacoretables/tables)
