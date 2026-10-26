# Explicit configuration scope. Missing real dependencies fail closed, while the
# portable suite records every unregistered integration test in its inventory.
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(DRAWBORAD_TEST_PROFILE "CorePortable" CACHE STRING "CorePortable or RealWindowsIntegration")
set_property(CACHE DRAWBORAD_TEST_PROFILE PROPERTY STRINGS CorePortable RealWindowsIntegration)
set(DRAWBORAD_ONNXRUNTIME_ROOT "" CACHE PATH "Offline ONNX Runtime SDK root")
set(DRAWBORAD_TEST_COFFEE_IMAGE "" CACHE FILEPATH "Exact 600x400 coffee PNG")
set(DRAWBORAD_TEST_COFFEE_PACKAGE "" CACHE PATH "User-selected local SAM 2.1 package")
set(DRAWBORAD_TEST_COFFEE_REFERENCE "" CACHE PATH "Official coffee tensor/mask reference")
set(DRAWBORAD_TEST_COFFEE_AUTO_REFERENCE "" CACHE PATH "Official automatic coffee masks")
set(DRAWBORAD_TEST_FULL_SCENE_REFERENCE "" CACHE PATH "Official point/box full-scene reference")
set(DRAWBORAD_TEST_PROBE_ENCODER "" CACHE FILEPATH "Developer-only ONNX adapter probe encoder")
set(DRAWBORAD_TEST_PROBE_DECODER "" CACHE FILEPATH "Developer-only ONNX adapter probe decoder")

function(_drawborad_json_string out text)
  string(REPLACE "\\" "\\\\" value "${text}")
  string(REPLACE "\"" "\\\"" value "${value}")
  string(REPLACE "\n" "\\n" value "${value}")
  string(REPLACE "\r" "\\r" value "${value}")
  string(REPLACE "\t" "\\t" value "${value}")
  set(${out} "\"${value}\"" PARENT_SCOPE)
endfunction()

function(_drawborad_json_member object key value)
  _drawborad_json_string(quoted "${value}")
  string(JSON updated SET "${${object}}" "${key}" "${quoted}")
  set(${object} "${updated}" PARENT_SCOPE)
endfunction()

function(drawborad_release_inventory stage)
  set(facts "{}")
  _drawborad_json_member(facts stage "${stage}")
  _drawborad_json_member(facts profile "${DRAWBORAD_TEST_PROFILE}")
  _drawborad_json_member(facts source_dir "${PROJECT_SOURCE_DIR}")
  _drawborad_json_member(facts configuration "${CMAKE_BUILD_TYPE}")
  _drawborad_json_member(facts host_system "${CMAKE_HOST_SYSTEM_NAME}")
  _drawborad_json_member(facts host_version "${CMAKE_HOST_SYSTEM_VERSION}")
  set(dependencies "{}")
  foreach(name IN ITEMS PROJECT_VERSION WIN32 MSVC MSVC_VERSION CMAKE_SIZEOF_VOID_P CMAKE_GENERATOR
      CMAKE_GENERATOR_PLATFORM CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_ID CMAKE_CXX_COMPILER_VERSION
      CMAKE_BUILD_TYPE CMAKE_CONFIGURATION_TYPES CMAKE_TOOLCHAIN_FILE CMAKE_PREFIX_PATH
      VCPKG_INSTALLED_DIR VCPKG_TARGET_TRIPLET DRAWBORAD_DESKTOP DRAWBORAD_QUALIFY
      DRAWBORAD_WINDOWS_VALIDATION DRAWBORAD_SANITIZE DRAWBORAD_ONNXRUNTIME_ROOT
      DRAWBORAD_ORT_INCLUDE DRAWBORAD_ORT_LIBRARY DRAWBORAD_ORT_RUNTIME
      DRAWBORAD_HAS_ONNXRUNTIME Qt6_FOUND Qt6Test_FOUND Qt6_VERSION Qt6_DIR
      PNG_FOUND JPEG_FOUND ZLIB_FOUND TIFF_FOUND DRAWBORAD_LCMS2_LIBRARY
      DRAWBORAD_WEBP_LIBRARY DRAWBORAD_HEIF_LIBRARY
      DRAWBORAD_TEST_COFFEE_IMAGE DRAWBORAD_TEST_COFFEE_PACKAGE DRAWBORAD_TEST_COFFEE_REFERENCE
      DRAWBORAD_TEST_COFFEE_AUTO_REFERENCE DRAWBORAD_TEST_FULL_SCENE_REFERENCE
      DRAWBORAD_TEST_ONNX_MODEL DRAWBORAD_TEST_SAM_ENCODER DRAWBORAD_TEST_SAM_DECODER
      DRAWBORAD_TEST_PROBE_ENCODER DRAWBORAD_TEST_PROBE_DECODER)
    _drawborad_json_member(dependencies "${name}" "${${name}}")
  endforeach()
  string(JSON facts SET "${facts}" dependencies "${dependencies}")
  # Include the complete effective cache, including configuration-specific
  # compiler/linker flags. A reconfigure must invalidate an older test receipt
  # even when the Git SHA, profile and registered test names are unchanged.
  get_cmake_property(cache_names CACHE_VARIABLES)
  set(effective_cache "{}")
  foreach(name IN LISTS cache_names)
    _drawborad_json_member(effective_cache "${name}" "${${name}}")
  endforeach()
  string(JSON facts SET "${facts}" cmake_cache "${effective_cache}")
  set(registered "[]")
  if(stage STREQUAL "inventory")
    get_property(test_names DIRECTORY "${PROJECT_SOURCE_DIR}/tests" PROPERTY TESTS)
    set(index 0)
    foreach(name IN LISTS test_names)
      get_test_property("${name}" LABELS labels)
      set(row "{}")
      _drawborad_json_member(row name "${name}")
      set(label_array "[]")
      set(label_index 0)
      foreach(label IN LISTS labels)
        _drawborad_json_string(quoted "${label}")
        string(JSON label_array SET "${label_array}" ${label_index} "${quoted}")
        math(EXPR label_index "${label_index}+1")
      endforeach()
      string(JSON row SET "${row}" labels "${label_array}")
      string(JSON registered SET "${registered}" ${index} "${row}")
      math(EXPR index "${index}+1")
    endforeach()
    set(output "${CMAKE_BINARY_DIR}/TEST_INVENTORY.json")
  else()
    set(output "${CMAKE_BINARY_DIR}/CONFIGURE_STATUS.json")
  endif()
  string(JSON facts SET "${facts}" registered "${registered}")
  file(WRITE "${CMAKE_BINARY_DIR}/release-config-${stage}.json" "${facts}\n")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/qa/release_inputs.py"
    --facts "${CMAKE_BINARY_DIR}/release-config-${stage}.json" --output "${output}"
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Blocked: release configuration inputs incomplete; see ${output}")
  endif()
endfunction()
