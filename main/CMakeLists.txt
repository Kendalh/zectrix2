idf_component_register(SRCS
                       "app_main.cc"
                       "apps/clock_app.cc"
                       "apps/epd_demo_app.cc"
                       "apps/wifi_setup_app.cc"
                       INCLUDE_DIRS "."
                       REQUIRES
                       zectrix_board
                       zectrix_epd
                       zos
                       esp_timer
                       nvs_flash
                       EMBED_FILES
                       "assets/lighthouse_400x300_1bpp.bin"
                       "assets/snow_path_400x300_1bpp.bin"
                       "assets/footprint_animation.bin"
                       "assets/mountain_400x300_4bpp.bin")
