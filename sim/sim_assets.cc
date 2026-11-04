// Embeds the demo images under the same symbols ESP-IDF's EMBED_FILES creates.
#define EMBED(sym, file)                                                   \
    __asm__(".section .rodata\n.global _binary_" sym "_start\n"            \
            "_binary_" sym "_start:\n.incbin \"" ASSET_DIR "/" file "\"\n" \
            ".global _binary_" sym "_end\n_binary_" sym "_end:\n.byte 0\n.previous\n");

EMBED("lighthouse_400x300_1bpp_bin", "lighthouse_400x300_1bpp.bin")
EMBED("snow_path_400x300_1bpp_bin", "snow_path_400x300_1bpp.bin")
EMBED("footprint_animation_bin", "footprint_animation.bin")
EMBED("mountain_400x300_4bpp_bin", "mountain_400x300_4bpp.bin")
