#!/bin/sh
# run_assay.sh <assaydir> <module> <testname> <exe>
#
# Runs one vendored casacore C++ test through casacore's `casacore_assay`
# harness (honouring .run scripts, stdin .in files and .out comparison with
# floating-point tolerance).
#
# Each test runs in a private temporary directory under $TMPDIR, NOT in the
# build tree. This:
#   * isolates tests from each other (casacore tests share table-file names and
#     would otherwise collide, especially under parallel ctest), and
#   * avoids file-sync/locking systems (e.g. Dropbox) on the source tree that
#     break casacore's table locking ("table ... is in use in another process").
#
# The temp tree mirrors tables/<module>/test so that the few tests referencing
# ../../Tables/test/<data> resolve.
assaydir="$1"
module="$2"
tst="$3"
exe="$4"

work=$(mktemp -d "${TMPDIR:-/tmp}/cct_${tst}.XXXXXX") || exit 1
wd="$work/tables/$module/test"
mkdir -p "$wd" "$work/tables/Tables/test"

# Reference table data used by tTable_2 and several TaQL tests.
cp -R "$assaydir/Tables/tTable_2.data_v0" "$assaydir/Tables/tTable_2.data_v1" \
      "$work/tables/Tables/test/" 2>/dev/null

# The test executable and its helper files.
cp "$exe" "$wd/$tst"
for typ in run py in out; do
    [ -e "$assaydir/$module/$tst.$typ" ] && cp "$assaydir/$module/$tst.$typ" "$wd/"
done
for f in "$assaydir/$module/$tst".in_*; do
    [ -e "$f" ] && cp -R "$f" "$wd/"
done 2>/dev/null

cd "$wd" || exit 1
testsrcdir=.
casa_checktool=
export testsrcdir casa_checktool
"$assaydir/casacore_assay" "./$tst"
status=$?

cd / && rm -rf "$work"
[ "$status" = "3" ] && status=0       # UNTESTED -> success
exit "$status"
