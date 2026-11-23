#include "../rle.h"
#include "../fifo.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

static void test_rle(void) {
    static const uint8_t src[] = {
        0x02, 0x01, 0x02, 0x03, /* literal 3 */
        0x41,                   /* 2 zeros */
        0x81,                   /* 2 FF */
        0xc2, 0xa5,             /* 3 A5 */
        0x3f,                  /* literal 64 */
    };
    uint8_t compressed[sizeof(src)+64];
    uint8_t expected[74], out[74], another[74];
    uint8_t buf[66];
    uint32_t i;
    memcpy(compressed, src, sizeof(src));
    for (i=0;i<64;i++) compressed[sizeof(src)+i]=(uint8_t)i;
    memset(expected,0,sizeof(expected));
    expected[0]=1;expected[1]=2;expected[2]=3;
    expected[5]=expected[6]=0xff;
    expected[7]=expected[8]=expected[9]=0xa5;
    for(i=0;i<64;i++)expected[10+i]=(uint8_t)i;
    assert(RLE_Decode(compressed,sizeof(compressed),out,sizeof(out),RLE_WRITE)==RLE_OK);
    assert(memcmp(expected,out,sizeof(out))==0);
    memset(another,0x5a,sizeof(another));
    assert(RLE_Decode(compressed,sizeof(compressed),another,sizeof(another),RLE_XOR)==RLE_OK);
    for(i=0;i<sizeof(another);i++)assert(another[i]==(uint8_t)(expected[i]^0x5a));
    assert(RLE_Decode(compressed,sizeof(compressed),out,73,RLE_WRITE)==RLE_ERR_OUTPUT);
    assert(RLE_Decode(compressed,sizeof(compressed)-1,out,74,RLE_WRITE)==RLE_ERR_INPUT);
    assert(RLE_Decode(compressed,sizeof(compressed),out,75,RLE_WRITE)==RLE_ERR_OUTPUT);
    assert(RLE_Decode(compressed,1,out,74,RLE_WRITE)==RLE_ERR_INPUT);
    assert(RLE_Decode(0,1,out,74,RLE_WRITE)==RLE_ERR_ARGUMENT);
    assert(RLE_Decode(compressed,sizeof(compressed),0,74,RLE_WRITE)==RLE_ERR_ARGUMENT);
    assert(RLE_Decode(compressed,sizeof(compressed),out,74,(rle_operation_t)2)==RLE_ERR_ARGUMENT);
    assert(RLE_Decode(0,0,0,0,RLE_WRITE)==RLE_OK);
    assert(RLE_Decode((const uint8_t[]){0x7f},1,buf,66,RLE_WRITE)==RLE_ERR_OUTPUT);
}

static void test_fifo(void) {
    uint8_t storage[8],out[8],in[8] = {10,11,12,13,14,15,16,17},v;
    fifo_t f={0};
    assert(!FIFO_Init(&f,storage,0));
    assert(!FIFO_Init(&f,storage,7));
    assert(!FIFO_Init(&f,storage,1));
    assert(!FIFO_Init(0,storage,8));
    assert(FIFO_Init(&f,storage,8));
    assert(FIFO_Count(&f)==0 && FIFO_Free(&f)==8);
    assert(FIFO_Write(&f,in,8)==8);
    assert(FIFO_Write(&f,in,1)==0);
    assert(FIFO_Count(&f)==8 && FIFO_Free(&f)==0);
    assert(FIFO_Read(&f,out,3)==3);
    assert(memcmp(out,in,3)==0);
    assert(FIFO_Write(&f,in,3)==3);
    assert(FIFO_Read(&f,out,8)==8);
    assert(memcmp(out,in+3,5)==0 && memcmp(out+5,in,3)==0);
    assert(FIFO_Count(&f)==0);
    assert(!FIFO_Pop(&f,&v));
    assert(FIFO_Push(&f,99));
    assert(FIFO_Pop(&f,&v)&&v==99);
    assert(FIFO_Push(&f,42));
    FIFO_Clear(&f);
    assert(FIFO_Count(&f)==0);
    assert(FIFO_Init(&f,storage,8));
    /* Exercise head/tail wraparound without doing 2^32 operations. */
    __atomic_store_n(&f.head,0xfffffffeu,__ATOMIC_RELAXED);
    __atomic_store_n(&f.tail,0xfffffffeu,__ATOMIC_RELAXED);
    assert(FIFO_Push(&f,7) && FIFO_Push(&f,8) && FIFO_Push(&f,9));
    assert(FIFO_Pop(&f,&v)&&v==7);
    assert(FIFO_Pop(&f,&v)&&v==8);
    assert(FIFO_Pop(&f,&v)&&v==9);
    assert(FIFO_Count(&f)==0);
}

int main(void){test_rle();test_fifo();puts("LIB RLE/FIFO tests PASS");return 0;}
