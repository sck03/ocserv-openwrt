add_library(client_model STATIC platform.cpp profile.cpp)
target_include_directories(client_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(client_model PUBLIC
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601
    CERT_CHAIN_PARA_HAS_EXTRA_FIELDS GNUTLS_STATIC LIBXML_STATIC)
target_compile_options(client_model PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(client_model INTERFACE -static -static-libgcc -static-libstdc++
    -Wl,--gc-sections -Wl,--dynamicbase -Wl,--nxcompat
    -Wl,--major-subsystem-version,6 -Wl,--minor-subsystem-version,1)
target_link_libraries(client_model PUBLIC ws2_32 winhttp crypt32 bcrypt advapi32 user32 shell32 ole32 gdi32 comctl32 comdlg32 normaliz)
add_library(client_session STATIC session.cpp certificates.cpp)
target_include_directories(client_session PUBLIC "${BRIDGE_DEPS}/include")
target_compile_options(client_session PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(client_session PUBLIC client_model
    -Wl,--start-group
    "${BRIDGE_DEPS}/lib/libopenconnect.a"
    "${BRIDGE_DEPS}/lib/libstoken.a"
    "${BRIDGE_DEPS}/lib/libgnutls.a"
    "${BRIDGE_DEPS}/lib/libxml2.a"
    "${BRIDGE_DEPS}/lib/libz.a"
    "${BRIDGE_DEPS}/lib/libhogweed.a"
    "${BRIDGE_DEPS}/lib/libnettle.a"
    "${BRIDGE_DEPS}/lib/libgmp.a"
    -Wl,--end-group ncrypt secur32 iphlpapi)
add_library(client_ui STATIC application.cpp ui.cpp dialogs.cpp)
target_compile_options(client_ui PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(client_ui PUBLIC client_session)
add_executable(BridgeVPN WIN32 main.cpp "${PROJECT_SOURCE_DIR}/resources/app.rc")
set_source_files_properties("${PROJECT_SOURCE_DIR}/resources/app.rc" PROPERTIES
    OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/resources/app.manifest;${PROJECT_SOURCE_DIR}/resources/app.ico")
target_include_directories(BridgeVPN PRIVATE "${PROJECT_SOURCE_DIR}/resources")
target_compile_options(BridgeVPN PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(BridgeVPN PRIVATE -municode)
target_link_libraries(BridgeVPN PRIVATE client_ui)
set_target_properties(BridgeVPN PROPERTIES OUTPUT_NAME "布利杰VPN" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_custom_command(TARGET BridgeVPN POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/.deps/sources/wintun/bin/${WINTUN_ARCH}/wintun.dll" "$<TARGET_FILE_DIR:BridgeVPN>/wintun.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vpnc-script-win.js" "$<TARGET_FILE_DIR:BridgeVPN>/vpnc-script-win.js")
if(BUILD_TESTING)
    add_executable(native_model_tests tests/profile_tests.cpp)
    target_link_libraries(native_model_tests PRIVATE client_model)
    set_target_properties(native_model_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME native_profiles COMMAND native_model_tests "${CMAKE_BINARY_DIR}/fixtures")
    add_executable(native_session_tests tests/session_tests.cpp)
    target_link_libraries(native_session_tests PRIVATE client_session)
    target_link_options(native_session_tests PRIVATE -municode)
    set_target_properties(native_session_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_executable(native_script_tests tests/script_runner_tests.cpp)
    target_link_libraries(native_script_tests PRIVATE client_session)
    target_link_options(native_script_tests PRIVATE -municode)
    set_target_properties(native_script_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME native_scripts COMMAND native_script_tests "${CMAKE_BINARY_DIR}/script-fixtures")
    add_executable(native_ui_tests tests/ui_tests.cpp "${PROJECT_SOURCE_DIR}/resources/app.rc")
    target_include_directories(native_ui_tests PRIVATE "${PROJECT_SOURCE_DIR}/resources")
    target_link_libraries(native_ui_tests PRIVATE client_ui)
    target_link_options(native_ui_tests PRIVATE -municode)
    set_target_properties(native_ui_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME native_ui COMMAND native_ui_tests "${CMAKE_BINARY_DIR}/ui-fixtures")
endif()
