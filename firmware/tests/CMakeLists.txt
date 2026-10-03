cmake_minimum_required(VERSION 3.20)
project(mai_pico_host_tests C)

enable_testing()
set(CMAKE_C_STANDARD 11)

add_executable(config_test config_test.c)
target_include_directories(config_test PRIVATE stubs ../src)
target_compile_options(config_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME config_test COMMAND config_test)

add_executable(button_test button_test.c)
target_include_directories(button_test PRIVATE stubs ../src)
target_compile_options(button_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME button_test COMMAND button_test)

add_executable(mpr121_test mpr121_test.c)
target_include_directories(mpr121_test PRIVATE stubs ../src)
target_compile_options(mpr121_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME mpr121_test COMMAND mpr121_test)

add_executable(touch_logic_test touch_logic_test.c)
target_include_directories(touch_logic_test PRIVATE ../src)
target_compile_options(touch_logic_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME touch_logic_test COMMAND touch_logic_test)

add_executable(save_test save_test.c)
target_include_directories(save_test PRIVATE stubs ../src)
target_compile_options(save_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME save_test COMMAND save_test)

add_executable(cfg_protocol_test cfg_protocol_test.c)
target_include_directories(cfg_protocol_test PRIVATE stubs ../src)
target_compile_options(cfg_protocol_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME cfg_protocol_test COMMAND cfg_protocol_test)

foreach(test hid_test touch_test io_test game_touch_state_test)
    add_executable(${test} ${test}.c)
    target_include_directories(${test} PRIVATE stubs ../src)
    target_compile_options(${test} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${test} COMMAND ${test})
endforeach()
