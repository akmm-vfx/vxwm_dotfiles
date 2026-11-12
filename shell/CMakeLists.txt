cmake_minimum_required(VERSION 3.16)
project(vxwm-shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS DBus Gui Qml Quick)
find_package(X11 REQUIRED)

add_executable(screenshot-tool screenshot-tool.cpp)
target_link_libraries(screenshot-tool PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick X11::X11)
set_target_properties(screenshot-tool PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_executable(vxwm-shell main.cpp)
target_link_libraries(vxwm-shell PRIVATE Qt6::DBus Qt6::Gui Qt6::Qml Qt6::Quick X11::X11)
set_target_properties(vxwm-shell PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
