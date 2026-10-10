set(public_files
    "${CMAKE_CURRENT_SOURCE_DIR}/include/OpenLoco/Modding/Activation.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/OpenLoco/Modding/Dependencies.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/OpenLoco/Modding/Discovery.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/OpenLoco/Modding/Manifest.hpp"
)

set(private_files
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Activation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Dependencies.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Discovery.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Manifest.cpp"
)

set(test_files
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ActivationTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/DependenciesTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/DiscoveryTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ManifestTests.cpp"
)

loco_add_library(Modding STATIC
    PUBLIC_FILES
        ${public_files}
    PRIVATE_FILES
        ${private_files}
    TEST_FILES
        ${test_files}
)
