cmake_minimum_required(VERSION 3.22)
project(spacedock VERSION 0.5.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick DBus Widgets Concurrent)
find_package(KF6Config 6.0 REQUIRED)
find_package(KF6CoreAddons 6.0 REQUIRED)
find_package(KF6Service 6.0 REQUIRED)
find_package(KF6WidgetsAddons 6.0 REQUIRED)
qt_standard_project_setup()
qt_add_executable(spacedock src/trashcontroller.cpp src/trashcontroller.h src/imagepicker.cpp src/imagepicker.h src/docklifecycle.cpp src/docklifecycle.h src/main.cpp src/dockbackend.cpp src/dockbackend.h src/appimagestore.cpp src/appimagestore.h src/appimagecontroller.cpp src/appimagecontroller.h src/tooltipshadow.cpp src/tooltipshadow.h src/tuning.cpp src/tuning.h src/submenuplacement.h)
target_link_libraries(spacedock PRIVATE Qt6::Quick Qt6::DBus Qt6::Widgets Qt6::Concurrent KF6::ConfigCore KF6::CoreAddons KF6::Service KF6::WidgetsAddons)
qt_add_resources(spacedock ui PREFIX "/" FILES qml/Main.qml qml/DockSettings.qml qml/ApplicationSettings.qml qml/AutoHideController.qml qml/ExternalDropController.qml qml/DragController.qml qml/AppImageSetupMenu.qml qml/TaskArtwork.qml qml/AppImageArtworkTransition.qml qml/TaskMenu.qml qml/DockMenu.qml qml/DockMenuItem.qml qml/DockMenuSeparator.qml qml/DockToolTip.qml qml/Bounce.qml qml/Magnification.js qml/Shelf.qml qml/DockWindow.qml qml/PreviewWindow.qml tuning.json)
qt_add_resources(spacedock appimage_artwork PREFIX "/" FILES assets/icons/appimage_closed.png assets/icons/appimage_open.png)
include(CTest)
# Development helper: a real, independently launched Wayland app with a delayed
# first window. It is intentionally not part of the installed dock package.
if(BUILD_TESTING)
qt_add_executable(spacedock-launch-test src/launchtest.cpp)
target_link_libraries(spacedock-launch-test PRIVATE Qt6::Quick)
qt_add_resources(spacedock-launch-test launchtestui PREFIX "/" FILES qml/LaunchTestWindow.qml)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/spacedock-launch-test.desktop" CONTENT
"[Desktop Entry]
Type=Application
Name=Slow Launch Test (3 seconds)
Comment=Wait three seconds before showing a ready window
Exec=\"$<TARGET_FILE:spacedock-launch-test>\"
Icon=system-run
Terminal=false
StartupNotify=true
Categories=Development;
" FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ WORLD_READ)
add_dependencies(spacedock spacedock-launch-test)
endif()
# The development registration must identify the actual build executable, not
# an installed path or shell wrapper: KWin matches the canonical Exec path.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/spacedock.desktop.in" DOCK_DESKTOP_TEMPLATE)
string(REPLACE "@SPACEDOCK_EXEC@" "$<TARGET_FILE:spacedock>" DOCK_DESKTOP_BUILD "${DOCK_DESKTOP_TEMPLATE}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/spacedock.desktop" CONTENT "${DOCK_DESKTOP_BUILD}")
include(GNUInstallDirs)
configure_file(cmake/InstallDesktop.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/InstallDesktop.cmake @ONLY)
install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/InstallDesktop.cmake)
install(TARGETS spacedock RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES tuning.json DESTINATION ${CMAKE_INSTALL_DATADIR}/spacedock)
install(FILES assets/MANIFEST.csv DESTINATION ${CMAKE_INSTALL_DATADIR}/spacedock/assets)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(tuning-test tests/tuning_test.cpp src/tuning.cpp src/tuning.h)
    target_include_directories(tuning-test PRIVATE src)
    target_link_libraries(tuning-test PRIVATE Qt6::Test)
    qt_add_resources(tuning-test defaults PREFIX "/" FILES tuning.json)
    add_test(NAME tuning COMMAND tuning-test)
    add_executable(launcher-trust-test tests/launcher_trust_test.cpp)
    target_link_libraries(launcher-trust-test PRIVATE Qt6::Core KF6::ConfigCore)
    add_test(NAME launcher-trust COMMAND launcher-trust-test ${CMAKE_CURRENT_BINARY_DIR}/spacedock-launch-test.desktop)
    add_test(NAME desktop-registration COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_desktop_registration.py ${CMAKE_SOURCE_DIR}/tools/register_desktop.py ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(desktop-registration PROPERTIES TIMEOUT 45)
    add_test(NAME slow-launch-app COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_launch_app.py $<TARGET_FILE:spacedock-launch-test> ${CMAKE_CURRENT_BINARY_DIR}/spacedock-launch-test.desktop)
    set_tests_properties(slow-launch-app PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    qt_add_executable(startupenvironment-test tests/startupenvironment_test.cpp)
    target_include_directories(startupenvironment-test PRIVATE src)
    target_link_libraries(startupenvironment-test PRIVATE Qt6::Test)
    add_test(NAME launch-environment COMMAND startupenvironment-test)
    qt_add_executable(magnification-test tests/magnification_test.cpp)
    target_link_libraries(magnification-test PRIVATE Qt6::Test Qt6::Qml)
    qt_add_resources(magnification-test geometry PREFIX "/" FILES qml/Magnification.js)
    add_test(NAME magnification COMMAND magnification-test)
    qt_add_executable(appimagestore-test tests/appimagestore_test.cpp src/appimagestore.cpp src/appimagestore.h)
    target_include_directories(appimagestore-test PRIVATE src)
    target_link_libraries(appimagestore-test PRIVATE Qt6::Test Qt6::Gui KF6::ConfigCore KF6::CoreAddons KF6::Service)
    add_test(NAME appimage-store COMMAND appimagestore-test)
    set_tests_properties(appimage-store PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    # Compile the shared backend once; keep suite fixtures local to their owner.
    add_library(dock-ui-test-support STATIC
        tests/launcher_model.h
        src/trashcontroller.cpp src/trashcontroller.h
        src/imagepicker.cpp src/imagepicker.h
        src/dockbackend.cpp src/dockbackend.h
        src/appimagestore.cpp src/appimagestore.h
        src/appimagecontroller.cpp src/appimagecontroller.h
        src/tooltipshadow.cpp src/tooltipshadow.h src/tuning.cpp src/tuning.h)
    target_include_directories(dock-ui-test-support PUBLIC src tests)
    target_link_libraries(dock-ui-test-support PUBLIC Qt6::Test Qt6::Quick Qt6::DBus Qt6::Widgets Qt6::Concurrent KF6::ConfigCore KF6::CoreAddons KF6::Service KF6::WidgetsAddons)
    function(add_dock_ui_test target source test_name)
        qt_add_executable(${target} ${source})
        set_target_properties(${target} PROPERTIES AUTOUIC OFF)
        target_link_libraries(${target} PRIVATE dock-ui-test-support)
        qt_add_resources(${target} ${target}_ui PREFIX "/" FILES qml/Main.qml qml/DockSettings.qml qml/ApplicationSettings.qml qml/AutoHideController.qml qml/ExternalDropController.qml qml/DragController.qml qml/AppImageSetupMenu.qml qml/TaskArtwork.qml qml/AppImageArtworkTransition.qml qml/TaskMenu.qml qml/DockMenu.qml qml/DockMenuItem.qml qml/DockMenuSeparator.qml qml/DockToolTip.qml qml/Bounce.qml qml/Magnification.js qml/Shelf.qml qml/DockWindow.qml qml/PreviewWindow.qml tuning.json)
        qt_add_resources(${target} ${target}_artwork PREFIX "/" FILES assets/icons/appimage_closed.png assets/icons/appimage_open.png)
        add_test(NAME ${test_name} COMMAND ${target})
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endfunction()
    add_dock_ui_test(magnification-ui-test tests/magnification_ui_test.cpp magnification-ui)
    # This suite exercises several real-time animations and can exceed the
    # shared 45-second limit even when every assertion succeeds.
    set_tests_properties(magnification-ui PROPERTIES TIMEOUT 120)
    add_dock_ui_test(appimage-integration-test tests/appimage_integration_test.cpp appimage-integration)
    add_dock_ui_test(artwork-ui-test tests/artwork_ui_test.cpp artwork-ui)
    add_dock_ui_test(dockbackend-test tests/dockbackend_test.cpp dock-backend)
    add_test(NAME asset-policy COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_assets.py --root ${CMAKE_SOURCE_DIR})
    add_test(NAME asset-policy-tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_asset_policy.py)
    add_test(NAME dock-lifecycle COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_dock_lifecycle.py $<TARGET_FILE:spacedock> ${CMAKE_SOURCE_DIR}/tuning.json)
    set_tests_properties(dock-lifecycle PROPERTIES TIMEOUT 30)
    add_test(NAME qml-smoke COMMAND spacedock --smoke-test)
    set_tests_properties(qml-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
