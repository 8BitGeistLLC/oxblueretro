if(DEFINED IDF_TARGET)
    idf_component_register(
        INCLUDE_DIRS
            "${CMAKE_CURRENT_LIST_DIR}"
    )
else()
    add_library(gamepad INTERFACE)
    target_include_directories(gamepad INTERFACE "${CMAKE_CURRENT_LIST_DIR}")
endif()