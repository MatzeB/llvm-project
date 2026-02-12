# Test usage of LLD as a library.
# This test covers a more singular case where only one LLD driver is used in the
# target application executable.

add_lld_unittests(LLDAsLibELFTests
  # facebook begin T96340746
  RelocationDiagramTest.cpp
  # facebook end T96340746
  ROCm.cpp
  SomeDrivers.cpp
)

target_link_libraries(LLDAsLibELFTests
  PRIVATE
  lldCommon
  lldELF
)
