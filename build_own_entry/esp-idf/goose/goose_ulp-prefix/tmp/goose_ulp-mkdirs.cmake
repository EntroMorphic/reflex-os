# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/aaronjosserand-austin/Projects/esp-idf/components/ulp/cmake")
  file(MAKE_DIRECTORY "/Users/aaronjosserand-austin/Projects/esp-idf/components/ulp/cmake")
endif()
file(MAKE_DIRECTORY
  "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/esp-idf/goose/goose_ulp"
  "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/esp-idf/goose/goose_ulp-prefix"
  "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/esp-idf/goose/goose_ulp-prefix/tmp"
  "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/esp-idf/goose/goose_ulp-prefix/src/goose_ulp-stamp"
  "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/esp-idf/goose/goose_ulp-prefix/src"
  "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/esp-idf/goose/goose_ulp-prefix/src/goose_ulp-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/esp-idf/goose/goose_ulp-prefix/src/goose_ulp-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/esp-idf/goose/goose_ulp-prefix/src/goose_ulp-stamp${cfgdir}") # cfgdir has leading slash
endif()
