# Programs for manual execution, not run by ctest. They provide integration testing of the
# libraries with real packages and engines, which the automatic tests cannot provide, because
# those tests use synthetic inputs. They are development tools rather than part of the product,
# and are therefore declared as tests: built beside them in out/bin, and not installed.
add_subdirectory(installer)
add_subdirectory(runner)
