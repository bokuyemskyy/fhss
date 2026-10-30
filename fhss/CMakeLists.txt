add_library(fhss_common STATIC Src/fhss.c Src/ring_buffer.c)

target_include_directories(fhss_common PUBLIC Inc)

target_link_libraries(fhss_common PUBLIC stm32cubemx nrf24l01)

if(FHSS_ENABLE_TRACE)
    target_compile_definitions(fhss_common PUBLIC FHSS_ENABLE_TRACE)
endif()

if(FHSS_ENABLE_STATS)
    target_compile_definitions(fhss_common PUBLIC FHSS_ENABLE_STATS)
endif()
