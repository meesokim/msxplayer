#ifndef PNG_RGB_H
#define PNG_RGB_H

#ifdef __cplusplus
extern "C" {
#endif

/** 8-bit RGB, rowStride >= width*3. Uses zlib (compress). */
int writePngRgb24(const char* path, int width, int height, const unsigned char* rgb, int rowStride);

#ifdef __cplusplus
}
#endif

#endif
