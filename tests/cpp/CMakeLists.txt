# ---------------------------------------------------------------------------
# Vendored casacore C++ unit tests for the tables engine, built against the
# standalone casacore_tables library and registered with CTest.
#
# The casacore tests self-validate via AlwaysAssert/abort (non-zero exit on
# failure), so we run each executable directly rather than via the upstream
# `assay` output-comparison harness. A handful of upstream tests that rely on
# .run shell scripts or interactive input are intentionally not registered.
#
# Tests are run from a directory tree that mirrors the upstream
# tables/<Module>/test layout so that relative data-file paths resolve.
# ---------------------------------------------------------------------------

# Per-module compiled-test lists (from upstream <module>/test/CMakeLists.txt).
set(Tables_TESTS
    ascii2Table tArrayColumnSlices tArrayColumnCellSlices tColumnsIndex
    tColumnsIndexArray tConcatRows tConcatTable tConcatTable2 tConcatTable3
    tMemoryTable tReadAsciiTable tReadAsciiTable2 tRefRows tRefTable tRowCopier
    tScalarRecordColumn tTable tTableAccess tTableCopy tTableCopyPerf tTableDesc
    tTableDescHyper tTableInfo tTableIter tTableKeywords tTableLock
    tTableLockSync tTableLockSync_2 tTableRecord tTableRow tTableTrace
    tTableUtil tTableVector tTable_1 tTable_2 tTable_3 tTable_4 tVeryBigTable)

set(DataMan_TESTS
    dRetypedArrayEngine dVSCEngine dVirtColEng nISMBucket tBitFlagsEngine
    tCompressComplex tCompressFloat tDataManInfo tExternalStMan tExternalStManNew
    tForwardCol tForwardColRow tIncrementalStMan tMappedArrayEngine tMemoryStMan
    tScaledArrayEngine tScaledComplexData tSSMAddRemove tSSMStringHandler
    tStandardStMan tStArrayFile tStMan tStMan1 tStManAll tTiledBool
    tTiledCellStM_1 tTiledCellStMan tTiledColumnStMan tTiledDataStM_1
    tTiledDataStMan tTiledEmpty tTiledFileAccess tTiledShapeStM_1
    tTiledShapeStM_2 tTiledShapeStMan tTiledStMan tTSMShape tVirtColEng
    tVirtualTaQLColumn tVACEngine tVSCEngine)

set(TaQL_TESTS
    tExprGroup tExprGroupArray tExprNode tExprNodeSet tExprNodeSetElem
    tExprNodeSetOpt tExprUnitNode tExprNodeUDF tMArray tMArrayMath tMArrayUtil
    tRecordExpr tRecordGram tRecordGramTable tTableExprData tTableGram
    tTableGramError tTableGramFunc tTaQLNode)

set(LogTables_TESTS tLoggerHolder tLogging)

# casacore tests self-validate via AlwaysAssert, but several are driven by a
# companion .run script and/or compare stdout against a .out file. We run every
# test through casacore's own assay harness (adapted by run_assay.sh), which
# also runs each test in a private temp directory for isolation.
set(ASSAY_DIR ${CMAKE_CURRENT_SOURCE_DIR})

foreach(module Tables DataMan TaQL LogTables)
    foreach(test ${${module}_TESTS})
        set(src ${CMAKE_CURRENT_SOURCE_DIR}/${module}/${test}.cc)
        if(EXISTS ${src})
            add_executable(${test} ${src})
            target_link_libraries(${test} PRIVATE casacore_tables)
            # tests #include a sibling test .h (e.g. tTableDesc.h, dVACEngine.h)
            target_include_directories(${test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${module})
            add_test(NAME ${test}
                     COMMAND sh ${ASSAY_DIR}/run_assay.sh
                             ${ASSAY_DIR} ${module} ${test} $<TARGET_FILE:${test}>)
        endif()
    endforeach()
endforeach()
