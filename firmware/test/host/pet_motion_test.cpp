#include <assert.h>
#include <stdio.h>
#include "pet_motion.h"
int main() {
  for (uint8_t phase = 0; phase < PET_PLAN_LENGTH; ++phase) {
    const PetMotion motion = PET_PLAN[phase];
    const uint32_t duration = petMotionDuration(motion, 0xffffffffU);
    assert(duration >= 1800 && duration <= 23000);
    for (uint32_t ms = 0; ms < duration; ++ms) assert(petMotionFrame(motion, ms) < 28);
  }
  assert(petMotionFrame(PetMotion::Rest, 0) == petMotionFrame(PetMotion::Rest, 5000));
  assert(petMotionFrame(PetMotion::Rest, 5200) != petMotionFrame(PetMotion::Rest, 0));
  assert(petMotionFrame(PetMotion::Typing, 699) == 0);
  assert(petMotionFrame(PetMotion::Typing, 700) == 1);
  assert(petMotionFrame(PetMotion::Typing, 3899) == 5);
  assert(petMotionFrame(PetMotion::Typing, 3900) == 0);
  assert(petMotionFrame(PetMotion::Wave, 1800) == 21);
  assert(petMotionFrame(PetMotion::Happy, 2700) == 27);
  puts("PASS: motion bounds, durations, quiet holds, blinks, slow typing, finite gestures");
}
