#include <stdint.h>
#include "errno.h"
#include "stdio.h"
#include "string.h"
#include "unistd.h"

#define SHARED_MEM_SIZE        (1024 * 1024 * 1)   // 1 MB
#define SHARED_MEM_START_ADDR  0x03000000
#define SHARED_MEM_END_ADDR    (SHARED_MEM_START_ADDR + SHARED_MEM_SIZE - 1)

uint8_t result[64 * 64 * 4];

void initialize() {
    uint16_t* hp_p = (uint16_t*)SHARED_MEM_START_ADDR;
    uint8_t* lp_p  = (uint8_t*)(SHARED_MEM_START_ADDR + 8192);
    uint8_t* w_p   = (uint8_t*)(SHARED_MEM_START_ADDR + 8192 + 4096);

    for (int i = 0; i < 64; i ++) {
        hp_p[i] = 0x3f80; // 1
    }

    for (int i = 0; i < 64; i ++) {
        lp_p[i] = 0x40; // 2
    }

    for (int i = 0; i < 64; i ++) {
        w_p[i] = 0x44;  // 3 
    }
}

void display() {
    uint8_t* o_p = (uint8_t*)(SHARED_MEM_START_ADDR + 8192 + 4096 + 4096);
    for (int i = 0; i < 64 * 64 * 4; i ++) {
        result[i] = o_p[i];
    }

    float* p = (float*)result;
    // for (int i = 0; i < 64 * 64; i ++) {
    //     if (i != 0 && i % 64 == 0) {
    //         printf("\n");
    //     }
    //     printf("%.2f ", p[i]);
    // }
    // printf("\n");
}

int main() {
    // Move data from HBM to shared memory
    initialize();

    // Do Compute : o_p[FP32](64, 64) = hp_p[BF16](64, 64) * w_p[FP8](64, 64) + lp_p[FP8](64, 64) * w_p[FP8](64, 64)
    asm volatile("idg.set idg.opcode,0x1");          // opcode: SPU

    asm volatile("idg.set idg.mma.opmask,0x1c1");    // opmask: load hp, load lp, load w, gemm, store o

    asm volatile("idg.set idg.mma.n,0x40");          // N = 64
    asm volatile("idg.set idg.mma.k,0x2");           // K = 64
    asm volatile("idg.set idg.mma.m,0x2");           // M = 64

    asm volatile("idg.set idg.mma.hp.addr,0x0");     // HP: 0x0 BF16
    asm volatile("idg.set idg.mma.hp.stride,0x0");    
    asm volatile("idg.set idg.mma.hp.dtype,0x3");    

    asm volatile("idg.set idg.mma.lp.addr,0x80");    // LP: 0x2000 FP8
    asm volatile("idg.set idg.mma.lp.stride,0x0");    
    asm volatile("idg.set idg.mma.lp.dtype,0x1");    

    asm volatile("idg.set idg.mma.w.addr,0xc0");     // W:  0x3000 FP8
    asm volatile("idg.set idg.mma.w.stride,0x0");    
    asm volatile("idg.set idg.mma.w.dtype,0x1");    

    asm volatile("idg.set idg.mma.out.addr,0x100");   // W:  0x4000 FP32
    asm volatile("idg.set idg.mma.out.stride,0x0");    
    asm volatile("idg.set idg.mma.out.dtype,0x7");    

    asm volatile("idg.set idg.zero,0x0");             // FIRE !!!

    asm volatile("idg.set idg.zero,0x100");           // SYNC POINT, just a trick for simualtion

    // Move data from shared memory to HBM
    // display();

	return 0;
}
