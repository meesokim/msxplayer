#include "png_rgb.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

extern "C" int writePngRgb24(const char* path, int width, int height, const unsigned char* rgb, int rowStride) {
    if (!path || width <= 0 || height <= 0 || !rgb || rowStride < width * 3) return 0;
    return stbi_write_png(path, width, height, 3, rgb, rowStride) ? 1 : 0;
}
