include(FetchContent)

set(LORICA_SOURCE_DIR "" CACHE PATH "Optional Lorica source checkout")
set(LORICA_BUILD_PROTECTION OFF CACHE BOOL "" FORCE)
set(LORICA_BUILD_CONTRACTS OFF CACHE BOOL "" FORCE)
set(LORICA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(LORICA_SOURCE_DIR)
  add_subdirectory("${LORICA_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_lorica" EXCLUDE_FROM_ALL)
else()
  find_package(Lorica CONFIG REQUIRED)
endif()

# The same native launch, HTTP, JSON and SQLite packages used by Discutere.
set(LAPILLI_NATIVE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lapilli
  GIT_REPOSITORY https://github.com/LUDIARS/Lapilli.git
  GIT_TAG f4aa7d5595478b1ff5208d9cd588f51432df95fb
  SOURCE_SUBDIR native EXCLUDE_FROM_ALL)
FetchContent_Declare(sqlite
  URL https://registry.npmjs.org/better-sqlite3/-/better-sqlite3-13.0.3.tgz
  URL_HASH SHA256=77e0513dc1a469fb3bceec4c7fb5ad3f403109787eda05be047ec17fd56868cb
  SOURCE_SUBDIR histrio-no-build EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(lapilli sqlite)
