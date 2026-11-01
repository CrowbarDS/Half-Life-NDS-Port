qt_standard_project_setup(REQUIRES 6.8)

set(HLDS_INSTALLER_SOURCES
    Detect.cpp
    Detect.hpp
    Installer.cpp
    Installer.hpp
    InstallWorker.cpp
    InstallWorker.hpp
)

qt_add_executable(hlds-convert-gui main.cpp ${HLDS_INSTALLER_SOURCES})
target_link_libraries(hlds-convert-gui
    PRIVATE
        hlds_convert_core
        hlds_install
        Qt6::Widgets
)
set_target_properties(hlds-convert-gui PROPERTIES
    WIN32_EXECUTABLE ON
)

# The same pages driven with no display, for a test.
qt_add_executable(hlds-install-check InstallCheck.cpp ${HLDS_INSTALLER_SOURCES})
target_link_libraries(hlds-install-check
    PRIVATE
        hlds_convert_core
        hlds_install
        Qt6::Widgets
)
