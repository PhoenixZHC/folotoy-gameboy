idf_component_register(SRCS "minigb_apu.c" INCLUDE_DIRS ".")
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MINIGB_APU_AUDIO_FORMAT_S16SYS=1 AUDIO_SAMPLE_RATE=14000)
