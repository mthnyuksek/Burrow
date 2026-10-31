cmake_minimum_required(VERSION 3.20)
project(Burrow LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# UI independent logic: vaults, agents, runs, conversations. Plain C++17 plus the Win32 API.
add_library(burrow_core STATIC
    Source/Core/agent_definition.cpp
    Source/Core/claude_process.cpp
    Source/Core/conversation.cpp
    Source/Core/conversation_router.cpp
    Source/Core/id_util.cpp
    Source/Core/job_builder.cpp
    Source/Core/json.cpp
    Source/Core/markdown_file.cpp
    Source/Core/platform.cpp
    Source/Core/project_definition.cpp
    Source/Core/remote_config.cpp
    Source/Core/rule_definition.cpp
    Source/Core/run_manager.cpp
    Source/Core/run_record.cpp
    Source/Core/run_stats.cpp
    Source/Core/scheduler.cpp
    Source/Core/skill_definition.cpp
    Source/Core/skill_templates.cpp
    Source/Core/skill_templates_en.cpp
    Source/Core/task_definition.cpp
    Source/Core/telegram_config.cpp
    Source/Core/usage_info.cpp
    Source/Core/vault.cpp
    Source/Core/vault_registry.cpp
)
target_include_directories(burrow_core PUBLIC Source)

# The web page (Web folder) and the WebView2 loader DLL are compiled into the program as resources.
# Every file becomes an RCDATA entry whose identifier is its path in capitals with every
# non-alphanumeric character turned into "_" ("web/pages/chats.js" -> WEB_PAGES_CHATS_JS);
# embedded_web.cpp does the same conversion when it looks a file up.
file(GLOB_RECURSE BURROW_WEB_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Web/*")
set(BURROW_WEB_LOADER "${CMAKE_CURRENT_SOURCE_DIR}/Third_Party/Web_View2/Bin/WebView2Loader.dll")
set(BURROW_WEB_RC "${CMAKE_CURRENT_BINARY_DIR}/embedded_web.rc")
set(BURROW_WEB_RC_TEXT "LOADER_WEBVIEW2LOADER_DLL RCDATA \"${BURROW_WEB_LOADER}\"
")
foreach(web_file IN LISTS BURROW_WEB_FILES)
    file(RELATIVE_PATH web_name "${CMAKE_CURRENT_SOURCE_DIR}/Web" "${web_file}")
    string(MAKE_C_IDENTIFIER "web/${web_name}" web_id)
    string(TOUPPER "${web_id}" web_id)
    string(APPEND BURROW_WEB_RC_TEXT "${web_id} RCDATA \"${web_file}\"
")
endforeach()
file(GENERATE OUTPUT "${BURROW_WEB_RC}" CONTENT "${BURROW_WEB_RC_TEXT}")
set_source_files_properties("${BURROW_WEB_RC}" PROPERTIES GENERATED TRUE
    OBJECT_DEPENDS "${BURROW_WEB_FILES};${BURROW_WEB_LOADER}")

add_executable(burrow WIN32
    Source/Ui/burrow_app.cpp
    Source/Ui/embedded_web.cpp
    Source/Ui/folder_dialog.cpp
    Source/Ui/remote_server.cpp
    Source/Ui/telegram_client.cpp
    Source/Ui/tunnel.cpp
    Source/Ui/web_api.cpp
    Source/Ui/web_frame.cpp
    Source/Ui/web_view_host.cpp
    Resources/burrow.rc
    ${BURROW_WEB_RC}
)
target_include_directories(burrow PRIVATE Third_Party/Web_View2/Include)
target_compile_definitions(burrow PRIVATE UNICODE _UNICODE)
target_link_libraries(burrow PRIVATE burrow_core ole32 oleaut32 shlwapi shell32 uuid dwmapi winhttp ws2_32 bcrypt)
target_link_options(burrow PRIVATE -static)

# Self test of the core: cmake -DBURROW_BUILD_TESTS=ON, run `core_selftest [--live]`.
# --live also starts Claude Code a few times with tiny Haiku requests.
option(BURROW_BUILD_TESTS "Build the core self test" OFF)
if(BURROW_BUILD_TESTS)
    add_executable(core_selftest Tests/core_selftest.cpp)
    target_link_libraries(core_selftest PRIVATE burrow_core)
    target_link_options(core_selftest PRIVATE -static)
endif()
