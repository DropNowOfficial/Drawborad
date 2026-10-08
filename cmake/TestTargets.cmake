add_test(NAME F01 COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/build/test_dependency_gate.py)
function(native_task id source)
  add_executable(test_${id} ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp ${PROJECT_SOURCE_DIR}/${source})
  target_include_directories(test_${id} PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_${id} PRIVATE drawborad_core)
  add_test(NAME ${id} COMMAND test_${id})
  set_tests_properties(${id} PROPERTIES TIMEOUT 60)
endfunction()

native_task(F02 tests/core/DocumentSpecTests.cpp)
native_task(F03 tests/core/CommandTests.cpp)
native_task(F04 tests/storage/RecoveryTests.cpp)
native_task(F05 tests/jobs/BrokerTests.cpp)

add_executable(worker_smoke ${PROJECT_SOURCE_DIR}/tests/jobs/WorkerSmoke.cpp)
target_link_libraries(worker_smoke PRIVATE drawborad_core)
add_test(NAME F05.NativeTransport COMMAND worker_smoke $<TARGET_FILE:DrawboradWorker>)
set_tests_properties(F05.NativeTransport PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 20)
add_test(NAME F05.ProcessProtocol COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/jobs/test_worker_process.py $<TARGET_FILE:DrawboradWorker>)

native_task(F06.Controller tests/platform/NativeSmokeTests.cpp)

if(DRAWBORAD_DESKTOP)
  add_test(NAME F06 COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/platform/test_native_smoke.py $<TARGET_FILE:Drawborad>)
else()
  add_test(NAME F06 COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/platform/test_native_smoke.py -)
endif()
set_tests_properties(F06 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
if(WIN32 AND DRAWBORAD_DESKTOP)
  get_filename_component(_drawborad_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
  set_tests_properties(F06 PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_drawborad_qt_prefix}/bin")
endif()

add_test(NAME F01.Runner COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/build/test_task_runner.py)
if(DRAWBORAD_DESKTOP)
  add_test(
    NAME R00.BuildFreshness
    COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/platform/test_build_freshness.py
            ${CMAKE_BINARY_DIR}/drawborad_expected_build_token.txt
            $<TARGET_FILE_DIR:Drawborad>/drawborad_build_token.txt)
endif()

function(render_task id source)
  add_executable(test_${id} ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp ${PROJECT_SOURCE_DIR}/${source})
  target_include_directories(test_${id} PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_${id} PRIVATE drawborad_render)
  add_test(NAME ${id} COMMAND test_${id})
  set_tests_properties(${id} PROPERTIES TIMEOUT 60)
endfunction()
render_task(R01 tests/render/ReferenceTests.cpp)
render_task(R02 tests/render/DirtyTileTests.cpp)
render_task(R03 tests/brush/InputTests.cpp)
render_task(R04 tests/history/ReplayTests.cpp)
render_task(R05 tests/view/CoordinateTests.cpp)
render_task(R06 tests/render/ResourceTests.cpp)
add_executable(render_bench ${PROJECT_SOURCE_DIR}/tools/bench/render_bench.cpp)
target_link_libraries(render_bench PRIVATE drawborad_render)

function(color_task id source)
  add_executable(test_${id} ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp ${PROJECT_SOURCE_DIR}/${source})
  target_include_directories(test_${id} PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_${id} PRIVATE drawborad_color drawborad_render)
  add_test(NAME ${id} COMMAND test_${id})
  set_tests_properties(${id} PROPERTIES TIMEOUT 60)
endfunction()
color_task(C01 tests/color/ProfileTests.cpp)
color_task(C02 tests/color/BlendOracleTests.cpp)
if(DRAWBORAD_LCMS2_INCLUDE AND DRAWBORAD_LCMS2_LIBRARY)
  add_executable(test_C02.LCMS
    ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp
    ${PROJECT_SOURCE_DIR}/tests/color/LcmsProofTests.cpp)
  target_include_directories(test_C02.LCMS PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/tests/support
    ${PROJECT_SOURCE_DIR})
  target_include_directories(test_C02.LCMS SYSTEM PRIVATE ${DRAWBORAD_LCMS2_INCLUDE})
  target_link_libraries(test_C02.LCMS PRIVATE drawborad_color drawborad_render ${DRAWBORAD_LCMS2_LIBRARY})
  add_test(NAME C02.LCMS COMMAND test_C02.LCMS)
  set_tests_properties(C02.LCMS PROPERTIES TIMEOUT 60)
endif()

function(feature_task id source)
  add_executable(test_${id} ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp ${PROJECT_SOURCE_DIR}/${source})
  target_include_directories(test_${id} PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_${id} PRIVATE drawborad_codecs drawborad_models drawborad_render drawborad_color drawborad_core)
  add_test(NAME ${id} COMMAND test_${id})
  set_tests_properties(${id} PROPERTIES TIMEOUT 60)
endfunction()
feature_task(C03 tests/codecs/ImageReadTests.cpp)
feature_task(C03.ProcessProtocol tests/codecs/CodecProcessTests.cpp)
add_test(
  NAME C03.ProcessWorker
  COMMAND ${Python3_EXECUTABLE}
          ${PROJECT_SOURCE_DIR}/tests/codecs/test_codec_worker.py
          ${CMAKE_BINARY_DIR})
set_tests_properties(C03.ProcessWorker PROPERTIES TIMEOUT 30)
feature_task(C04 tests/codecs/PsdReadTests.cpp)
feature_task(C05 tests/codecs/PsdWriteTests.cpp)
feature_task(C06 tests/codecs/ExportMatrixTests.cpp)
feature_task(C07 tests/codecs/FailureMatrixTests.cpp)
feature_task(C07.Fuzz tests/fuzz/CodecFuzz.cpp)
feature_task(M01 tests/models/PackageTests.cpp)
feature_task(M02 tests/models/ProviderParityTests.cpp)
feature_task(M03 tests/segmentation/ProposalTests.cpp)
feature_task(M04 tests/review/MaskEditTests.cpp)
feature_task(M05 tests/models/CustomAdapterTests.cpp)
feature_task(M06 tests/jobs/QueueTests.cpp)
add_executable(test_E01 ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp ${PROJECT_SOURCE_DIR}/tests/reconstruct/ProvenanceTests.cpp)
target_include_directories(test_E01 PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
target_link_libraries(test_E01 PRIVATE drawborad_reconstruct drawborad_render drawborad_models drawborad_core)
add_test(NAME E01 COMMAND test_E01)
set_tests_properties(E01 PROPERTIES TIMEOUT 60)
add_executable(test_E02 ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp ${PROJECT_SOURCE_DIR}/tests/reconstruct/PlanningTests.cpp)
target_include_directories(test_E02 PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
target_link_libraries(test_E02 PRIVATE drawborad_reconstruct drawborad_render drawborad_models drawborad_core)
add_test(NAME E02 COMMAND test_E02)
set_tests_properties(E02 PROPERTIES TIMEOUT 60)
add_executable(test_E03 ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp ${PROJECT_SOURCE_DIR}/tests/reconstruct/RefinementTests.cpp)
target_include_directories(test_E03 PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
target_link_libraries(test_E03 PRIVATE drawborad_reconstruct drawborad_render drawborad_models drawborad_core)
add_test(NAME E03 COMMAND test_E03)
set_tests_properties(E03 PROPERTIES TIMEOUT 60)
add_executable(test_E04 ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp ${PROJECT_SOURCE_DIR}/tests/reconstruct/TextLogoTests.cpp)
target_include_directories(test_E04 PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
target_link_libraries(test_E04 PRIVATE drawborad_critical drawborad_render drawborad_core)
add_test(NAME E04 COMMAND test_E04)
set_tests_properties(E04 PROPERTIES TIMEOUT 60)
add_executable(test_E05 ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp ${PROJECT_SOURCE_DIR}/tests/reconstruct/StyleTests.cpp)
target_include_directories(test_E05 PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
target_compile_definitions(test_E05 PRIVATE DRAWBORAD_STYLE_ROOT="${PROJECT_SOURCE_DIR}/resources/styles")
target_link_libraries(test_E05 PRIVATE drawborad_styles drawborad_render drawborad_core)
add_test(NAME E05 COMMAND test_E05)
set_tests_properties(E05 PROPERTIES TIMEOUT 60)

feature_task(X13.Stress tests/stress/ColorModelStressTests.cpp)

if(DRAWBORAD_HAS_ONNXRUNTIME AND DRAWBORAD_TEST_ONNX_MODEL)
  add_executable(test_M02.Runtime
    ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp
    ${PROJECT_SOURCE_DIR}/tests/models/OrtRuntimeTests.cpp)
  target_include_directories(test_M02.Runtime PRIVATE
    ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_M02.Runtime PRIVATE drawborad_models drawborad_core)
  add_test(NAME M02.Runtime COMMAND test_M02.Runtime)
  set_tests_properties(M02.Runtime PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "DRAWBORAD_TEST_ONNX_MODEL=${DRAWBORAD_TEST_ONNX_MODEL}")
  if(UNIX)
    set_tests_properties(M02.Runtime PROPERTIES
      ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:${DRAWBORAD_ONNXRUNTIME_ROOT}/lib")
  endif()
endif()

if(DRAWBORAD_HAS_ONNXRUNTIME AND DRAWBORAD_TEST_SAM_ENCODER AND DRAWBORAD_TEST_SAM_DECODER)
  add_executable(test_M02.SamRuntime
    ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp
    ${PROJECT_SOURCE_DIR}/tests/models/OrtSamRuntimeTests.cpp)
  target_include_directories(test_M02.SamRuntime PRIVATE
    ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_M02.SamRuntime PRIVATE drawborad_models drawborad_core)
  add_test(NAME M02.SamRuntime COMMAND test_M02.SamRuntime)
  set_tests_properties(M02.SamRuntime PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "DRAWBORAD_TEST_SAM_ENCODER=${DRAWBORAD_TEST_SAM_ENCODER};DRAWBORAD_TEST_SAM_DECODER=${DRAWBORAD_TEST_SAM_DECODER}")
  if(UNIX)
    set_tests_properties(M02.SamRuntime PROPERTIES
      ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:${DRAWBORAD_ONNXRUNTIME_ROOT}/lib")
  endif()
endif()

add_test(
  NAME M02.Tools
  COMMAND ${Python3_EXECUTABLE}
          ${PROJECT_SOURCE_DIR}/tests/models/test_model_tools.py
          ${PROJECT_SOURCE_DIR})
set_tests_properties(M02.Tools PROPERTIES TIMEOUT 30)

if(DRAWBORAD_HAS_ONNXRUNTIME AND DRAWBORAD_TEST_PROBE_ENCODER AND DRAWBORAD_TEST_PROBE_DECODER)
  add_executable(test_M05.Runtime
    ${PROJECT_SOURCE_DIR}/tests/support/TestMain.cpp
    ${PROJECT_SOURCE_DIR}/tests/models/CompatibilityRuntimeTests.cpp)
  target_include_directories(test_M05.Runtime PRIVATE
    ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_M05.Runtime PRIVATE drawborad_models drawborad_core)
  add_test(NAME M05.Runtime COMMAND test_M05.Runtime)
  set_tests_properties(M05.Runtime PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "DRAWBORAD_TEST_PROBE_ENCODER=${DRAWBORAD_TEST_PROBE_ENCODER};DRAWBORAD_TEST_PROBE_DECODER=${DRAWBORAD_TEST_PROBE_DECODER}")
  if(UNIX)
    set_tests_properties(M05.Runtime PROPERTIES
      ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:${DRAWBORAD_ONNXRUNTIME_ROOT}/lib")
  endif()
endif()
