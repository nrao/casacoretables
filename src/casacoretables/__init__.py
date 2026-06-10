"""casacoretables: a standalone, minimal-dependency build of casacore's
table system with a pybind11 interface.

This package bundles only casacore's ``casa`` (base) and ``tables`` modules and
exposes them through :mod:`casacoretables.tables`, whose API mirrors
``python-casacore``'s ``casacore.tables``.
"""

from . import tables

__all__ = ["tables"]
