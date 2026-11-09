cmake_minimum_required(VERSION 3.16)
project(focusdesk_host_tests C)
enable_testing()

add_executable(test_smoke test_smoke.c)
add_test(NAME smoke COMMAND test_smoke)
add_executable(test_sitting_monitor
  test_sitting_monitor.c
  ../../components/sitting_monitor/sitting_monitor.c)
target_include_directories(test_sitting_monitor PRIVATE
  ../../components/focusdesk_common/include
  ../../components/sitting_monitor/include)
add_test(NAME sitting_monitor COMMAND test_sitting_monitor)
