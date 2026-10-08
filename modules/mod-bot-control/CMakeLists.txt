# Sources are collected automatically by the parent modules/CMakeLists.txt.
# The bridge uses Playerbots and the character system's bundled JSON library.

if(BUILD_TESTING)
    set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/bc_frames_test.cpp")
    set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()
