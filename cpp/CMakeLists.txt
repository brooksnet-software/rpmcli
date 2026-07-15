cmake_minimum_required(VERSION 3.15)
project(rpmcli_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JSON backend: "nlohmann" (default, vendored single header) or "boost".
set(RPM_JSON_BACKEND "nlohmann" CACHE STRING "JSON backend: nlohmann or boost")
set_property(CACHE RPM_JSON_BACKEND PROPERTY STRINGS nlohmann boost)

# ---- rpm client library ----------------------------------------------------
add_library(rpm STATIC
  src/RPCConnection.cpp
  src/RPM.cpp
)
target_include_directories(rpm PUBLIC include)

if(RPM_JSON_BACKEND STREQUAL "boost")
  message(STATUS "JSON backend: Boost.JSON")
  target_compile_definitions(rpm PUBLIC RPM_JSON_BOOST)
  # Prefer a real Boost install; otherwise fall back to header-only mode using
  # boost/json/src.hpp, which only needs the Boost headers (BOOST_ROOT).
  find_package(Boost 1.75 QUIET COMPONENTS json)
  if(Boost_FOUND)
    target_link_libraries(rpm PUBLIC Boost::json)
  else()
    if(NOT BOOST_ROOT AND DEFINED ENV{BOOST_ROOT})
      set(BOOST_ROOT $ENV{BOOST_ROOT})
    endif()
    if(NOT BOOST_ROOT AND DEFINED ENV{BOOST_HOME})
      set(BOOST_ROOT $ENV{BOOST_HOME})
    endif()
    if(NOT BOOST_ROOT)
      message(FATAL_ERROR
        "Boost.JSON not found. Pass -DBOOST_ROOT=<path-to-boost> for the boost backend.")
    endif()
    message(STATUS "Using header-only Boost.JSON from ${BOOST_ROOT}")
    target_include_directories(rpm PUBLIC ${BOOST_ROOT})
    target_sources(rpm PRIVATE src/json_boost_src.cpp)
    # Header-only mode compiles Boost.JSON from src.hpp, so suppress Boost's
    # auto-link pragmas that would otherwise demand a prebuilt libboost_json.
    target_compile_definitions(rpm PUBLIC BOOST_ALL_NO_LIB)
  endif()
else()
  message(STATUS "JSON backend: nlohmann/json")
  target_include_directories(rpm PUBLIC third_party)  # <nlohmann/json.hpp>
endif()

if(WIN32)
  target_link_libraries(rpm PUBLIC ws2_32)
endif()

# ---- standalone utilities --------------------------------------------------
foreach(tool jobadd jobhold queuesuspend resetsequence)
  add_executable(${tool} tools/${tool}.cpp)
  target_include_directories(${tool} PRIVATE tools)
  target_link_libraries(${tool} PRIVATE rpm)
endforeach()

# ---- read-only smoke test --------------------------------------------------
add_executable(smoketest tests/smoketest.cpp)
target_link_libraries(smoketest PRIVATE rpm)
