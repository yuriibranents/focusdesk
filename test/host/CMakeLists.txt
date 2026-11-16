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


add_executable(test_fd_heartbeat
  test_fd_heartbeat.c
  ../../components/event_bus/fd_heartbeat.c)
target_include_directories(test_fd_heartbeat PRIVATE
  ../../components/focusdesk_common/include
  ../../components/event_bus/include)
add_test(NAME fd_heartbeat COMMAND test_fd_heartbeat)

add_executable(test_event_bus
  test_event_bus.c
  ../../components/event_bus/event_bus.c)
target_include_directories(test_event_bus PRIVATE
  fake
  ../../components/focusdesk_common/include
  ../../components/event_bus/include)
add_test(NAME event_bus COMMAND test_event_bus)

add_executable(test_focus_session
  test_focus_session.c
  ../../components/focus_session/focus_session.c)
target_include_directories(test_focus_session PRIVATE
  ../../components/focusdesk_common/include
  ../../components/focus_session/include)
add_test(NAME focus_session COMMAND test_focus_session)

