cmake_minimum_required(VERSION 3.24)
project(CampusPrintScheduler VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin")
endforeach()

option(CPS_BUILD_QT_APPS "Build the Qt GUI, worker and supervisor" ON)
option(CPS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
include(CTest)
find_package(Threads REQUIRED)

add_library(cps_options INTERFACE)
if(MSVC)
    target_compile_options(cps_options INTERFACE /W4 /permissive- /utf-8)
    target_compile_definitions(cps_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(CPS_WARNINGS_AS_ERRORS)
        target_compile_options(cps_options INTERFACE /WX)
    endif()
else()
    target_compile_options(cps_options INTERFACE -Wall -Wextra -Wpedantic)
    if(CPS_WARNINGS_AS_ERRORS)
        target_compile_options(cps_options INTERFACE -Werror)
    endif()
endif()

add_library(cps_core src/core/scheduler.cpp src/core/shop.cpp src/core/shop_replay.cpp src/core/estimate.cpp src/core/replay.cpp src/metrics/statistics.cpp)
target_include_directories(cps_core PUBLIC include)
target_link_libraries(cps_core PUBLIC Threads::Threads PRIVATE cps_options)

if(BUILD_TESTING)
    add_executable(shop_tests tests/shop_tests.cpp)
    target_link_libraries(shop_tests PRIVATE cps_core cps_options)
    add_test(NAME shop COMMAND shop_tests)
    add_executable(estimate_tests tests/estimate_tests.cpp)
    target_link_libraries(estimate_tests PRIVATE cps_core cps_options)
    add_test(NAME estimate COMMAND estimate_tests)
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE cps_core cps_options)
    add_test(NAME scheduler COMMAND core_tests)
    add_executable(scheduler_advanced_tests tests/scheduler_advanced_tests.cpp)
    target_link_libraries(scheduler_advanced_tests PRIVATE cps_core cps_options)
    add_test(NAME scheduler_advanced COMMAND scheduler_advanced_tests)
    set_tests_properties(scheduler_advanced PROPERTIES TIMEOUT 30)
    add_executable(metrics_replay_tests tests/metrics_replay_tests.cpp)
    target_link_libraries(metrics_replay_tests PRIVATE cps_core cps_options)
    add_test(NAME metrics_replay COMMAND metrics_replay_tests)
endif()

if(CPS_BUILD_QT_APPS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)
    add_executable(scheduler-replay tools/replay_main.cpp)
    target_link_libraries(scheduler-replay PRIVATE Qt6::Core cps_core cps_options)
    add_library(cps_runtime
        src/runtime/job_io.cpp
        src/runtime/state_store.cpp
        src/runtime/shop_io.cpp
        src/platform/daemon.cpp
        src/platform/path.cpp
        src/platform/stdio.cpp)
    target_include_directories(cps_runtime PUBLIC include)
    target_link_libraries(cps_runtime PUBLIC Qt6::Core cps_core PRIVATE cps_options)
    add_executable(shop-experiments tools/shop_experiments.cpp)
    target_link_libraries(shop-experiments PRIVATE cps_runtime cps_options)

    add_executable(print-worker src/worker/main.cpp)
    target_link_libraries(print-worker PRIVATE cps_runtime cps_options)
    add_executable(print-supervisor src/supervisor/main.cpp src/supervisor/control_server.cpp)
    target_link_libraries(print-supervisor PRIVATE cps_runtime Qt6::Network cps_options)
    add_executable(print-coordinator src/coordinator/main.cpp)
    target_link_libraries(print-coordinator PRIVATE cps_runtime Qt6::Network cps_options)
    # Flat executable layout is intentional in the development scaffold.
    # GUI and child programs live together in build/<preset>/bin on every OS.
    add_library(cps_gui src/gui/event_stream.cpp src/gui/control_client.cpp src/gui/print_controller.cpp src/gui/print_window.cpp src/gui/editorial_chrome.cpp)
    target_sources(cps_gui PRIVATE src/gui/merchant_data.cpp src/gui/merchant_painter.cpp
        src/gui/merchant_orders.cpp src/gui/merchant_devices_stats.cpp src/gui/merchant_window.cpp src/gui/merchant_shell.cpp
        src/gui/merchant_dispatch_model.cpp src/gui/merchant_dispatch_painter.cpp)
    qt_add_resources(cps_gui merchant_assets PREFIX "/merchant" BASE "assets/merchant"
        FILES assets/merchant/workbench.png assets/merchant/queue.png assets/merchant/devices.png assets/merchant/stats.png
        assets/merchant/hero-paper-v10.png assets/merchant/dispatch-paper-v10.png
        assets/merchant/brand-16.png assets/merchant/brand-24.png assets/merchant/brand-32.png
        assets/merchant/brand-48.png assets/merchant/brand-64.png assets/merchant/brand-128.png
        assets/merchant/brand-256.png
        assets/merchant/icons-v3/brand-16.png assets/merchant/icons-v3/brand-24.png assets/merchant/icons-v3/brand-32.png
        assets/merchant/icons-v3/brand-48.png assets/merchant/icons-v3/brand-64.png assets/merchant/icons-v3/brand-128.png
        assets/merchant/icons-v3/brand-256.png)
    set_target_properties(cps_gui PROPERTIES AUTOMOC ON)
    target_include_directories(cps_gui PUBLIC src/gui)
    target_link_libraries(cps_gui PUBLIC cps_runtime Qt6::Widgets Qt6::Network PRIVATE cps_options)
    add_executable(campus-print src/gui/main.cpp)
    if(WIN32)
        target_link_libraries(cps_gui PRIVATE shell32 shlwapi ole32 propsys uuid dwmapi)
        set_target_properties(campus-print PROPERTIES WIN32_EXECUTABLE TRUE)
        configure_file(assets/merchant/app.rc.in merchant-icon.rc @ONLY)
        target_sources(campus-print PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/merchant-icon.rc")
        add_executable(campus-print-merchant WIN32 src/gui/main.cpp)
        target_sources(campus-print-merchant PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/merchant-icon.rc")
        target_link_libraries(campus-print-merchant PRIVATE cps_gui cps_options)
        add_custom_command(TARGET campus-print POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/merchant/icons-v3/paperroom-merchant.ico"
            "$<TARGET_FILE_DIR:campus-print>/paperroom-merchant-v3.ico")
        add_custom_command(TARGET campus-print-merchant POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/merchant/icons-v3/paperroom-merchant.ico"
            "$<TARGET_FILE_DIR:campus-print-merchant>/paperroom-merchant-v3.ico")
    endif()
    target_link_libraries(campus-print PRIVATE cps_gui cps_options)

    if(BUILD_TESTING)
        find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
        add_executable(office_converter_stub tests/office_converter_stub.cpp)
        target_link_libraries(office_converter_stub PRIVATE Qt6::Core cps_options)
        add_executable(gui_tests tests/gui_tests.cpp tests/native_frame_evidence.cpp tests/windows_test_logs.cpp)
        set_target_properties(gui_tests PROPERTIES AUTOMOC ON)
        target_link_libraries(gui_tests PRIVATE cps_gui Qt6::Test cps_options)
        add_test(NAME gui COMMAND gui_tests -o "${CMAKE_BINARY_DIR}/gui-results.xml,junitxml" -o -,txt)
        set_tests_properties(gui PROPERTIES TIMEOUT 300 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(merchant_tests tests/merchant_tests.cpp tests/native_frame_evidence.cpp tests/windows_test_logs.cpp)
        set_target_properties(merchant_tests PROPERTIES AUTOMOC ON)
        target_link_libraries(merchant_tests PRIVATE cps_gui Qt6::Test cps_options)
        add_test(NAME merchant COMMAND merchant_tests -o "${CMAKE_BINARY_DIR}/merchant-results.xml,junitxml" -o -,txt)
        add_executable(merchant_dispatch_tests tests/merchant_dispatch_tests.cpp)
        target_link_libraries(merchant_dispatch_tests PRIVATE cps_gui cps_options)
        add_test(NAME merchant_dispatch COMMAND merchant_dispatch_tests)
        set_tests_properties(merchant PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        add_executable(runtime_state_tests tests/runtime_state_tests.cpp)
        add_executable(shop_integration_tests tests/shop_integration.cpp)
        target_link_libraries(shop_integration_tests PRIVATE cps_runtime Qt6::Network cps_options)
        add_test(NAME shop_integration COMMAND shop_integration_tests)
        set_tests_properties(shop_integration PROPERTIES TIMEOUT 40)
        target_link_libraries(runtime_state_tests PRIVATE cps_runtime Qt6::Core cps_options)
        add_test(NAME runtime_state COMMAND runtime_state_tests)

        add_executable(coordinator_integration_tests tests/coordinator_integration.cpp)
        target_link_libraries(coordinator_integration_tests PRIVATE Qt6::Core Qt6::Network cps_options)
        add_test(NAME coordinator_integration COMMAND coordinator_integration_tests
            --coordinator "$<TARGET_FILE:print-coordinator>")
        set_tests_properties(coordinator_integration PROPERTIES TIMEOUT 45)

        set(Python3_FIND_STRATEGY LOCATION)
        find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
        add_test(NAME classroom_web COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/classroom_web_test.py" --coordinator "$<TARGET_FILE:print-coordinator>"
            --office-stub "$<TARGET_FILE:office_converter_stub>")
        set_tests_properties(classroom_web PROPERTIES TIMEOUT 90)
        add_test(NAME office_web COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/office_web_preview.py" --source-root "${CMAKE_SOURCE_DIR}")
        set_tests_properties(office_web PROPERTIES TIMEOUT 660 SKIP_RETURN_CODE 77)
        add_test(NAME shop_experiments COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/shop_experiments_test.py" --experiment "$<TARGET_FILE:shop-experiments>")
        set_tests_properties(shop_experiments PROPERTIES TIMEOUT 45)
        add_test(NAME package_layout COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/package_layout_test.py")
        set_tests_properties(package_layout PROPERTIES TIMEOUT 10)
        add_test(NAME native_test_runner COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/native_test_runner_test.py")
        set_tests_properties(native_test_runner PROPERTIES TIMEOUT 15)
        if(WIN32)
            add_test(NAME windows_console_audit COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/windows_console_audit_test.py")
            set_tests_properties(windows_console_audit PROPERTIES TIMEOUT 30)
        endif()
        add_test(NAME document_workload COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/document_workload_tests.py" --experiment "$<TARGET_FILE:shop-experiments>")
        set_tests_properties(document_workload PROPERTIES TIMEOUT 45)
        add_test(NAME workload_tools COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/workload_tools_test.py"
            --replay "$<TARGET_FILE:scheduler-replay>"
            --converter "${CMAKE_SOURCE_DIR}/tools/convert_swf.py")
        set_tests_properties(workload_tools PROPERTIES TIMEOUT 60)
        add_test(NAME process_integration COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/process_integration.py"
            --worker "$<TARGET_FILE:print-worker>"
            --supervisor "$<TARGET_FILE:print-supervisor>"
            --gui "$<TARGET_FILE:campus-print>")
        set_tests_properties(process_integration PROPERTIES TIMEOUT 90)
        add_test(NAME control_integration COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/control_integration.py"
            --supervisor "$<TARGET_FILE:print-supervisor>")
        set_tests_properties(control_integration PROPERTIES TIMEOUT 90)
    endif()
endif()
