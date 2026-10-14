find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

add_executable(microspice_gui main.cpp)
target_link_libraries(microspice_gui PRIVATE microspice::core Qt6::Widgets Threads::Threads)
ms_set_warnings(microspice_gui)
target_compile_definitions(microspice_gui PRIVATE MS_APP_VERSION="${PROJECT_VERSION}")

if(APPLE)
  add_executable(microspice_icon_generator EXCLUDE_FROM_ALL icon_generator.cpp)
  target_link_libraries(microspice_icon_generator PRIVATE Qt6::Gui)
  ms_set_warnings(microspice_icon_generator)
  set(ms_icon "${CMAKE_CURRENT_BINARY_DIR}/microspice.icns")
  add_custom_command(OUTPUT "${ms_icon}"
    COMMAND microspice_icon_generator "${CMAKE_CURRENT_BINARY_DIR}/microspice.iconset"
    COMMAND /usr/bin/iconutil -c icns "${CMAKE_CURRENT_BINARY_DIR}/microspice.iconset" -o "${ms_icon}"
    DEPENDS microspice_icon_generator
    VERBATIM)
  target_sources(microspice_gui PRIVATE "${ms_icon}")
  set_source_files_properties("${ms_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(microspice_gui PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "microspice"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.michaelnwanna.microspice"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_STRING "microspice ${PROJECT_VERSION}"
    INSTALL_RPATH "@executable_path/../Frameworks"
    MACOSX_BUNDLE_ICON_FILE "microspice.icns"
  )
  get_target_property(ms_qmake Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(ms_qt_bin "${ms_qmake}" DIRECTORY)
  get_filename_component(ms_qt_prefix "${ms_qt_bin}" DIRECTORY)
  find_program(MS_MACDEPLOYQT macdeployqt HINTS "${ms_qt_bin}" REQUIRED)
  install(TARGETS microspice_gui BUNDLE DESTINATION . COMPONENT Desktop)
  install(FILES "${PROJECT_SOURCE_DIR}/examples/voltage_divider.microspice"
    "${PROJECT_SOURCE_DIR}/examples/rc_charging.microspice"
    DESTINATION microspice_gui.app/Contents/Resources/examples COMPONENT Desktop)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/deploy_macos.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/deploy_macos.cmake" @ONLY)
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploy_macos.cmake" COMPONENT Desktop)
  set(CPACK_GENERATOR ZIP)
  set(CPACK_PACKAGE_NAME microspice)
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME "microspice-${PROJECT_VERSION}-macOS-${CMAKE_SYSTEM_PROCESSOR}")
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
  include(CPack)
endif()

if(MS_BUILD_TESTING)
  add_executable(microspice_background_job_test background_job_test.cpp)
  target_link_libraries(microspice_background_job_test PRIVATE microspice::core Qt6::Widgets Threads::Threads)
  ms_set_warnings(microspice_background_job_test)
  add_test(NAME microspice_background_job_test COMMAND microspice_background_job_test)
  add_executable(microspice_inspector_test inspector_test.cpp)
  target_link_libraries(microspice_inspector_test PRIVATE microspice::core Qt6::Widgets)
  ms_set_warnings(microspice_inspector_test)
  add_test(NAME microspice_inspector_test COMMAND microspice_inspector_test)
  set_tests_properties(microspice_inspector_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(microspice_analysis_panel_test analysis_panel_test.cpp)
  target_link_libraries(microspice_analysis_panel_test PRIVATE microspice::core Qt6::Widgets Threads::Threads)
  ms_set_warnings(microspice_analysis_panel_test)
  add_test(NAME microspice_analysis_panel_test COMMAND microspice_analysis_panel_test
    "${PROJECT_SOURCE_DIR}/examples/rc_charging.microspice")
  set_tests_properties(microspice_analysis_panel_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(microspice_analysis_test analysis_test.cpp)
  target_link_libraries(microspice_analysis_test PRIVATE microspice::core Qt6::Widgets)
  ms_set_warnings(microspice_analysis_test)
  add_test(NAME microspice_analysis_test COMMAND microspice_analysis_test)
  add_executable(microspice_project_test project_test.cpp)
  target_link_libraries(microspice_project_test PRIVATE microspice::core Qt6::Widgets)
  ms_set_warnings(microspice_project_test)
  add_test(NAME microspice_project_test COMMAND microspice_project_test
    "${PROJECT_SOURCE_DIR}/examples/voltage_divider.microspice")
  set_tests_properties(microspice_project_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_test(NAME microspice_window_test COMMAND microspice_gui --smoke-test)
  set_tests_properties(microspice_window_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(microspice_connectivity_test connectivity_test.cpp)
  target_link_libraries(microspice_connectivity_test PRIVATE microspice::core Qt6::Widgets)
  ms_set_warnings(microspice_connectivity_test)
  add_test(NAME microspice_connectivity_test COMMAND microspice_connectivity_test)
  add_executable(microspice_gui_test schematic_test.cpp)
  target_link_libraries(microspice_gui_test PRIVATE microspice::core Qt6::Widgets)
  ms_set_warnings(microspice_gui_test)
  add_test(NAME microspice_gui_test COMMAND microspice_gui_test)
  set_tests_properties(microspice_gui_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()