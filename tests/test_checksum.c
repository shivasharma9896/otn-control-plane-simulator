#include "control_channel.h"
#include <assert.h>
#include <stdio.h>
int main(void){const uint8_t x[]="otn";assert(control_checksum(x,3)!=0);assert(control_checksum(x,3)==control_checksum(x,3));puts("checksum tests passed");return 0;}
