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

add_test(NAME F01.Runner COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/build/test_task_runner.py)
