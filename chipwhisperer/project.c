#include "hal.h"
#include "simpleserial.h"
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

static inline unsigned
dec16le(const void *src)
{
	const uint8_t *buf = src;
	return (unsigned)buf[0]
		| ((unsigned)buf[1] << 8);
}

void rng_gb(void *ctx, void *dst, size_t len) {

	uint8_t *dst_f = (uint8_t *)dst;
	
	for (int j=0;j<len;j++){
        dst_f[j] = rand() % 256;
	}
	
}

static inline uint64_t
dec64le(const void *src)
{
	const uint8_t *buf = src;
	return (uint64_t)buf[0]
		| ((uint64_t)buf[1] << 8)
		| ((uint64_t)buf[2] << 16)
		| ((uint64_t)buf[3] << 24)
		| ((uint64_t)buf[4] << 32)
		| ((uint64_t)buf[5] << 40)
		| ((uint64_t)buf[6] << 48)
		| ((uint64_t)buf[7] << 56);
}

static uint32_t
sig_gauss(unsigned logn,
	void (*rng)(void *ctx, void *dst, size_t len), void *rng_context, int8_t *x, const uint8_t *t)
{
	const uint16_t tab_hi[] = {
		0x58B0, 0x36FE,
		0x1E3A, 0x0EA0,
		0x0632, 0x024A,
		0x00BC, 0x0034,
		0x000C, 0x0002
	};
	
	#define SG_MAX_HI_Hawk_1024 ((sizeof tab_hi) / sizeof(uint16_t))
	
	const uint64_t tab_lo[] = {
		0x3AAA2EB76504E560, 0x01AE2B17728DF2DE,
		0x70E1C03E49BB683E, 0x6A00B82C69624C93,
		0x55CDA662EF2D1C48, 0x2685DB30348656A4,
		0x31E874B355421BB7, 0x430192770E205503,
		0x57C0676C029895A7, 0x5353BD4091AA96DB,
		0x3D4D67696E51F820, 0x09915A53D8667BEE,
		0x014A1A8A93F20738, 0x0026670030160D5F,
		0x0003DAF47E8DFB21, 0x0000557CD1C5F797,
		0x000006634617B3FF, 0x0000006965E15B13,
		0x00000005DBEFB646, 0x0000000047E9AB38,
		0x0000000002F93038, 0x00000000001B2445,
		0x000000000000D5A7, 0x00000000000005AA,
		0x0000000000000021, 0x0000000000000000
	};
	
	#define SG_MAX_LO_Hawk_1024 ((sizeof tab_lo) / sizeof(uint64_t))
	
	size_t hi_len = SG_MAX_HI_Hawk_1024;
	size_t lo_len = SG_MAX_LO_Hawk_1024;

	size_t n = (size_t)1 << logn;
	
	uint32_t sn = 0;
	for (size_t u = 0; u < (n << 1); u += 16) {
		union {
			uint8_t b[160];
			uint16_t w[80];
			uint64_t q[20];
		} buf;
		rng(rng_context, buf.b, sizeof buf.b);
        
		for (size_t j = 0; j < 4; j ++) {
			for (size_t k = 0; k < 4; k ++) {
                trigger_high();
				size_t v = u + (j << 2) + k;
				uint64_t lo = dec64le(
					buf.b + (j << 3) + (k << 5));
				uint32_t hi = dec16le(
					buf.b + (j << 3) + 128 + (k << 1));
					
				/* Extract sign bit. */
				uint32_t neg = -(uint32_t)(lo >> 63);
				lo &= 0x7FFFFFFFFFFFFFFF;
				hi &= 0x7FFF;

				/* Use even or odd column depending on
				   parity of t. */
				uint32_t pbit = (t[v >> 3] >> (v & 7)) & 1;
				uint64_t p_odd = -(uint64_t)pbit;
				uint32_t p_oddw = (uint32_t)p_odd;

				uint32_t r = 0;
				for (size_t i = 0; i < hi_len; i += 2) {
					uint64_t tlo0 = tab_lo[i + 0];
					uint64_t tlo1 = tab_lo[i + 1];
					uint64_t tlo = tlo0
						^ (p_odd & (tlo0 ^ tlo1));
					uint32_t cc =
						(uint32_t)((lo - tlo) >> 63);
					uint32_t thi0 = tab_hi[i + 0];
					uint32_t thi1 = tab_hi[i + 1];
					uint32_t thi = thi0
						^ (p_oddw & (thi0 ^ thi1));
					r += (hi - thi - cc) >> 31;
				}
				uint32_t hinz = (hi - 1) >> 31;
				for (size_t i = hi_len; i < lo_len; i += 2) {
					uint64_t tlo0 = tab_lo[i + 0];
					uint64_t tlo1 = tab_lo[i + 1];
					uint64_t tlo = tlo0
						^ (p_odd & (tlo0 ^ tlo1));
					uint32_t cc =
						(uint32_t)((lo - tlo) >> 63);
					r += hinz & cc;
				}

				/* Multiply by 2 and apply parity. */
				r = (r << 1) - p_oddw;
				

				/* Apply sign bit. */
				r = (r ^ neg) - neg;

				x[v] = (int8_t)*(int32_t *)&r;
				sn += r * r;
                trigger_low();
                return sn;
			}
		}
       
	}
	return sn;
}

uint8_t get_pt(uint8_t* pt, uint8_t len)
{
    int8_t x[16] = {0};
    uint8_t t[2];
    volatile int res = 4;
    
    srand(pt[0]);
    t[0] = rand()%256;

    res = sig_gauss(3,&rng_gb,NULL, x, t);   

	simpleserial_put('r', 16, x);
	return 0x00;
}



int main(void)
{
    platform_init();
    init_uart();
    trigger_setup();

	simpleserial_init();

    simpleserial_addcmd('p', 16,  get_pt);

    while(1)
        simpleserial_get();
}
