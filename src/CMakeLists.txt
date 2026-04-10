cmake_minimum_required(VERSION 3.20)
project(RmCustomClient VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# 自动处理 MOC, UIC, RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 寻找 Qt6，包含 MQTT
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_package(Qt6 COMPONENTS Mqtt QUIET) # 假如没装 Qt Mqtt 模块，可通过条件编译或提示解决

# 寻找 Protobuf
find_package(Protobuf REQUIRED)

if(Qt6Mqtt_FOUND)
    message(STATUS "Qt6 Mqtt module found!")
else()
    message(WARNING "Qt6 Mqtt module NOT found! Please install qt6-mqtt-dev. MQTT features will be disabled or mocked.")
endif()

# 寻找 FFmpeg
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavutil
    libswscale
)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/model
    ${CMAKE_CURRENT_SOURCE_DIR}/network
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/video
    ${FFMPEG_INCLUDE_DIRS}
)

# protoc 命令生成 C++ 文件
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS model/messages.proto)

# 收集源码
set(SOURCES
    main.cpp
    model/RobotState.cpp
    network/MqttManager.cpp
    network/VideoReceiver.cpp
    ui/MainWindow.cpp
    video/VideoDecoder.cpp
    ${PROTO_SRCS}
)

set(HEADERS
    model/RobotState.h
    network/MqttManager.h
    network/VideoReceiver.h
    ui/MainWindow.h
    video/VideoDecoder.h
    ${PROTO_HDRS}
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 链接 Qt 和 FFmpeg 和 Protobuf
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    ${FFMPEG_LIBRARIES}
    protobuf::libprotobuf
    mosquitto
)

if(Qt6Mqtt_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Mqtt)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_QT_MQTT)
endif()

target_link_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARY_DIRS})
