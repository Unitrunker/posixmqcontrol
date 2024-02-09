cmake_minimum_required(VERSION 3.1)

project(posixmqcontrol LANGUAGES C)
add_executable(posixmqcontrol posixmqcontrol.c)
target_include_directories(posixmqcontrol SYSTEM PUBLIC /usr/lib /usr/local/lib)
target_link_libraries(posixmqcontrol m rt)
add_custom_command(TARGET posixmqcontrol POST_BUILD
  COMMAND cp -f ${posixmqcontrol_SOURCE_DIR}/posixmqcontrol.1 ${PROJECT_BINARY_DIR} && gzip ${PROJECT_BINARY_DIR}/posixmqcontrol.1 )

install(FILES ${CMAKE_SOURCE_DIR}/posixmqcontrol.1.gz DESTINATION man/man1)
install(TARGETS posixmqcontrol DESTINATION bin)
