add_library(client_model STATIC platform.cpp profile.cpp diagnostics.cpp)
target_include_directories(client_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${VPN_GENERATED_DIR}")
target_compile_definitions(client_model PUBLIC
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00
    CERT_CHAIN_PARA_HAS_EXTRA_FIELDS GNUTLS_STATIC LIBXML_STATIC)
target_compile_options(client_model PUBLIC
    "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-ffunction-sections;-fdata-sections>"
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-Os>)
target_link_options(client_model INTERFACE -static -static-libgcc -static-libstdc++
    -Wl,--gc-sections -Wl,--dynamicbase -Wl,--nxcompat
    -Wl,--major-os-version,10 -Wl,--minor-os-version,0
    -Wl,--major-subsystem-version,6 -Wl,--minor-subsystem-version,2)
target_link_libraries(client_model PUBLIC ws2_32 winhttp crypt32 bcrypt advapi32 user32 shell32 ole32 gdi32 comctl32 comdlg32 normaliz)
add_library(client_session STATIC session.cpp certificates.cpp)
target_include_directories(client_session PUBLIC "${VPN_DEPS}/include")
target_link_libraries(client_session PUBLIC
    -Wl,--start-group client_model
    "${VPN_DEPS}/lib/libopenconnect.a"
    "${VPN_DEPS}/lib/libstoken.a"
    "${VPN_DEPS}/lib/libgnutls.a"
    "${VPN_DEPS}/lib/libxml2.a"
    "${VPN_DEPS}/lib/libz.a"
    "${VPN_DEPS}/lib/libhogweed.a"
    "${VPN_DEPS}/lib/libnettle.a"
    "${VPN_DEPS}/lib/libgmp.a"
    -Wl,--end-group ncrypt secur32 iphlpapi)
add_library(client_ui STATIC application.cpp ui.cpp dialogs.cpp)
target_link_libraries(client_ui PUBLIC client_session)
add_executable(LinkoraVPN WIN32 main.cpp "${PROJECT_SOURCE_DIR}/resources/app.rc")
set_source_files_properties("${PROJECT_SOURCE_DIR}/resources/app.rc" PROPERTIES
    OBJECT_DEPENDS "${VPN_GENERATED_DIR}/app.manifest;${PROJECT_SOURCE_DIR}/resources/app.ico")
target_include_directories(LinkoraVPN PRIVATE "${PROJECT_SOURCE_DIR}/resources")
target_link_options(LinkoraVPN PRIVATE -municode)
target_link_libraries(LinkoraVPN PRIVATE client_ui)
set_target_properties(LinkoraVPN PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
configure_file("${PROJECT_SOURCE_DIR}/.deps/sources/wintun/bin/${WINTUN_ARCH}/wintun.dll"
               "${CMAKE_BINARY_DIR}/wintun.dll" COPYONLY)
configure_file(vendor/vpnc-script-win.js "${CMAKE_BINARY_DIR}/vpnc-script-win.js" COPYONLY)
if(BUILD_TESTING)
    add_executable(native_connection_tests tests/connection_tests.cpp)
    target_include_directories(native_connection_tests PRIVATE "${VPN_DEPS}/include")
    target_link_libraries(native_connection_tests PRIVATE client_model)
    set_target_properties(native_connection_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME native_connections COMMAND native_connection_tests)
    add_executable(native_resource_tests tests/resource_tests.cpp)
    target_link_libraries(native_resource_tests PRIVATE client_ui psapi)
    set_target_properties(native_resource_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME native_resources COMMAND native_resource_tests)
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
