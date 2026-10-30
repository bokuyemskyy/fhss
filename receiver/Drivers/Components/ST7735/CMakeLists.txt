add_library(st7735 STATIC Src/st7735.c Src/fonts.c)

target_include_directories(st7735 PUBLIC Inc)

target_link_libraries(st7735 PUBLIC stm32cubemx)