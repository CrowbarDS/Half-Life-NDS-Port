set(HLDS_INSTALLER_SOURCES
    Detect.cpp
    Detect.hpp
    Installer.cpp
    Installer.hpp
    InstallWorker.cpp
    InstallWorker.hpp
)

# (Qt 5 has neither qt_standard_project_setup nor qt_add_executable: the plain
# CMake ones, with moc run by CMake.)
if(HLDS_QT EQUAL 6)
    qt_standard_project_setup(REQUIRES 6.8)
    set(hlds_add_executable qt_add_executable)
else()
    set(CMAKE_AUTOMOC ON)
    set(hlds_add_executable add_executable)
endif()

cmake_language(CALL ${hlds_add_executable} hlds-convert-gui main.cpp ${HLDS_INSTALLER_SOURCES})
target_link_libraries(hlds-convert-gui
    PRIVATE
        hlds_convert_core
        hlds_install
        hlds_bundled_rom
        Qt::Widgets
)
set_target_properties(hlds-convert-gui PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# The same pages driven with no display, for a test.
cmake_language(CALL ${hlds_add_executable} hlds-install-check InstallCheck.cpp ${HLDS_INSTALLER_SOURCES})
target_link_libraries(hlds-install-check
    PRIVATE
        hlds_convert_core
        hlds_install
        hlds_bundled_rom
        Qt::Widgets
)
