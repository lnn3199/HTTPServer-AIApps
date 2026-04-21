cmake_minimum_required(VERSION 3.10)
project(HTTPServer-AIApps)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# Debian/Ubuntu: apt 安装的 OpenCV 4 常在此目录提供 OpenCVConfig.cmake（需安装 libopencv-dev）
if(NOT OpenCV_DIR)
    if(EXISTS "/usr/lib/x86_64-linux-gnu/cmake/opencv4/OpenCVConfig.cmake")
        set(OpenCV_DIR "/usr/lib/x86_64-linux-gnu/cmake/opencv4")
    elseif(EXISTS "/usr/lib/aarch64-linux-gnu/cmake/opencv4/OpenCVConfig.cmake")
        set(OpenCV_DIR "/usr/lib/aarch64-linux-gnu/cmake/opencv4")
    endif()
endif()
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui dnn videoio)

set(MYSQL_INCLUDES
    /usr/include/mysql-cppconn-8
    /usr/include/mysql
)

find_library(MYSQLCPPCONN_LIBRARY
    NAMES mysqlcppconn mysqlcppconn8
    PATHS /usr/lib /usr/lib64 /usr/local/lib
)

find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    PATHS /usr/lib /usr/lib64 /usr/local/lib
)

if(NOT ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR "ONNX Runtime not found (libonnxruntime). Install it or adjust search paths.")
endif()

# C++ API 头文件（官方 tgz 内 include/；勿只拷贝 lib 不拷贝 include）
find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    PATHS
        /usr/local/include
        /usr/include
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/include"
)
if(NOT ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    get_filename_component(_ORT_LIBDIR "${ONNXRUNTIME_LIBRARY}" DIRECTORY)
    get_filename_component(_ORT_PREFIX "${_ORT_LIBDIR}" DIRECTORY)
    if(EXISTS "${_ORT_PREFIX}/include/onnxruntime_cxx_api.h")
        set(ONNXRUNTIME_INCLUDE_DIR "${_ORT_PREFIX}/include")
    endif()
endif()
if(NOT ONNXRUNTIME_INCLUDE_DIR)
    message(FATAL_ERROR
        "ONNX Runtime C++ headers not found (onnxruntime_cxx_api.h).\n"
        "From the same onnxruntime-linux-x64-*.tgz you used for the .so, copy the whole 'include' folder to e.g.\n"
        "  /usr/local/include/   (so that /usr/local/include/onnxruntime_cxx_api.h exists)\n"
        "Or extract the tgz under third_party/onnxruntime/ (include/ next to lib/).")
endif()

# SimpleAmqpClient：优先使用 third_party 源码一并编译（免系统安装）；否则用 /usr/local 或 /usr
# 支持 zip 解压多一层：third_party/SimpleAmqpClient/SimpleAmqpClient-master/CMakeLists.txt
set(_SAC_STAGING "${CMAKE_SOURCE_DIR}/third_party/SimpleAmqpClient")
set(SIMPLEAMQPCLIENT_BUNDLED_DIR "")
if(EXISTS "${_SAC_STAGING}/CMakeLists.txt")
    set(SIMPLEAMQPCLIENT_BUNDLED_DIR "${_SAC_STAGING}")
elseif(EXISTS "${_SAC_STAGING}/SimpleAmqpClient-master/CMakeLists.txt")
    set(SIMPLEAMQPCLIENT_BUNDLED_DIR "${_SAC_STAGING}/SimpleAmqpClient-master")
elseif(EXISTS "${_SAC_STAGING}/SimpleAmqpClient-main/CMakeLists.txt")
    set(SIMPLEAMQPCLIENT_BUNDLED_DIR "${_SAC_STAGING}/SimpleAmqpClient-main")
endif()
if(SIMPLEAMQPCLIENT_BUNDLED_DIR)
    message(STATUS "SimpleAmqpClient: building from ${SIMPLEAMQPCLIENT_BUNDLED_DIR}")
    add_subdirectory("${SIMPLEAMQPCLIENT_BUNDLED_DIR}" "${CMAKE_BINARY_DIR}/third_party/SimpleAmqpClient-build")
    set(HTTPSERVER_SIMPLEAMQP_BUNDLED ON)
else()
    find_path(SIMPLEAMQPCLIENT_INCLUDE_DIR
        NAMES SimpleAmqpClient/SimpleAmqpClient.h
        PATHS /usr/local/include /usr/include
    )
    if(NOT SIMPLEAMQPCLIENT_INCLUDE_DIR)
        message(FATAL_ERROR
            "SimpleAmqpClient not found.\n"
            "  Option A: Put sources under third_party/SimpleAmqpClient/ — either CMakeLists.txt at that path,\n"
            "           or third_party/SimpleAmqpClient/SimpleAmqpClient-master/ (zip default). See third_party/README.txt\n"
            "  Option B: Build and install to /usr/local (cmake --install . --prefix /usr/local)")
    endif()
    find_library(SIMPLEAMQPCLIENT_LIBRARY
        NAMES SimpleAmqpClient
        PATHS /usr/local/lib /usr/lib /usr/lib64
    )
    if(NOT SIMPLEAMQPCLIENT_LIBRARY)
        message(FATAL_ERROR "SimpleAmqpClient library not found (libSimpleAmqpClient.so). Run ldconfig after install.")
    endif()
endif()

file(GLOB_RECURSE HTTP_SERVER_SRC "${PROJECT_SOURCE_DIR}/HttpServer/src/*.cpp")
file(GLOB_RECURSE CHAT_SRC "${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/*.cpp")

add_executable(http_server
    ${HTTP_SERVER_SRC}
    ${CHAT_SRC}
)

target_include_directories(http_server PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/HttpServer/include
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/include
    ${MYSQL_INCLUDES}
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_INCLUDE_DIR}
    /usr/local/include
)
if(HTTPSERVER_SIMPLEAMQP_BUNDLED)
    # 与 SimpleAmqpClient 工程内 include_directories(BEFORE src) 一致
    target_include_directories(http_server PRIVATE "${SIMPLEAMQPCLIENT_BUNDLED_DIR}/src")
else()
    target_include_directories(http_server PRIVATE ${SIMPLEAMQPCLIENT_INCLUDE_DIR})
endif()

target_link_directories(http_server PRIVATE /usr/local/lib)

target_link_libraries(http_server PRIVATE
    pthread
    muduo_net
    muduo_base
    mysqlcppconn
    mysqlclient
    ssl
    crypto
    ${CURL_LIBRARIES}
    ${OpenCV_LIBS}
    ${ONNXRUNTIME_LIBRARY}
    rabbitmq
)
if(HTTPSERVER_SIMPLEAMQP_BUNDLED)
    target_link_libraries(http_server PRIVATE SimpleAmqpClient)
else()
    target_link_libraries(http_server PRIVATE ${SIMPLEAMQPCLIENT_LIBRARY})
endif()

message(STATUS "HTTP Server sources: ${HTTP_SERVER_SRC}")
message(STATUS "Chat application sources: ${CHAT_SRC}")
