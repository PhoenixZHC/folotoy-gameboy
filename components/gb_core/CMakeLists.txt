idf_component_register(
    SRCS "gb_port.cpp" "sdfat_compat.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_timer minigb_apu
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fjump-tables -ftree-switch-conversion)
