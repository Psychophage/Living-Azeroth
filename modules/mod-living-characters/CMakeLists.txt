# Sources are collected automatically by the parent modules/CMakeLists.txt

# Unit tests use the core's Google Test target.
if(BUILD_TESTING)
    set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pbc_budget_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pbc_dialogue_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pbc_model_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pbc_conversation_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pbc_actions_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pbc_action_store_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pbc_store_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pbc_service_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pbc_context_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pbc_knowledge_test.cpp")
    set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/llm"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/api"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/events"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/character"
        "${CMAKE_CURRENT_SOURCE_DIR}/../mod-playerbots/src/Bot"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps")
endif()
