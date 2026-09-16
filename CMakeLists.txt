cmake_minimum_required(VERSION 3.16)

# EF5 requires OpenMP. On macOS, the system Apple Clang does not include an
# OpenMP runtime, so prefer an installed Homebrew GCC when the caller has not
# selected a compiler explicitly. -DCMAKE_CXX_COMPILER and the CXX environment
# variable always take precedence over this fallback.
if(CMAKE_HOST_APPLE
   AND NOT DEFINED CMAKE_CXX_COMPILER
   AND "$ENV{CXX}" STREQUAL "")
  find_program(EF5_HOMEBREW_GXX
    NAMES g++-16 g++-15 g++-14 g++-13 g++-12
    HINTS /opt/homebrew/bin /usr/local/bin
    NO_DEFAULT_PATH)
  if(EF5_HOMEBREW_GXX)
    set(CMAKE_CXX_COMPILER "${EF5_HOMEBREW_GXX}" CACHE FILEPATH
      "C++ compiler selected for EF5 OpenMP support")
    message(STATUS "EF5: selected Homebrew GCC: ${EF5_HOMEBREW_GXX}")
  endif()
endif()

project(EF5 VERSION 1.2.7 LANGUAGES CXX)

set(EF5_BUILD_MODE "Debug" CACHE STRING
  "Build mode: Debug or Release")
set_property(CACHE EF5_BUILD_MODE PROPERTY STRINGS Debug Release)
if(NOT EF5_BUILD_MODE STREQUAL "Debug" AND
   NOT EF5_BUILD_MODE STREQUAL "Release")
  message(FATAL_ERROR "EF5_BUILD_MODE must be Debug or Release")
endif()
if(CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_CONFIGURATION_TYPES "${EF5_BUILD_MODE}" CACHE STRING
    "Available build configurations" FORCE)
else()
  set(CMAKE_BUILD_TYPE "${EF5_BUILD_MODE}" CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
include(FindPackageHandleStandardArgs)

option(EF5_BUILD_KWTEST "Build the kinematic-wave test program" ON)
option(EF5_BUILD_TOOLS "Build the data conversion utilities (except TRMMV6Clip)" OFF)
option(EF5_BUILD_TRMMV6_TOOL "Build TRMMV6Clip (requires HDF4 and JPEG)" OFF)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep runnable programs next to the top-level CMakeLists.txt. The cache
# variable lets callers choose another location when packaging or in CI.
set(EF5_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}" CACHE PATH
  "Directory for EF5 executables")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${EF5_RUNTIME_OUTPUT_DIRECTORY}")
foreach(config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config}
    "${EF5_RUNTIME_OUTPUT_DIRECTORY}")
endforeach()

find_package(ZLIB REQUIRED)
find_package(TIFF REQUIRED)

# Apple Clang does not ship OpenMP. Help FindOpenMP locate a Homebrew libomp
# installation while still allowing callers to supply their own hints.
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  find_path(EF5_LIBOMP_INCLUDE_DIR omp.h
    HINTS /opt/homebrew/opt/libomp/include /usr/local/opt/libomp/include)
  find_library(EF5_LIBOMP_LIBRARY omp
    HINTS /opt/homebrew/opt/libomp/lib /usr/local/opt/libomp/lib)
  if(EF5_LIBOMP_INCLUDE_DIR AND EF5_LIBOMP_LIBRARY)
    set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp" CACHE STRING
      "C++ compiler flags for OpenMP")
    set(OpenMP_CXX_INCLUDE_DIR "${EF5_LIBOMP_INCLUDE_DIR}" CACHE PATH
      "OpenMP include directory")
    set(OpenMP_CXX_LIB_NAMES omp CACHE STRING "OpenMP runtime library")
    set(OpenMP_omp_LIBRARY "${EF5_LIBOMP_LIBRARY}" CACHE FILEPATH
      "OpenMP runtime library")
  endif()
endif()

find_package(OpenMP QUIET COMPONENTS CXX)
if(NOT OpenMP_CXX_FOUND)
  if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
      "OpenMP was not found for Apple Clang. Install Homebrew libomp or "
      "configure with -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++-15. "
      "The repository's VS Code settings already select GCC 15.")
  else()
    message(FATAL_ERROR "A working C++ OpenMP implementation is required")
  endif()
endif()

# libgeotiff does not consistently install a CMake package across platforms.
# Prefer pkg-config and fall back to a conventional header/library lookup.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(GEOTIFF QUIET IMPORTED_TARGET libgeotiff)
endif()

if(TARGET PkgConfig::GEOTIFF)
  set(EF5_GEOTIFF_TARGET PkgConfig::GEOTIFF)
else()
  find_path(GEOTIFF_INCLUDE_DIR
    NAMES geotiffio.h
    PATH_SUFFIXES geotiff libgeotiff)
  find_library(GEOTIFF_LIBRARY NAMES geotiff)
  find_package_handle_standard_args(GeoTIFF DEFAULT_MSG
    GEOTIFF_LIBRARY GEOTIFF_INCLUDE_DIR)

  add_library(GeoTIFF::GeoTIFF UNKNOWN IMPORTED)
  set_target_properties(GeoTIFF::GeoTIFF PROPERTIES
    IMPORTED_LOCATION "${GEOTIFF_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${GEOTIFF_INCLUDE_DIR}")
  set(EF5_GEOTIFF_TARGET GeoTIFF::GeoTIFF)
  get_filename_component(GEOTIFF_LIBRARY_DIR "${GEOTIFF_LIBRARY}" DIRECTORY)
  set(GEOTIFF_INCLUDE_DIRS "${GEOTIFF_INCLUDE_DIR}")
  set(GEOTIFF_LIBRARY_DIRS "${GEOTIFF_LIBRARY_DIR}")
  set(GEOTIFF_LINK_LIBRARIES "${GEOTIFF_LIBRARY}")
endif()

set(EF5_UNIT_SOURCES
  src/LAEAProjection.cpp
  src/GeographicProjection.cpp
  src/DistanceUnit.cpp
  src/TimeUnit.cpp
  src/DistancePerTimeUnits.cpp
  src/TimeVar.cpp)

set(EF5_TYPE_SOURCES
  src/DatedName.cpp
  src/PETType.cpp
  src/PrecipType.cpp
  src/TempType.cpp
  src/GaugeMap.cpp)

set(EF5_CONFIG_SOURCES
  src/BasicConfigSection.cpp
  src/PrecipConfigSection.cpp
  src/PETConfigSection.cpp
  src/TempConfigSection.cpp
  src/GaugeConfigSection.cpp
  src/BasinConfigSection.cpp
  src/CaliParamConfigSection.cpp
  src/ParamSetConfigSection.cpp
  src/RoutingCaliParamConfigSection.cpp
  src/RoutingParamSetConfigSection.cpp
  src/TaskConfigSection.cpp
  src/EnsTaskConfigSection.cpp
  src/ExecuteConfigSection.cpp
  src/Config.cpp
  src/SnowCaliParamConfigSection.cpp
  src/SnowParamSetConfigSection.cpp
  src/InundationCaliParamConfigSection.cpp
  src/InundationParamSetConfigSection.cpp)

set(EF5_INPUT_SOURCES
  src/RPSkewness.cpp
  src/TimeSeries.cpp
  src/PETReader.cpp
  src/PrecipReader.cpp
  src/TempReader.cpp
  src/TifGrid.cpp
  src/BifGrid.cpp
  src/AscGrid.cpp
  src/BasicGrids.cpp
  src/TRMMRTGrid.cpp
  src/MRMSGrid.cpp
  src/GridWriter.cpp
  src/GridWriterFull.cpp
  src/GriddedOutput.cpp)

set(EF5_MODEL_SOURCES
  src/Model.cpp
  src/CRESTModel.cpp
  src/HyMOD.cpp
  src/SAC.cpp
  src/LinearRoute.cpp
  src/KinematicRoute.cpp
  src/ObjectiveFunc.cpp
  src/Simulator.cpp
  src/ARS.cpp
  src/DREAM.cpp
  src/dream_functions.cpp
  src/misc_functions.cpp
  src/Snow17Model.cpp
  src/HPModel.cpp
  src/SimpleInundation.cpp
  src/VCInundation.cpp)

set(EF5_COMMON_SOURCES
  ${EF5_UNIT_SOURCES}
  ${EF5_TYPE_SOURCES}
  ${EF5_CONFIG_SOURCES}
  ${EF5_INPUT_SOURCES}
  ${EF5_MODEL_SOURCES}
  src/ExecutionController.cpp)

if(NOT WIN32)
  list(APPEND EF5_COMMON_SOURCES src/DEMProcessor.cpp)
endif()

add_library(ef5_core STATIC ${EF5_COMMON_SOURCES})
target_include_directories(ef5_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ef5_core
  PUBLIC
    ZLIB::ZLIB
    TIFF::TIFF
    ${EF5_GEOTIFF_TARGET}
    OpenMP::OpenMP_CXX)

if(MSVC)
  target_compile_options(ef5_core PRIVATE /W4)
else()
  target_compile_options(ef5_core PRIVATE -Wall)
endif()

if(WIN32)
  enable_language(RC)
  add_executable(ef5 WIN32 src/EF5Windows.cpp src/ef5.rc)
else()
  add_executable(ef5 src/EF5.cpp)
endif()
target_link_libraries(ef5 PRIVATE ef5_core)

if(EF5_BUILD_KWTEST)
  add_executable(kwtest src/KWTest.cpp)
  target_link_libraries(kwtest PRIVATE ef5_core)
endif()

if(EF5_BUILD_TOOLS)
  add_executable(TRMMRTClip
    src/TRMMRTClip.cpp src/TRMMRTGrid.cpp src/AscGrid.cpp)
  target_include_directories(TRMMRTClip PRIVATE src)
  target_link_libraries(TRMMRTClip PRIVATE ZLIB::ZLIB)

  add_executable(TRMMDClip
    src/TRMMDClip.cpp src/TRMMDGrid.cpp src/AscGrid.cpp)
  target_include_directories(TRMMDClip PRIVATE src)
  target_link_libraries(TRMMDClip PRIVATE ZLIB::ZLIB)

  add_executable(BIFClip src/BIFClip.cpp src/BifGrid.cpp src/AscGrid.cpp)
  target_include_directories(BIFClip PRIVATE src)

  foreach(tool IN ITEMS MRMSConvert MRMSSum ComputeRP)
    if(tool STREQUAL "ComputeRP")
      add_executable(${tool} src/${tool}.cpp src/TifGrid.cpp)
    else()
      add_executable(${tool}
        src/${tool}.cpp src/TifGrid.cpp src/MRMSGrid.cpp)
    endif()
    target_include_directories(${tool} PRIVATE src)
    target_link_libraries(${tool} PRIVATE
      ZLIB::ZLIB TIFF::TIFF ${EF5_GEOTIFF_TARGET})
  endforeach()
endif()

if(EF5_BUILD_TRMMV6_TOOL)
  find_package(JPEG REQUIRED)
  find_path(HDF4_INCLUDE_DIR NAMES hdf/mfhdf.h mfhdf.h)
  find_library(HDF4_MFHDF_LIBRARY NAMES mfhdf)
  find_library(HDF4_DF_LIBRARY NAMES df hdf)
  find_package_handle_standard_args(HDF4 DEFAULT_MSG
    HDF4_INCLUDE_DIR HDF4_MFHDF_LIBRARY HDF4_DF_LIBRARY)
  if(NOT HDF4_FOUND)
    message(FATAL_ERROR
      "EF5_BUILD_TRMMV6_TOOL requires the HDF4 mfhdf and df libraries")
  endif()

  add_executable(TRMMV6Clip
    src/TRMMV6Clip.cpp src/TRMMV6Grid.cpp src/AscGrid.cpp)
  target_include_directories(TRMMV6Clip PRIVATE src "${HDF4_INCLUDE_DIR}")
  target_link_libraries(TRMMV6Clip PRIVATE
    "${HDF4_MFHDF_LIBRARY}" "${HDF4_DF_LIBRARY}" ZLIB::ZLIB JPEG::JPEG)
endif()

install(TARGETS ef5 RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Print the resolved toolchain, dependencies, and output locations after all
# optional dependencies have been discovered.
function(ef5_print_summary_item label value)
  if("${value}" STREQUAL "" OR "${value}" MATCHES "NOTFOUND")
    set(value "<not reported by CMake>")
  endif()
  message(STATUS "  ${label}: ${value}")
endfunction()

if(DEFINED ZLIB_VERSION)
  set(EF5_ZLIB_VERSION "${ZLIB_VERSION}")
else()
  set(EF5_ZLIB_VERSION "${ZLIB_VERSION_STRING}")
endif()

if(DEFINED TIFF_VERSION)
  set(EF5_TIFF_VERSION "${TIFF_VERSION}")
else()
  set(EF5_TIFF_VERSION "${TIFF_VERSION_STRING}")
endif()

find_file(EF5_OPENMP_HEADER_PATH omp.h
  HINTS ${OpenMP_CXX_INCLUDE_DIRS} ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})

if(IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
  set(EF5_INSTALL_EXECUTABLE
    "${CMAKE_INSTALL_BINDIR}/ef5${CMAKE_EXECUTABLE_SUFFIX}")
else()
  set(EF5_INSTALL_EXECUTABLE
    "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/ef5${CMAKE_EXECUTABLE_SUFFIX}")
endif()

message(STATUS "")
message(STATUS "========== EF5 configuration summary ==========")
ef5_print_summary_item("Build type" "${CMAKE_BUILD_TYPE}")
ef5_print_summary_item("Generator" "${CMAKE_GENERATOR}")
ef5_print_summary_item("C++ compiler" "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
ef5_print_summary_item("C++ compiler path" "${CMAKE_CXX_COMPILER}")
ef5_print_summary_item("C++ standard" "C++${CMAKE_CXX_STANDARD}")
message(STATUS "")
ef5_print_summary_item("OpenMP version" "${OpenMP_CXX_VERSION}")
ef5_print_summary_item("OpenMP flags" "${OpenMP_CXX_FLAGS}")
ef5_print_summary_item("OpenMP header" "${EF5_OPENMP_HEADER_PATH}")
ef5_print_summary_item("OpenMP libraries" "${OpenMP_CXX_LIBRARIES}")
message(STATUS "")
ef5_print_summary_item("zlib version" "${EF5_ZLIB_VERSION}")
ef5_print_summary_item("zlib include directories" "${ZLIB_INCLUDE_DIRS}")
ef5_print_summary_item("zlib libraries" "${ZLIB_LIBRARIES}")
message(STATUS "")
ef5_print_summary_item("libtiff version" "${EF5_TIFF_VERSION}")
ef5_print_summary_item("libtiff include directories" "${TIFF_INCLUDE_DIRS}")
ef5_print_summary_item("libtiff libraries" "${TIFF_LIBRARIES}")
message(STATUS "")
ef5_print_summary_item("libgeotiff version" "${GEOTIFF_VERSION}")
ef5_print_summary_item("libgeotiff include directories" "${GEOTIFF_INCLUDE_DIRS}")
ef5_print_summary_item("libgeotiff library directories" "${GEOTIFF_LIBRARY_DIRS}")
ef5_print_summary_item("libgeotiff libraries" "${GEOTIFF_LINK_LIBRARIES}")

if(EF5_BUILD_TRMMV6_TOOL)
  message(STATUS "")
  ef5_print_summary_item("JPEG include directories" "${JPEG_INCLUDE_DIRS}")
  ef5_print_summary_item("JPEG libraries" "${JPEG_LIBRARIES}")
  ef5_print_summary_item("HDF4 include directory" "${HDF4_INCLUDE_DIR}")
  ef5_print_summary_item("HDF4 mfhdf library" "${HDF4_MFHDF_LIBRARY}")
  ef5_print_summary_item("HDF4 df library" "${HDF4_DF_LIBRARY}")
endif()

message(STATUS "")
ef5_print_summary_item("Executable output" "${EF5_RUNTIME_OUTPUT_DIRECTORY}/ef5${CMAKE_EXECUTABLE_SUFFIX}")
ef5_print_summary_item("Install prefix" "${CMAKE_INSTALL_PREFIX}")
ef5_print_summary_item("Installed executable" "${EF5_INSTALL_EXECUTABLE}")
message(STATUS "================================================")
message(STATUS "")
