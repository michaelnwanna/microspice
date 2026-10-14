include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(nlohmann_json
  URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3
  URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

add_executable(microspice_web_bridge bridge.cpp)
target_link_libraries(microspice_web_bridge PRIVATE microspice::script nlohmann_json::nlohmann_json)
ms_set_warnings(microspice_web_bridge)

if(MS_BUILD_TESTING)
  find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
  add_test(NAME microspice_web_tests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_server.py" "$<TARGET_FILE:microspice_web_bridge>")
  set_tests_properties(microspice_web_tests PROPERTIES TIMEOUT 90)
  if(MS_BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    add_executable(microspice_web_project_parity_test project_parity_test.cpp)
    target_link_libraries(microspice_web_project_parity_test PRIVATE
      microspice::script nlohmann_json::nlohmann_json Qt6::Widgets)
    ms_set_warnings(microspice_web_project_parity_test)
    add_test(NAME microspice_web_project_parity_test
      COMMAND microspice_web_project_parity_test "${PROJECT_SOURCE_DIR}/examples")
  endif()
endif()