#pragma once
#include <stdint.h>

// Sparse gestures separated by long, variable work/rest phases.
enum class PetMotion : uint8_t { Typing, Rest, Review, Wave, Happy };
constexpr uint8_t PET_PLAN_LENGTH = 11;
const PetMotion PET_PLAN[PET_PLAN_LENGTH] = {
    PetMotion::Typing, PetMotion::Rest, PetMotion::Review, PetMotion::Typing,
    PetMotion::Rest, PetMotion::Wave, PetMotion::Rest, PetMotion::Typing,
    PetMotion::Review, PetMotion::Happy, PetMotion::Rest};

inline uint32_t petMotionDuration(PetMotion motion, uint32_t variation) {
  switch (motion) {
    case PetMotion::Typing: return 12000 + variation % 9000;
    case PetMotion::Rest: return 9000 + variation % 14000;
    case PetMotion::Review: return 6000 + variation % 6000;
    case PetMotion::Wave: return 1800;
    case PetMotion::Happy: return 2700;
  }
  return 12000;
}

inline uint8_t petMotionFrame(PetMotion motion, uint32_t elapsed) {
  switch (motion) {
    case PetMotion::Typing: {
      const uint16_t holds[6] = {700, 450, 450, 900, 500, 900};
      uint32_t position = elapsed % 3900;
      for (uint8_t i = 0; i < 6; ++i) {
        if (position < holds[i]) return i;
        position -= holds[i];
      }
      return 0;
    }
    case PetMotion::Rest: return 6 + (elapsed % 5500 >= 5200 ? 1 : 0);
    case PetMotion::Review: return 12 + (elapsed / 700) % 6;
    case PetMotion::Wave: return 18 + (elapsed / 450 < 4 ? elapsed / 450 : 3);
    case PetMotion::Happy: return 22 + (elapsed / 450 < 6 ? elapsed / 450 : 5);
  }
  return 0;
}
