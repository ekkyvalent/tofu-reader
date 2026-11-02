add_executable(ReadStoreTest ReadStoreTest.cpp ${REPO_ROOT}/src/ReadStore.cpp)

target_include_directories(ReadStoreTest PRIVATE
  stubs
  ../library_builder/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/LibraryIndex
)

target_link_libraries(ReadStoreTest PRIVATE crosspoint_test_common GTest::gtest_main)

target_compile_definitions(ReadStoreTest PRIVATE CROSSPOINT_UNIT_TESTS=1)

gtest_discover_tests(ReadStoreTest TEST_PREFIX "ReadStoreTest.")
