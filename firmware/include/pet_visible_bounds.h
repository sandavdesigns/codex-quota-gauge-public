#pragma once
#include <stdint.h>
// Opaque bounds of the original 28 frames at 96x104.
constexpr uint16_t PET_DRAW_HEIGHT=96;
const uint8_t PET_VISIBLE_TOP[28]={25,25,25,26,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,19,9,16,25,20};
const uint8_t PET_VISIBLE_HEIGHT[28]={77,77,77,76,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77,77};
