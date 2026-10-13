cmake_minimum_required(VERSION 3.20)
project(konvoy VERSION 0.1.0 LANGUAGES CXX)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(KF6 6.0 REQUIRED COMPONENTS CoreAddons Config Parts XmlGui DBusAddons Notifications WindowSystem)

add_executable(konvoy
    src/main.cpp
    src/config.cpp
    src/theme.cpp
    src/pane.cpp
    src/panegrid.cpp
    src/sidebar.cpp
    src/newpanedialog.cpp
    src/mainwindow.cpp
    src/usage.cpp
    src/usageblock.cpp
    src/chats.cpp
)

# The icon also travels inside the program, for runs from the build folder.
qt_add_resources(konvoy "icon" PREFIX "/" FILES icons/io.github.tobias2909.konvoy.svg)

target_compile_definitions(konvoy PRIVATE
    PROJECT_VERSION_STRING="${PROJECT_VERSION}"
    KONVOY_LIBEXECDIR="${KDE_INSTALL_FULL_LIBEXECDIR}"
)

target_link_libraries(konvoy PRIVATE
    Qt6::Widgets
    Qt6::DBus
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::Parts
    KF6::XmlGui
    KF6::DBusAddons
    KF6::Notifications
    KF6::WindowSystem
)

install(TARGETS konvoy ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS hooks/konvoy-hook hooks/konvoy-hooks usage/konvoy-usage usage/konvoy-statusline DESTINATION ${KDE_INSTALL_LIBEXECDIR})
install(FILES konvoy.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install(FILES icons/io.github.tobias2909.konvoy.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
# The menu entry gets the full path: a prefix like ~/.local/bin is often only in the
# shell's PATH, not in the desktop session's.
configure_file(io.github.tobias2909.konvoy.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/io.github.tobias2909.konvoy.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.tobias2909.konvoy.desktop DESTINATION ${KDE_INSTALL_APPDIR})

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
