#pragma once

#include <cstdint>

bool loadTexRGBA32fromBuffer(void *d3d11Device, uint8_t *pBuffer, int width, int height, void **pOut);
bool loadTexBGRA32fromBuffer(void *d3d11Device, uint8_t *pBuffer, int width, int height, void **pOut);

bool releaseTexture(void *pTexture);

bool loadFileFull(const char *fileName, char **pOutBuffer, size_t *pSize);

char *createRGBATexData(int width, int height, unsigned int rgbaColor);


