set(THREADX_ARCH "risc-v32")
set(THREADX_TOOLCHAIN "gnu")

# To be used to get the ESP-IDF target (e.g. ESP32)
idf_build_get_property(idf_target IDF_TARGET)

idf_component_register(SRCS ""
    INCLUDE_DIRS "threadx/common/inc" "threadx/ports/risc-v32/gnu/inc"
    PRIV_REQUIRES soc
    )

# Import ThreadX library targets
add_subdirectory(threadx)

set(threadx_targets threadx)

# Link ThreadX libraries to component library
target_link_libraries(${COMPONENT_LIB} INTERFACE ${threadx_targets})