add_executable(dvitest
        dvitest.c
)

target_include_directories(dvitest PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/images
)

# pull in common dependencies
target_link_libraries(dvitest
        pico_stdlib
        pico_multicore
        hardware_dma
        pico_sync
        hardware_pio
)

# Build pio
pico_generate_pio_header(dvitest ${CMAKE_CURRENT_LIST_DIR}/ndsl_video.pio)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(dvitest)
