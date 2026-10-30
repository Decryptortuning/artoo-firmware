cmake_minimum_required(VERSION 3.28)
project(artoo LANGUAGES C CXX)
# Match the existing GCC-driver link: no automatic libstdc++ introduction.
set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(ARTOO_PROFILE production CACHE STRING "production, factory or bench")
set(ARTOO_BOARD dvt CACHE STRING "dvt or bb03")
set(ARTOO_PYTHON "${CMAKE_SOURCE_DIR}/.venv/bin/python" CACHE FILEPATH "Pinned host Python")
set(ARTOO_HOST_CC "/usr/bin/gcc" CACHE FILEPATH "Native asset compiler")
set(ARTOO_MAVLINK_EXPORT "" CACHE PATH "Verified standalone Solo MAVLink export")
set(ARTOO_TOOLCHAIN_ARCHIVE "" CACHE FILEPATH "Pinned candidate Arm toolchain archive")
set(ARTOO_PILLOW_WHEEL "" CACHE FILEPATH "Pinned candidate Pillow wheel")
if(NOT EXISTS "${ARTOO_MAVLINK_EXPORT}/manifest.json")
  message(FATAL_ERROR "Set ARTOO_MAVLINK_EXPORT to the reviewed immutable Solo header export")
endif()
file(GLOB_RECURSE mavlink_headers CONFIGURE_DEPENDS "${ARTOO_MAVLINK_EXPORT}/include/*")
if(NOT ARTOO_PROFILE MATCHES "^(production|factory|bench)$" OR NOT ARTOO_BOARD MATCHES "^(dvt|bb03)$")
  message(FATAL_ERROR "Invalid Artoo board/profile")
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Artoo requires an out-of-tree build")
endif()
foreach(stale resources-gen.cpp resources-gen.h version.cpp)
  if(EXISTS "${CMAKE_SOURCE_DIR}/src/${stale}")
    message(FATAL_ERROR "Stale Tup output src/${stale}; use scripts/build-wsl.sh to remove only known ignored outputs")
  endif()
endforeach()
if(ARTOO_BOARD STREQUAL "dvt")
  set(board_id 3)
else()
  set(board_id 2)
endif()
set(factory 0)
set(bench 0)
if(ARTOO_PROFILE STREQUAL "factory")
  set(factory 1)
elseif(ARTOO_PROFILE STREQUAL "bench")
  set(bench 1)
endif()
include(cmake/sources.cmake)
file(GLOB actual_sources CONFIGURE_DEPENDS RELATIVE "${CMAKE_SOURCE_DIR}" "src/*.cpp" "src/stm32/*.cpp")
set(expected_sources ${ARTOO_CPP_SOURCES})
list(SORT actual_sources)
list(SORT expected_sources)
if(NOT actual_sources STREQUAL expected_sources)
  message(FATAL_ERROR "Source inventory changed; review and update cmake/sources.cmake")
endif()
set(gen "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${gen}")
# Always inventory actual inputs and refresh version on commit/dirty changes.
# The helper replaces version.cpp only when content changes.
add_custom_target(artoo_inputs
  COMMAND "${ARTOO_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/build_record.py" prepare
    --root "${CMAKE_SOURCE_DIR}" --build "${CMAKE_BINARY_DIR}"
    --profile "${ARTOO_PROFILE}" --board "${ARTOO_BOARD}"
    --cc "${CMAKE_C_COMPILER}" --cxx "${CMAKE_CXX_COMPILER}" --host-cc "${ARTOO_HOST_CC}"
    --mavlink-export "${ARTOO_MAVLINK_EXPORT}" --toolchain-archive "${ARTOO_TOOLCHAIN_ARCHIVE}" --pillow-wheel "${ARTOO_PILLOW_WHEEL}"
  BYPRODUCTS "${gen}/version.cpp" "${CMAKE_BINARY_DIR}/build-inputs.json"
  DEPENDS "${ARTOO_MAVLINK_EXPORT}/manifest.json" ${mavlink_headers}
  VERBATIM)
file(GLOB_RECURSE asset_inputs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/*" "${CMAKE_SOURCE_DIR}/vendor/tamp/*")
add_custom_command(OUTPUT "${gen}/resources-gen.cpp" "${gen}/resources-gen.h"
  COMMAND "${CMAKE_COMMAND}" -E env "ARTOO_HOST_CC=${ARTOO_HOST_CC}"
    "${ARTOO_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/assetgen.py" resources/assets.cfg "${gen}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  DEPENDS ${asset_inputs} tools/assetgen.py tools/assetcodec.py tools/tampencode.c src/tampconfig.h requirements-build.txt "${CMAKE_BINARY_DIR}/build-inputs.json"
  VERBATIM)
add_custom_target(artoo_assets DEPENDS "${gen}/resources-gen.cpp" "${gen}/resources-gen.h")
add_dependencies(artoo_assets artoo_inputs)
# Tup linked basename-sorted object files, including the generated resources and
# version. Keep that order to preserve init-array order during the parity wave.
set(all_sources ${ARTOO_CPP_SOURCES} "${gen}/resources-gen.cpp"
  vendor/tamp/common.c vendor/tamp/decompressor.c)
set(ordered_sources "")
foreach(source IN LISTS all_sources)
  get_filename_component(stem "${source}" NAME_WE)
  if(source MATCHES "^vendor/tamp/")
    set(stem "tamp-${stem}")
  endif()
  list(APPEND ordered_sources "${stem}.o|${source}")
endforeach()
list(SORT ordered_sources)
set(all_sources "")
foreach(pair IN LISTS ordered_sources)
  string(REGEX REPLACE "^[^|]+\\|" "" source "${pair}")
  list(APPEND all_sources "${source}")
endforeach()
add_executable(artoo ${all_sources})
add_library(artoo_version OBJECT "${gen}/version.cpp")
add_dependencies(artoo_version artoo_inputs)
add_dependencies(artoo artoo_assets artoo_inputs)
add_dependencies(artoo artoo_version)
set_target_properties(artoo PROPERTIES SUFFIX ".elf" LINKER_LANGUAGE C C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
set_target_properties(artoo_version PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
foreach(compile_target artoo artoo_version)
target_include_directories(${compile_target} PRIVATE "${gen}" "${CMAKE_SOURCE_DIR}/src" "${ARTOO_MAVLINK_EXPORT}/include")
target_compile_definitions(${compile_target} PRIVATE BOARD=${board_id} ARTOO_FACTORY_TESTS=${factory} ARTOO_BENCH_NO_IDLE=${bench} MAVLINK_COMM_NUM_BUFFERS=1)
target_compile_options(${compile_target} PRIVATE -mthumb -mcpu=cortex-m3 -msoft-float -Os -ggdb -fstack-usage
  -Wall -Werror -Wundef -Wextra -Wshadow -Wredundant-decls -fno-common -ffunction-sections -fdata-sections -ffast-math
  -include "${CMAKE_SOURCE_DIR}/src/tampconfig.h"
  "$<$<COMPILE_LANGUAGE:C>:-Wimplicit-function-declaration;-Wmissing-prototypes;-Wstrict-prototypes>"
  "$<$<COMPILE_LANGUAGE:CXX>:-Weffc++;-fno-exceptions;-fno-threadsafe-statics;-fno-rtti>")
endforeach()
target_link_options(artoo PRIVATE -mthumb -mcpu=cortex-m3 -msoft-float --static --specs=nano.specs -nostartfiles -u _printf_float
  "-T${CMAKE_SOURCE_DIR}/src/ldscript.ld" "-Wl,-Map=${CMAKE_BINARY_DIR}/artoo.map,--cref,--gc-sections,-u,IVT")
# Tup emits its version object after the library group. Passing the owned object
# as a linker argument preserves that placement (CMake otherwise moves object
# expressions ahead of all libraries). It is not a second compilation graph.
target_link_libraries(artoo PRIVATE -Wl,--start-group c m gcc nosys -Wl,--end-group "-Wl,$<TARGET_OBJECTS:artoo_version>")
set_property(TARGET artoo APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/src/ldscript.ld")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/artoo.bin" "${CMAKE_BINARY_DIR}/artoo.hex"
  COMMAND "${CMAKE_OBJCOPY}" -O binary $<TARGET_FILE:artoo> "${CMAKE_BINARY_DIR}/artoo.bin"
  COMMAND "${CMAKE_OBJCOPY}" -O ihex $<TARGET_FILE:artoo> "${CMAKE_BINARY_DIR}/artoo.hex"
  DEPENDS artoo VERBATIM)
add_custom_target(artoo_manifest ALL
  COMMAND "${ARTOO_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/build_record.py" finish
    --root "${CMAKE_SOURCE_DIR}" --build "${CMAKE_BINARY_DIR}"
    --profile "${ARTOO_PROFILE}" --board "${ARTOO_BOARD}"
    --cc "${CMAKE_C_COMPILER}" --cxx "${CMAKE_CXX_COMPILER}" --host-cc "${ARTOO_HOST_CC}"
    --mavlink-export "${ARTOO_MAVLINK_EXPORT}" --toolchain-archive "${ARTOO_TOOLCHAIN_ARCHIVE}" --pillow-wheel "${ARTOO_PILLOW_WHEEL}"
  DEPENDS "${CMAKE_BINARY_DIR}/artoo.bin" "${CMAKE_BINARY_DIR}/artoo.hex" VERBATIM)
