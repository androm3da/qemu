#include <stdio.h>
#include <stdint.h>
typedef uint64_t u64; typedef int64_t i64; typedef uint32_t u32; typedef int32_t i32;
static u64 st = 0x9E3779B97F4A7C15ULL;
static u64 rnd(void){ st ^= st << 13; st ^= st >> 7; st ^= st << 17; return st; }
static u64 mix(u64 h, u64 v){ h ^= v; h *= 0x100000001B3ULL; h ^= h >> 29; return h; }
#define N 200000
#define CLASS(name, body) static u64 name(void){ u64 h = 1469598103934665603ULL; st = 0x9E3779B97F4A7C15ULL; for (int i = 0; i < N; i++){ u64 a = rnd(), b = rnd(); u32 s = (u32)(rnd() & 63); (void)a;(void)b;(void)s; body } return h; }
CLASS(add64,  h = mix(h, a + b); h = mix(h, a - b);)
CLASS(mul32,  h = mix(h, (u64)(u32)a * (u32)b); h = mix(h, (u64)(i64)((i64)(i32)a * (i32)b));)
CLASS(mul64,  h = mix(h, a * b);)
CLASS(shl,    h = mix(h, a << s); h = mix(h, (u64)((u32)a << (s & 31)));)
CLASS(lshr,   h = mix(h, a >> s); h = mix(h, (u64)((u32)a >> (s & 31)));)
CLASS(ashr,   h = mix(h, (u64)((i64)a >> s)); h = mix(h, (u64)(i64)((i32)a >> (s & 31)));)
CLASS(rot,    h = mix(h, (a << (s | 1)) | (a >> (64 - (s | 1)))); h = mix(h, (a >> (s | 1)) | (a << (64 - (s | 1))));)
CLASS(logic,  h = mix(h, a & b); h = mix(h, a | b); h = mix(h, a ^ b); h = mix(h, ~a & b);)
CLASS(cmp,    h = mix(h, (u64)(a < b)); h = mix(h, (u64)((i64)a < (i64)b)); h = mix(h, (u64)(a == b)); h = mix(h, (u64)((i64)a > (i64)(b >> 3)));)
CLASS(udiv,   u64 d = (b >> (s | 1)) | 1; h = mix(h, a / d); h = mix(h, a % d);)
CLASS(sdiv,   i64 d = (i64)((b >> (s | 1)) | 1); if (d == -1) d = 3; h = mix(h, (u64)((i64)a / d)); h = mix(h, (u64)((i64)a % d));)
CLASS(div32,  u32 d = ((u32)b >> (s & 15)) | 1; h = mix(h, (u32)a / d); h = mix(h, (u32)a % d); i32 sd = (i32)d; if (sd == -1) sd = 5; h = mix(h, (u64)(i64)((i32)a / sd));)
static u64 mulhi64(u64 a, u64 b){ u64 al=(u32)a, ah=a>>32, bl=(u32)b, bh=b>>32; u64 t=al*bl; u64 m1=ah*bl+(t>>32); u64 m2=al*bh+(u32)m1; return ah*bh+(m1>>32)+(m2>>32); }
CLASS(mulhi,  h = mix(h, mulhi64(a, b)); h = mix(h, a * b);)
CLASS(carry,  u64 lo = a + b; h = mix(h, lo < a); u64 br = a - b; h = mix(h, a < b); h = mix(h, br);)
CLASS(bits,   h = mix(h, (u64)__builtin_popcountll(a)); h = mix(h, (u64)__builtin_clzll(a | 1)); h = mix(h, (u64)__builtin_ctzll(a | (1ULL << 63))); h = mix(h, __builtin_bswap64(a)); h = mix(h, (u64)__builtin_bswap32((u32)a));)
CLASS(sat,    i64 x = (i64)a >> 1; i64 y = (i64)b >> 1; h = mix(h, (u64)(x + y)); h = mix(h, (u64)(x - y)); h = mix(h, (u64)((i32)(x >> 33) + (i32)(y >> 33)));)
CLASS(mem,    unsigned char buf[16]; for (int k = 0; k < 8; k++){ buf[k] = (unsigned char)(a >> (8*k)); buf[8+k] = (unsigned char)(b >> (8*k)); } h = mix(h, *(u64*)(buf + 1)); h = mix(h, *(u32*)(buf + 6)); h = mix(h, *(unsigned short*)(buf + 3));)
struct t { const char *n; u64 (*f)(void); };
static const struct t T[] = { {"add64",add64},{"mul32",mul32},{"mul64",mul64},{"shl",shl},{"lshr",lshr},{"ashr",ashr},{"rot",rot},{"logic",logic},{"cmp",cmp},{"udiv",udiv},{"sdiv",sdiv},{"div32",div32},{"mulhi",mulhi},{"carry",carry},{"bits",bits},{"sat",sat},{"mem",mem} };
int main(void){ for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) printf("%s %016llx\n", T[i].n, (unsigned long long)T[i].f()); return 0; }
