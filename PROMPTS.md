# PROMPTS.md

The user prompts that produced this package, in order, recorded verbatim
(including original typos). Interleaved are the answers given to the clarifying
multiple-choice questions the agent asked. This is a development-history record,
not documentation — see `AGENTS.md` and `README.md` for how the package works.

---

## Prompt 1 — initial task

> Your are tasked to create a stand alone package that only contains the table
> module of casacore and provide a pybind11 interface. The dependancies should
> be kept to a minimum and should be easy to build and install.
>
> Casacore repository: /Users/jsteeb/Dropbox/viper_dev/casacore
> Always use conda zinc environment
> Measures data can be found here: /Users/jsteeb/.casa/data
>
> Overview of steps:
> 1.) Make sure that you can build casacore and run unit tests
> 3.) For the table module remove as many dependancies as possible (including to
>     other casacore modules, boost, etc.) and make use of the standard library.
> 4.) Build again and ensure unit tests pass.
> 5.) Create a new Python package called casacoretables in
>     /Users/jsteeb/Dropbox/viper_dev with a similar layout as
>     /Users/jsteeb/Dropbox/viper_dev/astroviper (pyproject.toml, cmake, etc.)
>     and include casacore unit tests.
> 6.) Create a Pybind11 based interface similar python-casacore
>     (/Users/jsteeb/Dropbox/viper_dev/python-casacore). Ensure that no
>     uncessary copies are made are large arrays between Python and C++.
> 7.) Copy over unit tests from python-casacore and make sure they run.

### Clarifying questions asked, and the user's answers

1. **Decouple depth** — *How far should dependency removal go for the `tables`
   module?* → **"Also replace foundation types"** (replace Array/Record/String
   with std/xtensor throughout — chosen over "Bundle minimal casa+tables").

2. **TaQL** — *Keep the TaQL query language?* → **"Keep TaQL"**.

3. **Source layout** — *How should the standalone package source the C++ code?*
   → **"Vendor a trimmed copy"** (self-contained copy under the package).

After the agent discovered that wholesale Array/Record replacement breaks the
on-disk table format, it asked one more:

4. **Sequencing** — *How should I sequence the work, given that?* →
   **"Working package first (A+B), then evaluate C"** (deliver a working
   standalone package and the safe std reductions first; reassess foundation-
   type replacement afterwards).

---

## Prompt 2

> Why would replacing casacore::Array/Record with std/xtensor throughout break
> the on-disk table format?

---

## Prompt 3

> ls

---

## Prompt 4

> I would like you to add automated github workflows. Please go over all the
> templates in /Users/jsteeb/Dropbox/viper_dev/gh-actions-templates-public and
> list all the ones that can be used and what modifications would be neccesary.

---

## Prompt 5

> Please do the following:
> - Add a workflow that calls the black-template.yml
> - Create a python-publish-cpp.yml don't call the template and put it in
>   casacoretable
> - Create python-testing-linux.yml and python-testing-macos.yml and don't call
>   the template and put it in casacoretable
> - Create a github workflow that runs the C++ tests and put it in casacoretable

---

## Prompt 6

> - Add all extra to pyproject.toml
> - There are certain curcumstance were I want to run CASA and casacoretables in
>   the same Python enviroment. However I suspect there might be symbol crashes.
>   Please esnure that this is not the case.
> - Please create an ipynb showing how to use the entire Python API and put it
>   in docs. Add a reference and link to it in the readme.

---

## Prompt 7

> In the casacoretables_api_tour.ipynb don't use casa just use casacoretables.

---

## Prompt 8

> Is it possible to disable file locks when only reading data (getcol)?

---

## Prompt 9

> Thank you for doing a good job. Please write a comprehensive agent.md for
> casacoretables so that future claudes can do a good job. Also please create
> another file with all the prompts that were given.
