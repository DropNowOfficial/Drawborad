cmake_minimum_required(VERSION 3.25)
project(Drawborad VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(DRAWBORAD_DESKTOP "Build Qt native desktop shell" OFF)
option(DRAWBORAD_QUALIFY "Require qualified Windows dependency manifest" OFF)
option(DRAWBORAD_WINDOWS_VALIDATION "Allow local Windows validation before release dependency qualification" OFF)
option(DRAWBORAD_SANITIZE "Enable address/undefined sanitizers" OFF)
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
add_library(drawborad_core STATIC src/core/DocumentSpec.cpp src/core/Document.cpp src/core/CommandBus.cpp src/core/Digest.cpp src/storage/FileIO.cpp src/storage/TileStore.cpp src/storage/ProjectCodec.cpp src/storage/ProjectStore.cpp src/storage/Journal.cpp src/jobs/Protocol.cpp src/jobs/ResourceGovernor.cpp src/jobs/JobBroker.cpp src/jobs/WinWorkerHost.cpp src/platform/WinDiagnostics.cpp apps/studio/ApplicationController.cpp)
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
set(DRAWBORAD_ONNXRUNTIME_ROOT "" CACHE PATH "Offline ONNX Runtime SDK root")
set(DRAWBORAD_TEST_ONNX_MODEL "" CACHE FILEPATH "Developer-only ONNX Runtime smoke model")
set(DRAWBORAD_TEST_SAM_ENCODER "" CACHE FILEPATH "Developer-only SAM encoder smoke model")
set(DRAWBORAD_TEST_SAM_DECODER "" CACHE FILEPATH "Developer-only SAM decoder smoke model")
if(DRAWBORAD_ONNXRUNTIME_ROOT)
  find_path(DRAWBORAD_ORT_INCLUDE onnxruntime_cxx_api.h
    PATHS "${DRAWBORAD_ONNXRUNTIME_ROOT}/include" NO_DEFAULT_PATH)
  find_library(DRAWBORAD_ORT_LIBRARY NAMES onnxruntime
    PATHS "${DRAWBORAD_ONNXRUNTIME_ROOT}/lib" NO_DEFAULT_PATH)
  if(NOT DRAWBORAD_ORT_INCLUDE OR NOT DRAWBORAD_ORT_LIBRARY)
    message(FATAL_ERROR "DRAWBORAD_ONNXRUNTIME_ROOT does not contain include/onnxruntime_cxx_api.h and lib/onnxruntime")
  endif()
  target_include_directories(drawborad_models SYSTEM PRIVATE "${DRAWBORAD_ORT_INCLUDE}")
  target_link_libraries(drawborad_models PRIVATE "${DRAWBORAD_ORT_LIBRARY}")
  target_compile_definitions(drawborad_models PRIVATE DRAWBORAD_HAS_ONNXRUNTIME=1)
  set(DRAWBORAD_HAS_ONNXRUNTIME TRUE)
endif()

add_library(drawborad_render STATIC
  src/render/ReferenceRenderer.cpp src/render/CoverageRasterizer.cpp src/render/PixelBlend.cpp
  src/render/TileCache.cpp src/render/DisplayCache.cpp src/render/CacheEvictor.cpp
  src/brush/BrushEngine.cpp src/brush/SampleResampler.cpp src/platform/TabletInput.cpp
  src/history/EventIndex.cpp src/history/ReplayEngine.cpp
  src/view/Viewport.cpp src/view/HitTest.cpp src/platform/DeviceRecovery.cpp)
target_include_directories(drawborad_render PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(drawborad_render PUBLIC drawborad_core)
if(WIN32)
  target_compile_definitions(drawborad_core PUBLIC _WIN32_WINNT=0x0A00)
  target_link_libraries(drawborad_core PUBLIC advapi32 bcrypt dxgi shell32)
endif()
add_executable(DrawboradWorker apps/worker/Main.cpp)
target_link_libraries(DrawboradWorker PRIVATE drawborad_core)
if(DRAWBORAD_DESKTOP)
  qt_standard_project_setup(REQUIRES 6.12)
  qt_add_executable(Drawborad apps/studio/Main.cpp apps/studio/QtBridge.cpp apps/studio/QtBridge.h src/platform/CanvasItem.cpp src/platform/CanvasItem.h src/platform/CanvasRenderer.cpp)
  target_compile_definitions(Drawborad PRIVATE DRAWBORAD_HAS_QT)
  target_include_directories(Drawborad PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(Drawborad PRIVATE drawborad_core drawborad_render Qt6::Quick Qt6::QuickControls2 d3d11 dxgi)
  set_source_files_properties(ui/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
  qt_add_qml_module(Drawborad URI Drawborad VERSION 1.0 QML_FILES ui/Main.qml)
  string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef DRAWBORAD_BUILD_TOKEN)
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
