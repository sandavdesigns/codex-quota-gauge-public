#include <assert.h>
#include <stdio.h>
#include "pet_visible_bounds.h"
int main(){
 for(int i=0;i<28;++i){
  int height=PET_VISIBLE_HEIGHT[i], top=104+(PET_DRAW_HEIGHT-height)/2;
  int gapAbove=top-95, gapBelow=209-(top+height);
  assert(PET_VISIBLE_TOP[i]+height<=104);
  assert(height<=PET_DRAW_HEIGHT && top>=100 && top+height<=200);
  assert(gapAbove-gapBelow>=-1 && gapAbove-gapBelow<=1);
 }
 puts("PASS: all 28 poses have equal visible gaps within one pixel; source and destination bounds valid");
}
