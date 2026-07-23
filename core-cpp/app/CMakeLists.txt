# The wled-pc-rgb GUI app (Qt6 Widgets). Built only when Qt6 is available
# (see the guard in ../CMakeLists.txt).
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()   # enables AUTOMOC/AUTOUIC/AUTORCC

qt_add_executable(wled_pc_rgb
    src/main.cpp
    src/main_window.h
    src/main_window.cpp
    src/orgb_client.h
    src/orgb_client.cpp
    src/sysinfo.h
    src/sysinfo.cpp
    src/ipc_client.h
    src/ipc_client.cpp
)

target_link_libraries(wled_pc_rgb PRIVATE
    Qt6::Widgets
    Qt6::Network      # QLocalServer/QLocalSocket (single-instance); OpenRGB SDK client later
)

# GUI subsystem on Windows (no console window when launched).
set_target_properties(wled_pc_rgb PROPERTIES WIN32_EXECUTABLE ON)

# --- Make the build self-contained on Windows -------------------------------
# windeployqt alone misses the third-party MinGW libs Qt pulls in (libpcre2,
# libzstd, libpng, ...), so the exe would still fail with STATUS_DLL_NOT_FOUND.
# scripts/deploy-win.sh runs windeployqt AND copies the full ldd dependency
# closure, leaving a standalone exe that runs from anywhere.
if(WIN32)
    add_custom_command(TARGET wled_pc_rgb POST_BUILD
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/deploy-win.sh"
                "$<TARGET_FILE:wled_pc_rgb>"
        VERBATIM
        COMMENT "deploy-win: bundling Qt + full MinGW DLL closure (standalone exe)")
endif()

# Bundle the Java backend next to the exe so the app can auto-launch it.
add_custom_command(TARGET wled_pc_rgb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../../wled-backend-java/backend/WledBackend.java"
            "$<TARGET_FILE_DIR:wled_pc_rgb>/WledBackend.java"
    VERBATIM
    COMMENT "Bundling WledBackend.java next to the app")
