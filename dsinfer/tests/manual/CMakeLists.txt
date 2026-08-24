# Tools that need a model, a dictionary or some other input handed to them on the command line. They
# are built alongside the automatic tests but never registered with ctest.

add_subdirectory(txtdict EXCLUDE_FROM_ALL)

add_subdirectory(onnxdriver EXCLUDE_FROM_ALL)
