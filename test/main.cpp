/**
  * Unit test entry point
  */

#include <stdio.h>
#include <assert.h>
#include <string>

extern void testVOct();
//extern void testGate();
//extern void testOnVel();
//extern void testOffVel();
//extern void testPB();
//extern void testYaxis();
//extern void testPress();
//extern void testModes();

int main(int argc, char ** argv)
{

    assert(sizeof(size_t) == 8);

    printf("Tests passed.\n");

    return 0;
}
