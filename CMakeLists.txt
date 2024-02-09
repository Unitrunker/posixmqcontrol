cmake_minimum_required(VERSION 3.1)

project(posixmqcontrol LANGUAGES C)
add_executable(posixmqcontrol posixmqcontrol.c)
target_include_directories(posixmqcontrol SYSTEM PUBLIC /usr/lib /usr/local/lib)
target_link_libraries(posixmqcontrol m rt)

install(TARGETS posixmqcontrol DESTINATION bin)
