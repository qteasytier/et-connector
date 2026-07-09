cmake_minimum_required(VERSION 3.16)

# 设置CMake策略
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

# 项目名称
project(EasyTierConnector LANGUAGES CXX)

# 项目版本
set(PROJECT_VERSION "1.1.1")
set(ET_VERSION "2.6.4")

# 默认安装目录为项目根目录的Install文件夹，可由 -DCMAKE_INSTALL_PREFIX=... 覆盖
set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/Install" CACHE PATH "Installation directory")

# 编译产物输出到构建目录的 Output 子目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Output")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找Qt6包
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg)

# 源文件
set(SOURCES
    src/main.cpp
    src/SystemTray.cpp
    src/SettingsDialog.cpp
    src/AboutDialog.cpp
    src/ConfigManager.cpp
    src/IpcClient.cpp
    src/ConnectionController.cpp
)

# 头文件
set(HEADERS
    src/SystemTray.h
    src/SettingsDialog.h
    src/AboutDialog.h
    src/ConfigManager.h
    src/IpcClient.h
    src/ConnectionController.h
)

# 资源文件
set(RESOURCES
    assets/resources.qrc
)

# 创建可执行文件
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${RESOURCES})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES})
endif()

# 链接Qt库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    ET_VERSION="${ET_VERSION}"
    IS_COMMUNITY_VER
    FAVICON_SVG=":/favicon/favicon-open.svg"
    APP_DISPLAY_NAME="ET Connector"
)

# ============================================================================
# 后端守护进程构建与集成
# ============================================================================
# 查找 git（构建守护进程需要从仓库克隆源码）
find_program(GIT_EXECUTABLE git)

# 守护进程源码和构建产物路径
set(DAEMON_SRC_DIR "${CMAKE_BINARY_DIR}/qtet-daemon")
set(DAEMON_OUTPUT_DIR "${DAEMON_SRC_DIR}/build/Output")

# 守护进程可执行文件在不同平台上的名称
if(WIN32)
    set(DAEMON_BIN    "qtet-daemon.exe")
    set(DAEMON_RENAMED "qtet-connector-daemon.exe")
else()
    set(DAEMON_BIN    "qtet-daemon")
    set(DAEMON_RENAMED "qtet-connector-daemon")
endif()

if(GIT_EXECUTABLE)
    # 将守护进程构建加入默认 ALL 目标
    add_custom_target(qtet_daemon ALL
        COMMAND bash "${CMAKE_SOURCE_DIR}/assets/build_daemon.sh"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "构建 qtet-daemon 后端守护进程..."
    )
    # 主程序编译前必须先完成守护进程构建
    add_dependencies(${PROJECT_NAME} qtet_daemon)

    # 主程序链接完成后，将守护进程产物复制到输出目录
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND bash "${CMAKE_SOURCE_DIR}/assets/collect-daemon.sh"
            "${DAEMON_OUTPUT_DIR}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            "${DAEMON_BIN}"
            "${DAEMON_RENAMED}"
        COMMENT "将后端守护进程集成到输出目录..."
    )
else()
    message(WARNING "未找到 git，跳过守护进程构建。请安装 git 或手动将 qtet-daemon 放入输出目录。")
endif()

# Windows特定设置
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    # 生成 RC 文件引入 manifest 和图标
    set(APP_MANIFEST "${CMAKE_SOURCE_DIR}/app.manifest")
    set(APP_ICON "${CMAKE_SOURCE_DIR}/favicon/favicon-open.ico")
    set(APP_RC "${CMAKE_BINARY_DIR}/app.rc")

    # 检查图标文件是否存在
    if(NOT EXISTS ${APP_ICON})
        message(WARNING "图标文件 ${APP_ICON} 不存在，请将 favicon.svg 转换为 favicon.ico 格式")
    endif()

    # 自动生成 RC 文件
    file(WRITE ${APP_RC}
"// 自动生成的资源文件
#include <windows.h>

// 应用程序图标
IDI_ICON1   ICON    \"${APP_ICON}\"

// 应用程序清单
1   RT_MANIFEST   \"${APP_MANIFEST}\"
")

    # 将 RC 文件添加到源文件列表
    target_sources(${PROJECT_NAME} PRIVATE ${APP_RC})
endif()

# macOS Bundle 配置
if(APPLE)
    set(MACOS_ICON_FILE "favicon-open")
    set(MACOS_ICON_PATH "${CMAKE_SOURCE_DIR}/favicon/favicon-open.icns")

    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.easytier.connector"
        MACOSX_BUNDLE_BUNDLE_NAME "EasyTierConnector"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE "${MACOS_ICON_FILE}"
    )

    # 生成 Info.plist
    configure_file(
        "${CMAKE_SOURCE_DIR}/assets/Info.plist.in"
        "${CMAKE_BINARY_DIR}/Info.plist"
        @ONLY
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info.plist"
    )

    # Bundle 图标资源
    set_source_files_properties("${MACOS_ICON_PATH}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(${PROJECT_NAME} PRIVATE "${MACOS_ICON_PATH}")
endif()

# 安装规则
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# Windows 安装时使用 windeployqt 部署 Qt 运行时
if(WIN32)
    set(CMAKE_INSTALL_BINDIR "bin")

    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${QT_HOST_PATH}/bin ${QT6_HOST_PATH}/bin ${QT5_HOST_PATH}/bin)
    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE "
            message(STATUS \"使用windeployqt部署Qt运行时...\")
            get_filename_component(INSTALL_ABS_PATH \"\${CMAKE_INSTALL_PREFIX}\" ABSOLUTE)
            set(EXE_PATH \"\${INSTALL_ABS_PATH}/${CMAKE_INSTALL_BINDIR}/${PROJECT_NAME}.exe\")
            execute_process(
                COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                    --no-quick
                    --no-system-d3d-compiler
                    --no-translations
                    --no-opengl-sw
                    \"\${EXE_PATH}\"
                WORKING_DIRECTORY \"\${INSTALL_ABS_PATH}/${CMAKE_INSTALL_BINDIR}\"
                RESULT_VARIABLE deploy_result
            )
            if(NOT deploy_result EQUAL 0)
                message(WARNING \"windeployqt执行失败，返回码: \${deploy_result}\")
            endif()
        ")
    else()
        message(WARNING "未找到windeployqt工具，跳过Qt运行时部署")
    endif()
endif()

# macOS 安装时使用 macdeployqt 部署 Qt 框架到 Bundle
if(APPLE)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS ${QT_HOST_PATH}/bin ${QT6_HOST_PATH}/bin)
    if(MACDEPLOYQT_EXECUTABLE)
        install(CODE "
            message(STATUS \"使用 macdeployqt 部署 Qt 框架...\")
            get_filename_component(INSTALL_ABS_PATH \"\${CMAKE_INSTALL_PREFIX}\" ABSOLUTE)
            set(APP_PATH \"\${INSTALL_ABS_PATH}/${PROJECT_NAME}.app\")
            execute_process(
                COMMAND \"${MACDEPLOYQT_EXECUTABLE}\" \"\${APP_PATH}\" -always-overwrite
                WORKING_DIRECTORY \"\${INSTALL_ABS_PATH}\"
                RESULT_VARIABLE deploy_result
            )
            if(NOT deploy_result EQUAL 0)
                message(WARNING \"macdeployqt 执行失败，返回码: \${deploy_result}\")
            endif()
        ")
    else()
        message(WARNING "未找到 macdeployqt 工具，跳过 Qt 框架部署")
    endif()
endif()
