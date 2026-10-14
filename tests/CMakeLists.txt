add_executable(microspice_tests microspice_test.cpp)
target_link_libraries(microspice_tests PRIVATE microspice::core)
ms_set_warnings(microspice_tests)

add_test(NAME microspice_tests COMMAND microspice_tests)

add_executable(microspice_script_tests script_test.cpp)
target_link_libraries(microspice_script_tests PRIVATE microspice::script)
ms_set_warnings(microspice_script_tests)
target_compile_definitions(microspice_script_tests PRIVATE
	MS_SCRIPT_EXAMPLES="${PROJECT_SOURCE_DIR}/examples/scripts")
add_test(NAME microspice_script_compiler_tests COMMAND microspice_script_tests compiler)
add_test(NAME microspice_script_runtime_tests COMMAND microspice_script_tests runtime)
add_test(NAME microspice_script_cli_tests COMMAND "${CMAKE_COMMAND}"
	"-DCLI=$<TARGET_FILE:microspice_script>"
	"-DEXAMPLES=${PROJECT_SOURCE_DIR}/examples/scripts"
	"-DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/scripts"
	-P "${CMAKE_CURRENT_SOURCE_DIR}/script_cli_test.cmake")

if(UNIX)
	add_executable(microspice_script_signal_test script_signal_test.cpp)
	ms_set_warnings(microspice_script_signal_test)
	add_dependencies(microspice_script_signal_test microspice_script)
	add_test(NAME microspice_script_signal_test COMMAND microspice_script_signal_test
		"$<TARGET_FILE:microspice_script>" "${CMAKE_CURRENT_BINARY_DIR}/script_cancel_fixture.msim")
	set_tests_properties(microspice_script_signal_test PROPERTIES TIMEOUT 20)
endif()

foreach(example resistor_load current_driven_resistor voltage_divider rc_charging diode_bias subcircuit_divider)
	add_test(NAME script_${example}
		COMMAND microspice_script run "${PROJECT_SOURCE_DIR}/examples/scripts/${example}.msim")
endforeach()