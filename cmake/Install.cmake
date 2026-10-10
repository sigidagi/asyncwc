include_guard(GLOBAL)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(ASYNCWC_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/asyncwc")

install(TARGETS asyncwc
    EXPORT asyncwcTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/" "${ASYNCWC_GENERATED_INCLUDE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    PATTERN "*.in" EXCLUDE)

install(EXPORT asyncwcTargets
    NAMESPACE asyncwc::
    DESTINATION "${ASYNCWC_CMAKE_INSTALL_DIR}")

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/asyncwcConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/asyncwcConfig.cmake"
    INSTALL_DESTINATION "${ASYNCWC_CMAKE_INSTALL_DIR}")

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/asyncwcConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${PROJECT_BINARY_DIR}/asyncwcConfig.cmake"
    "${PROJECT_BINARY_DIR}/asyncwcConfigVersion.cmake"
    DESTINATION "${ASYNCWC_CMAKE_INSTALL_DIR}")
