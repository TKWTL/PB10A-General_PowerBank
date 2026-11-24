#include "mini_format.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char b[32];
    assert(mini_snprintf(b,sizeof(b),"%u:%02X:%-4s",7u,0xabu,"OK")==9);
    assert(strcmp(b,"7:AB:OK  ")==0);
    assert(mini_snprintf(b,4u,"abcdef")==6 && strcmp(b,"abc")==0);
    assert(mini_snprintf(b,0u,"1234")==4);
    assert(mini_snprintf(b,sizeof(b),"%d",INT_MIN)>0);
    assert(strcmp(b,"-2147483648")==0);
    assert(mini_snprintf(b,sizeof(b),"%% %02u",5u)==4);
    assert(strcmp(b,"% 05")==0);
    assert(mini_snprintf(b,sizeof(b),"%s",(const char *)0)==6);
    assert(strcmp(b,"(null)")==0);
    puts("mini_format formats, truncation, INT_MIN PASS");
    return 0;
}
