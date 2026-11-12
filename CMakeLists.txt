cmake_minimum_required(VERSION 3.25)
project(Drawborad VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(DRAWBORAD_DESKTOP "Build Qt native desktop shell" OFF)
option(DRAWBORAD_HEADLESS_WORKFLOW "Build Linux headless C++ workflow and CLI" OFF)
option(DRAWBORAD_QUALIFY "Require qualified Windows dependency manifest" OFF)
option(DRAWBORAD_WINDOWS_VALIDATION "Allow local Windows validation before release dependency qualification" OFF)
option(DRAWBORAD_SANITIZE "Enable address/undefined sanitizers" OFF)
include(cmake/ReleaseValidation.cmake)
drawborad_release_inventory(preflight)
include(cmake/Dependencies.cmake)
if(MSVC)
  add_compile_options(/W4 /WX /permissive- /fp:strict /utf-8)
  if(DRAWBORAD_SANITIZE)
    foreach(flag_var CMAKE_CXX_FLAGS_DEBUG CMAKE_EXE_LINKER_FLAGS_DEBUG)
      string(REGEX REPLACE "/RTC[1su]+|/ZI|/INCREMENTAL(:YES)?" "" ${flag_var} "${${flag_var}}")
    endforeach()
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO)
  endif()
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off)
  if(DRAWBORAD_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()
add_library(drawborad_core STATIC src/core/DocumentSpec.cpp src/core/Document.cpp src/core/LineartRaster.cpp src/core/CommandBus.cpp src/core/Digest.cpp src/storage/FileIO.cpp src/storage/TileStore.cpp src/storage/ProjectCodec.cpp src/storage/ProjectStore.cpp src/storage/Journal.cpp src/jobs/Protocol.cpp src/jobs/ResourceGovernor.cpp src/jobs/JobBroker.cpp src/jobs/WinWorkerHost.cpp src/platform/WinDiagnostics.cpp apps/studio/ApplicationController.cpp apps/studio/AlphaImportPolicy.cpp)
target_include_directories(drawborad_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
add_library(drawborad_color STATIC
  src/color/ProfileStore.cpp src/color/ColorService.cpp src/color/BlendPolicy.cpp src/color/ProofTransform.cpp)
target_include_directories(drawborad_color PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(drawborad_color PUBLIC drawborad_core)
find_path(DRAWBORAD_LCMS2_INCLUDE lcms2.h)
find_library(DRAWBORAD_LCMS2_LIBRARY NAMES lcms2)
if(DRAWBORAD_LCMS2_INCLUDE AND DRAWBORAD_LCMS2_LIBRARY)
  target_include_directories(drawborad_color SYSTEM PRIVATE ${DRAWBORAD_LCMS2_INCLUDE})
  target_link_libraries(drawborad_color PRIVATE ${DRAWBORAD_LCMS2_LIBRARY})
  target_compile_definitions(drawborad_color PRIVATE DRAWBORAD_HAS_LCMS2=1)
endif()

add_library(drawborad_codecs STATIC
  src/codecs/ImageReader.cpp
  src/codecs/CodecProcessProtocol.cpp
  src/codecs/HeicAdapter.cpp
  src/codecs/RasterReaders.cpp
  src/codecs/psd/PsdReader.cpp
  src/codecs/psd/PsdWriter.cpp
  src/codecs/ExportService.cpp
  src/codecs/RasterWriters.cpp
  src/codecs/LayerArchive.cpp
  src/codecs/AtomicExport.cpp)
target_include_directories(drawborad_codecs PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(drawborad_codecs PUBLIC drawborad_core drawborad_color)

find_package(PNG QUIET)
if(PNG_FOUND)
  target_link_libraries(drawborad_codecs PRIVATE PNG::PNG)
  target_compile_definitions(drawborad_codecs PRIVATE DRAWBORAD_HAS_PNG=1)
endif()

find_package(ZLIB QUIET)
if(ZLIB_FOUND)
  target_link_libraries(drawborad_codecs PRIVATE ZLIB::ZLIB)
  target_compile_definitions(drawborad_codecs PRIVATE DRAWBORAD_HAS_ZLIB=1)
endif()

find_package(JPEG QUIET)
if(JPEG_FOUND)
  target_link_libraries(drawborad_codecs PRIVATE JPEG::JPEG)
  target_compile_definitions(drawborad_codecs PRIVATE DRAWBORAD_HAS_JPEG=1)
endif()

find_package(TIFF QUIET)
if(TIFF_FOUND)
  target_link_libraries(drawborad_codecs PRIVATE TIFF::TIFF)
  target_compile_definitions(drawborad_codecs PRIVATE DRAWBORAD_HAS_TIFF=1)
endif()

find_path(DRAWBORAD_WEBP_INCLUDE webp/encode.h)
find_library(DRAWBORAD_WEBP_LIBRARY NAMES webp)
find_library(DRAWBORAD_WEBPMUX_LIBRARY NAMES webpmux)
find_path(DRAWBORAD_HEIF_INCLUDE libheif/heif.h)
find_library(DRAWBORAD_HEIF_LIBRARY NAMES heif)
if(DRAWBORAD_HEIF_INCLUDE AND DRAWBORAD_HEIF_LIBRARY)
  target_include_directories(drawborad_codecs SYSTEM PRIVATE ${DRAWBORAD_HEIF_INCLUDE})
  target_link_libraries(drawborad_codecs PRIVATE ${DRAWBORAD_HEIF_LIBRARY})
  target_compile_definitions(drawborad_codecs PRIVATE DRAWBORAD_HAS_HEIF=1)
endif()

if(DRAWBORAD_WEBP_INCLUDE AND DRAWBORAD_WEBP_LIBRARY)
  target_include_directories(drawborad_codecs SYSTEM PRIVATE ${DRAWBORAD_WEBP_INCLUDE})
  target_link_libraries(drawborad_codecs PRIVATE ${DRAWBORAD_WEBP_LIBRARY})
  target_compile_definitions(drawborad_codecs PRIVATE DRAWBORAD_HAS_WEBP=1)
  if(DRAWBORAD_WEBPMUX_LIBRARY)
    target_link_libraries(drawborad_codecs PRIVATE ${DRAWBORAD_WEBPMUX_LIBRARY})
    target_compile_definitions(drawborad_codecs PRIVATE DRAWBORAD_HAS_WEBPMUX=1)
  endif()
endif()

add_library(drawborad_models STATIC
  src/models/PackageVerifier.cpp
  src/models/ModelRegistry.cpp
  src/models/OrtSession.cpp
  src/models/AdapterRegistry.cpp
  src/segmentation/ProposalEngine.cpp
  src/review/ReviewSession.cpp
  src/jobs/ImageQueue.cpp
  src/jobs/QueueStore.cpp)
target_include_directories(drawborad_models PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(drawborad_models PUBLIC drawborad_core drawborad_color)
set(DRAWBORAD_TEST_ONNX_MODEL "" CACHE FILEPATH "Developer-only ONNX Runtime smoke model")
set(DRAWBORAD_TEST_SAM_ENCODER "" CACHE FILEPATH "Developer-only SAM encoder smoke model")
set(DRAWBORAD_TEST_SAM_DECODER "" CACHE FILEPATH "Developer-only SAM decoder smoke model")
set(DRAWBORAD_HAS_ONNXRUNTIME FALSE)
# A changed root must not reuse find results from a previously configured SDK.
unset(DRAWBORAD_ORT_INCLUDE CACHE)
unset(DRAWBORAD_ORT_LIBRARY CACHE)
unset(DRAWBORAD_ORT_RUNTIME CACHE)
if(DRAWBORAD_ONNXRUNTIME_ROOT)
  find_path(DRAWBORAD_ORT_INCLUDE onnxruntime_cxx_api.h
    PATHS "${DRAWBORAD_ONNXRUNTIME_ROOT}/include" NO_DEFAULT_PATH)
  find_library(DRAWBORAD_ORT_LIBRARY NAMES onnxruntime
    PATHS "${DRAWBORAD_ONNXRUNTIME_ROOT}/lib" NO_DEFAULT_PATH)
  if(NOT DRAWBORAD_ORT_INCLUDE OR NOT DRAWBORAD_ORT_LIBRARY)
    message(FATAL_ERROR "DRAWBORAD_ONNXRUNTIME_ROOT does not contain include/onnxruntime_cxx_api.h and lib/onnxruntime")
  endif()
  if(WIN32)
    find_file(DRAWBORAD_ORT_RUNTIME onnxruntime.dll
      PATHS "${DRAWBORAD_ONNXRUNTIME_ROOT}/lib" NO_DEFAULT_PATH)
    if(NOT DRAWBORAD_ORT_RUNTIME OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
      message(FATAL_ERROR "DRAWBORAD_ONNXRUNTIME_ROOT requires lib/onnxruntime.dll and an x64 target")
    endif()
  endif()
  target_include_directories(drawborad_models SYSTEM PRIVATE "${DRAWBORAD_ORT_INCLUDE}")
  target_link_libraries(drawborad_models PRIVATE "${DRAWBORAD_ORT_LIBRARY}")
  target_compile_definitions(drawborad_models PRIVATE DRAWBORAD_HAS_ONNXRUNTIME=1)
  set(DRAWBORAD_HAS_ONNXRUNTIME TRUE)
endif()
message(STATUS "Drawborad CPU inference runtime compiled: ${DRAWBORAD_HAS_ONNXRUNTIME}; model Ready requires executed preflight")

add_library(drawborad_reconstruct STATIC
  src/reconstruct/ReconstructionService.cpp src/reconstruct/EventWriter.cpp
  src/reconstruct/ContourPlanner.cpp src/reconstruct/RegionPlanner.cpp src/reconstruct/StrokeScheduler.cpp
  src/reconstruct/PigmentRefiner.cpp src/reconstruct/TexturePlanner.cpp src/reconstruct/RegionBudget.cpp)
target_include_directories(drawborad_reconstruct PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(drawborad_reconstruct PUBLIC drawborad_models drawborad_color drawborad_core)

add_library(drawborad_render STATIC
  src/render/ReferenceRenderer.cpp src/render/CoverageRasterizer.cpp src/render/PixelBlend.cpp
  src/render/TileCache.cpp src/render/DisplayCache.cpp src/render/CacheEvictor.cpp
  src/brush/BrushEngine.cpp src/brush/SampleResampler.cpp src/brush/FloodFill.cpp src/brush/BucketFill.cpp src/platform/TabletInput.cpp
  src/ui/MergedDocument.cpp
  src/history/EventIndex.cpp src/history/ReplayEngine.cpp
  src/view/Viewport.cpp src/view/HitTest.cpp src/platform/DeviceRecovery.cpp)
target_include_directories(drawborad_render PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(drawborad_render PUBLIC drawborad_core)
add_library(drawborad_quality STATIC src/quality/QualityEvaluator.cpp)
target_include_directories(drawborad_quality PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(drawborad_quality PUBLIC drawborad_core)
add_executable(drawborad_quality_runner tools/qa/quality_runner.cpp)
target_link_libraries(drawborad_quality_runner PRIVATE drawborad_quality)
add_library(drawborad_styles STATIC src/reconstruct/StyleRegistry.cpp)
target_include_directories(drawborad_styles PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(drawborad_styles PUBLIC drawborad_render drawborad_core)
add_library(drawborad_critical STATIC src/reconstruct/CriticalRegions.cpp src/text/TextCandidateService.cpp)
target_include_directories(drawborad_critical PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(drawborad_critical PUBLIC drawborad_render drawborad_core)
target_link_libraries(drawborad_reconstruct PUBLIC drawborad_render)
if(WIN32)
  target_compile_definitions(drawborad_core PUBLIC _WIN32_WINNT=0x0A00)
  target_link_libraries(drawborad_core PUBLIC advapi32 bcrypt dxgi shell32)
endif()
add_executable(DrawboradWorker apps/worker/Main.cpp)
target_link_libraries(DrawboradWorker PRIVATE drawborad_core)
add_executable(DrawboradCodec apps/codec/Main.cpp)
target_link_libraries(DrawboradCodec PRIVATE drawborad_codecs)
if(DRAWBORAD_HEADLESS_WORKFLOW)
  if(WIN32 OR DRAWBORAD_DESKTOP)
    message(FATAL_ERROR "DRAWBORAD_HEADLESS_WORKFLOW is the Linux service target; do not combine it with the Windows desktop shell")
  endif()
  find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui)
  target_link_libraries(drawborad_codecs PUBLIC Qt6::Core Qt6::Gui)
  target_link_libraries(drawborad_models PUBLIC Qt6::Core Qt6::Gui)
endif()
if(DRAWBORAD_DESKTOP)
  qt_standard_project_setup(REQUIRES 6.12)
endif()
if(DRAWBORAD_DESKTOP OR DRAWBORAD_HEADLESS_WORKFLOW)
  add_library(drawborad_workflow STATIC src/ui/ReviewJob.cpp src/ui/ReconstructionJob.cpp src/ui/PaintedDocument.cpp src/ui/SemanticManifest.cpp)
  target_include_directories(drawborad_workflow PUBLIC ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(drawborad_workflow PUBLIC drawborad_codecs drawborad_models drawborad_reconstruct Qt6::Core Qt6::Gui)
  execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE DRAWBORAD_GIT_SHA OUTPUT_STRIP_TRAILING_WHITESPACE)
  target_compile_definitions(drawborad_workflow PUBLIC DRAWBORAD_GIT_SHA="${DRAWBORAD_GIT_SHA}")
  target_link_libraries(DrawboradWorker PRIVATE drawborad_workflow)
  target_compile_definitions(DrawboradWorker PRIVATE DRAWBORAD_HAS_REVIEW_WORKER)
  target_compile_definitions(drawborad_core PRIVATE DRAWBORAD_HAS_REVIEW_WORKER)
endif()
if(DRAWBORAD_HEADLESS_WORKFLOW)
  add_executable(DrawboradHeadless apps/headless/Main.cpp src/headless/HeadlessWorkflow.cpp src/headless/ProjectSession.cpp)
  target_include_directories(DrawboradHeadless PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(DrawboradHeadless PRIVATE drawborad_workflow drawborad_core drawborad_render drawborad_codecs Qt6::Core Qt6::Gui)
endif()
if(DRAWBORAD_DESKTOP)
  qt_add_executable(Drawborad apps/studio/Main.cpp apps/studio/QtBridge.cpp apps/studio/QtBridge.h src/ui/WorkflowController.cpp src/ui/WorkflowController.h src/platform/CanvasItem.cpp src/platform/CanvasItem.h src/platform/CanvasRenderer.cpp)
  # The SAM3 runner is an optional locally executed model backend. No weights
  # are bundled; the application requires an explicitly selected checkpoint.
  add_custom_command(TARGET Drawborad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tools/models/sam3_semantic.py"
            "$<TARGET_FILE_DIR:Drawborad>/sam3_semantic.py")
  target_compile_definitions(Drawborad PRIVATE DRAWBORAD_HAS_QT)
  target_include_directories(Drawborad PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(Drawborad PRIVATE drawborad_workflow drawborad_core drawborad_render Qt6::Quick Qt6::QuickControls2 d3d11 dxgi)
  set_source_files_properties(ui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
  qt_add_qml_module(Drawborad URI Drawborad VERSION 1.0 QML_FILES ui/Main.qml)
  string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef DRAWBORAD_BUILD_TOKEN)
  # Changing the freshness token must also rebuild the executable on every
  # generator, including an otherwise up-to-date Visual Studio cache.
  target_compile_definitions(Drawborad PRIVATE DRAWBORAD_BUILD_TOKEN="${DRAWBORAD_BUILD_TOKEN}")
  file(WRITE "${CMAKE_BINARY_DIR}/drawborad_expected_build_token.txt" "${DRAWBORAD_BUILD_TOKEN}\n")
  add_custom_command(
    TARGET Drawborad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/drawborad_expected_build_token.txt"
            "$<TARGET_FILE_DIR:Drawborad>/drawborad_build_token.txt")
else()
  add_executable(DrawboradDiagnostics apps/studio/Main.cpp)
  target_link_libraries(DrawboradDiagnostics PRIVATE drawborad_core)
endif()
enable_testing()
add_subdirectory(tests)
if(WIN32 AND DRAWBORAD_HAS_ONNXRUNTIME)
  # One copy job owns each output directory; parallel test links cannot race
  # while copying the same DLL. App, Worker and tests use this same SDK file.
  add_custom_target(drawborad_ort_runtime ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DrawboradWorker>" "$<TARGET_FILE_DIR:test_M01>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DRAWBORAD_ORT_RUNTIME}" "$<TARGET_FILE_DIR:DrawboradWorker>/onnxruntime.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DRAWBORAD_ORT_RUNTIME}" "$<TARGET_FILE_DIR:test_M01>/onnxruntime.dll")
  if(EXISTS "${DRAWBORAD_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll")
    add_custom_command(TARGET drawborad_ort_runtime POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DRAWBORAD_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll" "$<TARGET_FILE_DIR:DrawboradWorker>/onnxruntime_providers_shared.dll"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DRAWBORAD_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll" "$<TARGET_FILE_DIR:test_M01>/onnxruntime_providers_shared.dll")
  endif()
  get_property(root_targets DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(test_targets DIRECTORY "${PROJECT_SOURCE_DIR}/tests" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(runtime_target IN LISTS root_targets test_targets)
    get_target_property(kind "${runtime_target}" TYPE)
    if(kind STREQUAL "EXECUTABLE")
      add_dependencies("${runtime_target}" drawborad_ort_runtime)
    endif()
  endforeach()
endif()
