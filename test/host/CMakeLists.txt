cmake_minimum_required(VERSION 3.16)
project(focusdesk_host_tests C)
enable_testing()

add_executable(test_smoke test_smoke.c)
add_test(NAME smoke COMMAND test_smoke)