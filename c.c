#ifndef NDEBUG
    #define NDEBUG
#endif
#ifndef NDEBUG
    #include <assert.h>
#else
    #define assert(x) ((void)0)
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if __STDC_VERSION__ < 199901L
    #error "编译器需要设置为 c99 或更高标准"
#endif

#if defined __has_builtin
    #if __has_builtin(__builtin_addcll)
        #define addcarry(lhs, rhs, res, cf_in, cf_out) res = __builtin_addcll(lhs, rhs, cf_in, &cf_out);
    #endif

    #if __has_builtin(__builtin_subcll)
        #define subborrow(lhs, rhs, res, cf_in, cf_out) res = __builtin_subcll(lhs, rhs, cf_in, &cf_out);
    #endif
#endif

// #if !defined addcarry
//     #define addcarry(lhs, rhs, res, cf_in, cf_out)
//         cf_out = __builtin_ia32_addcarryx_u64(cf_in, lhs, rhs, &res);
// #endif

// #if !defined subborrow
//     #define subborrow(lhs, rhs, res, cf_in, cf_out)
//         cf_out = __builtin_ia32_sbb_u64(cf_in, lhs, rhs, &res);
// #endif

#ifndef addcarry
    #warning                                                                                                           \
        "编译器不支持 __builtin_addcll, 由于低版本 gcc 对类似 builtin 的错误优化, addcarry 正在使用软件实现, 将会造成性能问题"
    #define addcarry(lhs, rhs, res, cf_in, cf_out)                                                                     \
        {                                                                                                              \
            part_t sum = lhs + rhs + cf_in;                                                                            \
            cf_out = cf_in ? sum <= lhs : sum < lhs;                                                                   \
            res = sum;                                                                                                 \
        }
#endif

#ifndef subborrow
    #warning                                                                                                           \
        "编译器不支持 __builtin_subcll, 由于低版本 gcc 对类似 builtin 的错误优化, subborrow 正在使用软件实现, 将会造成性能问题"
    #define subborrow(lhs, rhs, res, cf_in, cf_out)                                                                    \
        {                                                                                                              \
            part_t diff = lhs - rhs - cf_in;                                                                           \
            cf_out = cf_in ? diff >= lhs : diff > lhs;                                                                 \
            res = diff;                                                                                                \
        }
#endif

typedef unsigned long long part_t;
typedef __uint128_t double_part_t;

// 1024-bit 无符号整数
typedef struct uint1024_t
{
    part_t part[1024 / 64];
} uint1024_t;

#define less_uintx_uint64(rdi, rsi, part, res)                                                                         \
    {                                                                                                                  \
        part_t x = 0;                                                                                                  \
        for (int i = 1; i < part; i++)                                                                                 \
        {                                                                                                              \
            x = x | rdi[i];                                                                                            \
        }                                                                                                              \
        res = (x == 0) & (rdi[0] < rsi);                                                                               \
    }

#define sub_uintx_uint64(rdi, rsi, part, rdx)                                                                          \
    {                                                                                                                  \
        part_t cf = 0;                                                                                                 \
        subborrow(rdi[0], rsi, rdx[0], cf, cf);                                                                        \
        for (int i = 1; i < part; i++)                                                                                 \
        {                                                                                                              \
            subborrow(rdi[i], 0, rdx[i], cf, cf);                                                                      \
        }                                                                                                              \
    }

// is_prime = (n0 & (n0 > 1)) ? isprime(n0) : n0 == 2;
int is_prime_simple(part_t num)
{
    assert(num >= 3 && (num & 1) != 0);

    part_t q;
    part_t res;
    part_t d;

    d = 3;
    do
    {
        q = num / d;
        res = num - q * d;
        if (q < d)
            return 1;
        d += 2;
    } while (res != 0);
    return 0;
}

#define count_trailing_zeros_uintx(num, part, res)                                                                     \
    {                                                                                                                  \
        assert(part < sizeof(res) * 8);                                                                                \
        res = 1 << part;                                                                                               \
        for (int i = 0; i < part; i++)                                                                                 \
        {                                                                                                              \
            res |= ((num[i] != 0)) << i;                                                                               \
        }                                                                                                              \
        int lf = __builtin_ctz(res);                                                                                   \
        int cnt;                                                                                                       \
        cnt = __builtin_ctzll(num[lf]);                                                                                \
        res = (lf * 64 + cnt);                                                                                         \
    }

#define count_leading_zeros_uintx(num, part, res)                                                                      \
    {                                                                                                                  \
        assert(part < sizeof(res) * 8);                                                                                \
        res = 1;                                                                                                       \
        for (int i = 0; i < part; i++)                                                                                 \
        {                                                                                                              \
            res = (res << 1) | ((num[i] != 0));                                                                        \
        }                                                                                                              \
        int lf = __builtin_ctz(res);                                                                                   \
        int cnt;                                                                                                       \
        cnt = __builtin_clzll(num[part - lf - 1]);                                                                     \
        res = (lf * 64 + cnt);                                                                                         \
    }

#define shr_1_uintx(rdi, part, rsi)                                                                                    \
    {                                                                                                                  \
        part_t last = 0, tmp;                                                                                          \
        for (int i = part - 1; i >= 0; i--)                                                                            \
        {                                                                                                              \
            tmp = rdi[i];                                                                                              \
            rsi[i] = (tmp >> 1) | last;                                                                                \
            last = tmp << 63;                                                                                          \
        }                                                                                                              \
    }

#define less_uintx_uintx(lhs, rhs, part, res)                                                                          \
    {                                                                                                                  \
        part_t cf = 0;                                                                                                 \
        __attribute__((unused)) part_t tmp;                                                                            \
        for (int i = 0; i < part; i++)                                                                                 \
        {                                                                                                              \
            subborrow(lhs[i], rhs[i], tmp, cf, cf);                                                                    \
        }                                                                                                              \
        res = cf;                                                                                                      \
    }

#define equal_uintx_uintx(lhs, rhs, part, res)                                                                         \
    {                                                                                                                  \
        res = (__builtin_memcmp(lhs, rhs, part * sizeof(part_t)) == 0);                                                \
    }

#define last_digit_index_uintx(rdi, part, res)                                                                         \
    {                                                                                                                  \
        res = 0;                                                                                                       \
        for (int i = 0; i < part; i++)                                                                                 \
        {                                                                                                              \
            if (rdi[i] != 0)                                                                                           \
            {                                                                                                          \
                res = i;                                                                                               \
            }                                                                                                          \
        }                                                                                                              \
    }

#define add_uintx_uint64(rdi, rsi, part, rdx)                                                                          \
    {                                                                                                                  \
        part_t cf = 0;                                                                                                 \
        addcarry(rdi[0], rsi, rdx[0], cf, cf);                                                                         \
        for (int i = 1; i < part; i++)                                                                                 \
        {                                                                                                              \
            addcarry(rdi[i], 0, rdx[i], cf, cf);                                                                       \
        }                                                                                                              \
    }

#define div_uintx_uint64(rdi, rsi, part, rdx)                                                                          \
    {                                                                                                                  \
        double_part_t lhs, res;                                                                                        \
        part_t re;                                                                                                     \
        lhs = ((double_part_t)rdi[part - 1]);                                                                          \
        res = lhs / rsi;                                                                                               \
        rdx[part - 1] = res;                                                                                           \
        re = lhs - res * rsi;                                                                                          \
        for (int i = part - 2; i >= 0; i--)                                                                            \
        {                                                                                                              \
            lhs = (double_part_t)re << 64 | rdi[i];                                                                    \
            res = lhs / rsi;                                                                                           \
            rdx[i] = res;                                                                                              \
            re = lhs - res * rsi;                                                                                      \
        }                                                                                                              \
    }

#define sub_uintx_uintx(rdi, rsi, part, rdx)                                                                           \
    {                                                                                                                  \
        part_t cf = 0;                                                                                                 \
        for (int i = 0; i < part; i++)                                                                                 \
        {                                                                                                              \
            subborrow(rdi[i], rsi[i], rdx[i], cf, cf);                                                                 \
        }                                                                                                              \
    }

#define div_uintx_uintx(rdi, rsi, part, rdx)                                                                           \
    {                                                                                                                  \
        assert(rdi != rdx && rsi != rdx);                                                                              \
        /* The Art of Computer Programming Volume 2 by Donald Knuth, Section 4.3.1, Algorithm D */                     \
        __builtin_memset(rdx, 0, part * sizeof(part_t));                                                               \
        uint8_t res1, res2;                                                                                            \
        less_uintx_uintx(rdi, rsi, part, res1);                                                                        \
        equal_uintx_uintx(rdi, rsi, part, res2);                                                                       \
        if (res1)                                                                                                      \
        {}                                                                                                             \
        else if (res2)                                                                                                 \
        {                                                                                                              \
            rdx[0] = 1;                                                                                                \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            int n;                                                                                                     \
            last_digit_index_uintx(rsi, part, n);                                                                      \
            n++;                                                                                                       \
            if (n == 1)                                                                                                \
            {                                                                                                          \
                div_uintx_uint64(rdi, rsi[0], part, rdx);                                                              \
            }                                                                                                          \
            else                                                                                                       \
            {                                                                                                          \
                int m;                                                                                                 \
                last_digit_index_uintx(rdi, part, m);                                                                  \
                m = m + 1 - n;                                                                                         \
                                                                                                                       \
                int shift = __builtin_clzll(rsi[n - 1]);                                                               \
                part_t v[part];                                                                                        \
                __builtin_memset(v, 0, sizeof(part_t) * part);                                                         \
                /* left shift */                                                                                       \
                {                                                                                                      \
                    part_t digit_shift = shift >> 6;                                                                   \
                    part_t bit_shift = shift & 63;                                                                     \
                    if (bit_shift != 0)                                                                                \
                    {                                                                                                  \
                        part_t carry_shift = 64 - bit_shift;                                                           \
                        part_t carry = 0, tmp;                                                                         \
                        for (int i = digit_shift; i < part; i++)                                                       \
                        {                                                                                              \
                            tmp = rsi[i - digit_shift];                                                                \
                            v[i] = (tmp << bit_shift) | carry;                                                         \
                            carry = (tmp >> carry_shift);                                                              \
                        }                                                                                              \
                    }                                                                                                  \
                    else                                                                                               \
                    {                                                                                                  \
                        for (int i = digit_shift; i < part; i++)                                                       \
                        {                                                                                              \
                            v[i] = rsi[i - digit_shift];                                                               \
                        }                                                                                              \
                    }                                                                                                  \
                }                                                                                                      \
                                                                                                                       \
                part_t v_n_m1 = v[n - 1];                                                                              \
                part_t v_n_m2 = v[n - 2];                                                                              \
                                                                                                                       \
                part_t u[part + 1];                                                                                    \
                u[0] = rdi[0] << shift;                                                                                \
                __builtin_memset(u + 1, 0, sizeof(part_t) * part);                                                     \
                /* right shift */                                                                                      \
                {                                                                                                      \
                    part_t cnt = 64 - shift;                                                                           \
                    part_t* out = u + 1;                                                                               \
                                                                                                                       \
                    part_t digit_shift = cnt >> 6;                                                                     \
                    part_t bit_shift = cnt & 63;                                                                       \
                    if (bit_shift != 0)                                                                                \
                    {                                                                                                  \
                        part_t carry_shift = 64 - bit_shift;                                                           \
                        part_t carry = 0, tmp, index;                                                                  \
                        for (int i = digit_shift; i < part; i++)                                                       \
                        {                                                                                              \
                            index = part - 1 - i;                                                                      \
                            tmp = rdi[index + digit_shift];                                                            \
                            out[index] = (tmp >> bit_shift) | carry;                                                   \
                            carry = (tmp << carry_shift);                                                              \
                        }                                                                                              \
                    }                                                                                                  \
                    else                                                                                               \
                    {                                                                                                  \
                        for (int i = digit_shift; i < part; i++)                                                       \
                        {                                                                                              \
                            out[i - digit_shift] = rdi[i];                                                             \
                        }                                                                                              \
                    }                                                                                                  \
                }                                                                                                      \
                double_part_t tmp128;                                                                                  \
                part_t tmp_part_a1[part + 1];                                                                          \
                part_t j = m + 1;                                                                                      \
                part_t u_jn, q_hat, r_hat, carry;                                                                      \
                while (j > 0)                                                                                          \
                {                                                                                                      \
                    j--;                                                                                               \
                                                                                                                       \
                    u_jn = u[j + n];                                                                                   \
                                                                                                                       \
                    if (u_jn < v_n_m1)                                                                                 \
                    {                                                                                                  \
                        tmp128 = ((double_part_t)u_jn << 64) + u[j + n - 1];                                           \
                        q_hat = tmp128 / v_n_m1;                                                                       \
                        r_hat = tmp128 - q_hat * v_n_m1;                                                               \
                                                                                                                       \
                        tmp128 = (double_part_t)q_hat * v_n_m2;                                                        \
                        if (tmp128 > (((double_part_t)r_hat << 64) + u[j + n - 2]))                                    \
                        {                                                                                              \
                            q_hat--;                                                                                   \
                            addcarry(r_hat, v_n_m1, r_hat, 0, carry);                                                  \
                            if (!carry)                                                                                \
                            {                                                                                          \
                                tmp128 = (double_part_t)q_hat * v_n_m2;                                                \
                                if (tmp128 > (((double_part_t)r_hat << 64) + u[j + n - 2]))                            \
                                {                                                                                      \
                                    q_hat--;                                                                           \
                                }                                                                                      \
                            }                                                                                          \
                        }                                                                                              \
                    }                                                                                                  \
                    else                                                                                               \
                    {                                                                                                  \
                        q_hat = ((part_t) - 1);                                                                        \
                    }                                                                                                  \
                    /* tmp initialize */                                                                               \
                    {                                                                                                  \
                        __builtin_memset(tmp_part_a1, 0, sizeof(part_t) * (part + 1));                                 \
                        carry = 0;                                                                                     \
                        for (int i = 0; i < part; i++)                                                                 \
                        {                                                                                              \
                            tmp128 = ((double_part_t)q_hat) * (v[i]) + carry;                                          \
                            carry = tmp128 >> 64;                                                                      \
                            tmp_part_a1[i] = tmp128;                                                                   \
                        }                                                                                              \
                        tmp_part_a1[part] = carry;                                                                     \
                    }                                                                                                  \
                    part_t tmp64 = 0, sub;                                                                             \
                    /* sub assign : u -= (tmp, j, n) */                                                                \
                    {                                                                                                  \
                        /* tmp64 : borrow */                                                                           \
                        for (int i = 0; i <= n; i++)                                                                   \
                        {                                                                                              \
                            subborrow(u[i + j], tmp_part_a1[i], sub, tmp64, tmp64);                                    \
                            u[i + j] = sub;                                                                            \
                        }                                                                                              \
                    }                                                                                                  \
                    if (tmp64)                                                                                         \
                    {                                                                                                  \
                        q_hat--;                                                                                       \
                        /* add assign : u += (v, j, n) */                                                              \
                        {                                                                                              \
                            /* tmp64 : sum */                                                                          \
                            carry = 0;                                                                                 \
                            for (int i = 0; i < n; i++)                                                                \
                            {                                                                                          \
                                addcarry(u[i + j], v[i], tmp64, carry, carry);                                         \
                                u[i + j] = tmp64;                                                                      \
                            }                                                                                          \
                            if (carry)                                                                                 \
                            {                                                                                          \
                                u[n + j] = u[n + j] + 1;                                                               \
                            }                                                                                          \
                        }                                                                                              \
                    }                                                                                                  \
                    rdx[j] = q_hat;                                                                                    \
                }                                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
    }

#define equal_uintx_uint64(rdi, rsi, part, res)                                                                        \
    {                                                                                                                  \
        part_t x = 0;                                                                                                  \
        for (int i = 1; i < part; i++)                                                                                 \
        {                                                                                                              \
            x = x | rdi[i];                                                                                            \
        }                                                                                                              \
        res = (x == 0) & (rdi[0] == rsi);                                                                              \
    }

#define mul_uintx_uinty(rdi, rsi, partx, party, rdx)                                                                   \
    {                                                                                                                  \
        assert((void*)rdi != (void*)rdx && (void*)rsi != (void*)rdx);                                                  \
        assert(party >= partx);                                                                                        \
        __builtin_memset(rdx, 0, sizeof(part_t) * (party));                                                            \
        part_t carry, high, low, res0, c, mult;                                                                        \
        double_part_t prod;                                                                                            \
        for (int i = 0; i < partx; i++)                                                                                \
        {                                                                                                              \
            carry = 0;                                                                                                 \
            mult = rdi[i];                                                                                             \
            for (int j = i; j < party; j++)                                                                            \
            {                                                                                                          \
                prod = ((double_part_t)mult) * (rsi[j - i]);                                                           \
                high = prod >> 64;                                                                                     \
                low = prod;                                                                                            \
                                                                                                                       \
                res0 = rdx[j];                                                                                         \
                                                                                                                       \
                low = res0 + low;                                                                                      \
                c = res0 > low;                                                                                        \
                                                                                                                       \
                high = high + c;                                                                                       \
                                                                                                                       \
                res0 = low + carry;                                                                                    \
                c = low > res0;                                                                                        \
                                                                                                                       \
                carry = high + c;                                                                                      \
                rdx[j] = res0;                                                                                         \
            }                                                                                                          \
        }                                                                                                              \
    }

#define mul_uintx_uintx(rdi, rsi, part, rdx)                                                                           \
    {                                                                                                                  \
        assert((void*)rdi != (void*)rdx && (void*)rsi != (void*)rdx);                                                  \
        __builtin_memset(rdx, 0, sizeof(part_t) * part);                                                               \
        part_t carry, high, low, res0, c, mult;                                                                        \
        double_part_t prod;                                                                                            \
        for (int i = 0; i < part; i++)                                                                                 \
        {                                                                                                              \
            carry = 0;                                                                                                 \
            mult = rdi[i];                                                                                             \
            for (int j = i; j < part; j++)                                                                             \
            {                                                                                                          \
                prod = ((double_part_t)mult) * (rsi[j - i]);                                                           \
                high = prod >> 64;                                                                                     \
                low = prod;                                                                                            \
                                                                                                                       \
                res0 = rdx[j];                                                                                         \
                                                                                                                       \
                low = res0 + low;                                                                                      \
                c = res0 > low;                                                                                        \
                                                                                                                       \
                high = high + c;                                                                                       \
                                                                                                                       \
                res0 = low + carry;                                                                                    \
                c = low > res0;                                                                                        \
                                                                                                                       \
                carry = high + c;                                                                                      \
                rdx[j] = res0;                                                                                         \
            }                                                                                                          \
        }                                                                                                              \
    }

#define mul_uintx_uint64(rdi, rsi, part, rdx)                                                                          \
    {                                                                                                                  \
        part_t carry = 0;                                                                                              \
        double_part_t prod;                                                                                            \
        for (int i = 0; i < part; i++)                                                                                 \
        {                                                                                                              \
            prod = ((double_part_t)rdi[i]) * rsi + carry;                                                              \
            carry = prod >> 64;                                                                                        \
            rdx[i] = prod;                                                                                             \
        }                                                                                                              \
    }

#define add_uintx_uinty(rdi, rsi, partx, party, rdx)                                                                   \
    {                                                                                                                  \
        assert(party >= partx);                                                                                        \
        part_t cf = 0;                                                                                                 \
        for (int i = 0; i < partx; i++)                                                                                \
        {                                                                                                              \
            addcarry(rdi[i], rsi[i], rdx[i], cf, cf);                                                                  \
        }                                                                                                              \
        for (int i = partx; i < party; i++)                                                                            \
        {                                                                                                              \
            addcarry(rsi[i], 0, rdx[i], cf, cf);                                                                       \
        }                                                                                                              \
    }

#define mod_uintx_uintx(rdi, rsi, part, rdx)                                                                           \
    {                                                                                                                  \
        assert(rdi != rdx && rsi != rdx);                                                                              \
        part_t div_res[part];                                                                                          \
        div_uintx_uintx(rdi, rsi, part, div_res);                                                                      \
        part_t mul_res[part];                                                                                          \
        mul_uintx_uintx(div_res, rsi, part, mul_res);                                                                  \
        sub_uintx_uintx(rdi, mul_res, part, rdx);                                                                      \
    }

#define bit_at_uintx(num, bit, part, res)                                                                              \
    {                                                                                                                  \
        assert(bit < part * 64);                                                                                       \
        res = ((num[bit / 64]) >> (bit % 64)) & 1;                                                                     \
    }

static inline uint64_t rotl(const uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

static uint64_t s[4];

uint64_t next(void)
{
    const uint64_t result = rotl(s[0] + s[3], 23) + s[0];

    const uint64_t t = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];

    s[2] ^= t;

    s[3] = rotl(s[3], 45);

    return result;
}

void rng_nbits(part_t* res, int bits)
{
    while (bits >= 64)
    {
        *res = next();
        bits -= 64;
        res++;
    }
    if (bits > 0)
    {
        *res = next() & ((1ULL << bits) - 1);
    }
}

part_t inv(uint1024_t const* n)
{
    part_t R[1088 / 64];
    part_t Rp[1088 / 64];
    part_t N2176[2176 / 64];
    part_t Np[2176 / 64];
    __builtin_memset(N2176, 0, sizeof(N2176));
    __builtin_memcpy(N2176, n->part, sizeof(uint1024_t));
    __builtin_memset(R, 0, sizeof(R));
    __builtin_memset(Rp, 0, sizeof(Rp));
    R[1024 / 64] = 1;  // R = 2 ^ 1024

    part_t newt[1152 / 64];
    part_t t[1152 / 64];
    part_t mul[1152 / 64];
    part_t tmp[1152 / 64];
    __builtin_memset(&newt, 0, sizeof(newt));
    __builtin_memset(&t, 0, sizeof(t));
    newt[0] = 1;
    part_t r[1088 / 64];
    part_t newr[1088 / 64];
    part_t q[1088 / 64];
    __builtin_memcpy(newr, R, sizeof(R));
    r[1088 / 64 - 1] = 0;
    __builtin_memcpy(r, n, sizeof(uint1024_t));
    int res;
    for (;;)
    {
        equal_uintx_uint64(newr, 0, 1088 / 64, res);
        if (res)
        {
            break;
        }

        div_uintx_uintx(r, newr, 1088 / 64, q);

        __builtin_memcpy(&tmp, &newt, sizeof(newt));
        mul_uintx_uinty(q, newt, 1088 / 64, 1152 / 64, mul);
        sub_uintx_uintx(t, mul, 1152 / 64, newt);
        __builtin_memcpy(&t, &tmp, sizeof(t));

        __builtin_memcpy(&tmp, &newr, sizeof(newr));
        mul_uintx_uintx(q, newr, 1088 / 64, mul);
        sub_uintx_uintx(r, mul, 1088 / 64, newr);
        __builtin_memcpy(&r, &tmp, sizeof(r));
    }
#ifndef NDEBUG
    less_uintx_uint64(r, 2, 1024 / 64, res);
    assert(res);
#endif
    if (t[1152 / 64 - 1] >= (1uLL << 63))
    {
        add_uintx_uinty(n->part, t, 1024 / 64, 1152 / 64, t);
    }
    __builtin_memcpy(Rp, &t, sizeof(Rp));
    part_t mul_res[2176 / 64];
    __builtin_memset(mul_res, 0, sizeof(mul_res));
    __builtin_memcpy(mul_res + 1024 / 64, Rp, sizeof(Rp));
    sub_uintx_uint64(mul_res, 1, 2176 / 64, mul_res);
    div_uintx_uintx(mul_res, N2176, 2176 / 64, Np);
    return Np[0];
}

uint8_t equal_uint1024_uint1024_minus_1(uint1024_t const* rdi, uint1024_t const* rsi)
{
    assert(rsi->part[0] & 1);

    if (rdi->part[0] != rsi->part[0] - 1)
        return 0;
    else
    {
        return __builtin_memcmp(rdi->part + 1, rsi->part + 1, sizeof(uint1024_t) - sizeof(part_t)) == 0;
    }
}

void monpro_iifios_64(uint1024_t const* a, uint1024_t const* b, uint1024_t const* n, uint1024_t* t, uint64_t n0)
{
    assert(a != t && b != t && n != t);
    __builtin_memset(t, 0, sizeof(uint1024_t));
    int ls = 16;
    part_t r0;
    part_t r1;
    part_t s;
    part_t ts = 0;
    part_t c;
    for (int i = 0; i <= ls - 1; i++)
    {
        double_part_t tmp = (double_part_t)t->part[0] + ((double_part_t)a->part[0]) * b->part[i];
        r0 = tmp >> 64;
        s = (part_t)tmp;

        part_t m = (part_t)(((double_part_t)s) * n0);

        tmp = s + ((double_part_t)m) * n->part[0];
        r1 = tmp >> 64;
        // 未使用, 但为了和论文保持一致
        // s = tmp;
        for (int j = 1; j <= ls - 1; j++)
        {
            tmp = t->part[j] + ((double_part_t)a->part[j]) * b->part[i] + r0;
            r0 = tmp >> 64;
            s = (part_t)tmp;

            tmp = s + ((double_part_t)m) * n->part[j] + r1;
            r1 = tmp >> 64;
            s = (part_t)tmp;

            t->part[j - 1] = s;
        }
        tmp = ((double_part_t)r0) + r1;
        r0 = tmp >> 64;
        s = (part_t)tmp;

        tmp = ((double_part_t)ts) + s;
        r1 = tmp >> 64;
        s = (part_t)tmp;

        t->part[ls - 1] = s;
        ts = r0 + r1;
    }

    less_uintx_uintx(n->part, t->part, 1024 / 64, c);
    if (ts > 0 || c)
    {
        sub_uintx_uintx(t->part, n->part, 1024 / 64, t->part);
    }
}

void modexp_uint1024(uint1024_t* a, uint1024_t const* n, uint1024_t const* mod, uint64_t n0, uint1024_t* res)
{
    part_t tmp1[2048 / 64];
    part_t mod2048[2048 / 64];
    part_t tmp2[2048 / 64];

    __builtin_memset(mod2048, 0, sizeof(mod2048));
    __builtin_memcpy(mod2048, mod, sizeof(uint1024_t));

    // a = a ^ (2^1024) % mod
    __builtin_memset(tmp1, 0, sizeof(tmp1));
    __builtin_memcpy(tmp1 + 1024 / 64, a, sizeof(uint1024_t));
    mod_uintx_uintx(tmp1, mod2048, 2048 / 64, tmp2);
    __builtin_memset(a, 0, sizeof(uint1024_t));
    __builtin_memcpy(a, tmp2, sizeof(uint1024_t));

    // res = 1 ^ (2^1024) % mod
    __builtin_memset(tmp1, 0, sizeof(tmp1));
    tmp1[1024 / 64] = 1;
    mod_uintx_uintx(tmp1, mod2048, 2048 / 64, tmp2);
    __builtin_memset(res, 0, sizeof(uint1024_t));
    __builtin_memcpy(res, tmp2, sizeof(uint1024_t));

    int bit;
    int bit_count;
    count_leading_zeros_uintx(n->part, 1024 / 64, bit_count);
    bit_count = 1024 - bit_count;
    for (int i = 0; i < bit_count; i++)
    {
        bit_at_uintx(n->part, i, 1024 / 64, bit);
        if (bit == 1)
        {
            monpro_iifios_64(res, a, mod, (uint1024_t*)tmp2, n0);
            __builtin_memcpy(res, &tmp2, sizeof(uint1024_t));
        }
        monpro_iifios_64(a, a, mod, (uint1024_t*)tmp2, n0);
        __builtin_memcpy(a, tmp2, sizeof(uint1024_t));
    }
    __builtin_memset(tmp1, 0, sizeof(tmp1));
    tmp1[0] = 1;
    monpro_iifios_64(res, (uint1024_t*)tmp1, mod, (uint1024_t*)tmp2, n0);
    __builtin_memcpy(res, tmp2, sizeof(uint1024_t));
}

void sqrtm(uint1024_t const* a, uint1024_t const* mod, uint1024_t* res)
{
    part_t a2048[2048 / 64];
    part_t tmp1[2048 / 64];
    part_t m2048[2048 / 64];
    __builtin_memset(a2048, 0, sizeof(a2048));
    __builtin_memcpy(a2048, a, sizeof(uint1024_t));
    __builtin_memset(m2048, 0, sizeof(m2048));
    __builtin_memcpy(m2048, mod, sizeof(uint1024_t));

    mul_uintx_uintx(a2048, a2048, 2048 / 64, tmp1);
    mod_uintx_uintx(tmp1, m2048, 2048 / 64, a2048);
    __builtin_memcpy(res, a2048, sizeof(uint1024_t));
}

int miller_rabin_1(uint1024_t const* n, uint1024_t* x, uint1024_t const* q, int k, uint64_t n0)
{
    uint1024_t y;
    modexp_uint1024(x, q, n, n0, &y);
    int res;
    equal_uintx_uint64(y.part, 1, 1024 / 64, res);
    if (res || equal_uint1024_uint1024_minus_1(&y, n))
        return 1;

    for (int i = 1; i < k; ++i)
    {
        sqrtm(&y, n, &y);
        if (equal_uint1024_uint1024_minus_1(&y, n))
            return 1;
    }
    return 0;
}

int miller_rabin(uint1024_t const* n, int k)
{
    int res;
    less_uintx_uint64(n->part, 1000000L, 1024 / 64, res);
    if (res)
    {
        int is_prime;
        part_t n0 = n->part[0];
        is_prime = (n0 & (n0 > 1)) ? is_prime_simple(n0) : n0 == 2;
        return is_prime ? 2 : 0;
    }

    if ((n->part[0] & 1) == 0)
    {
        return 0;
    }
    uint1024_t u;
    sub_uintx_uint64(n->part, 1, 1024 / 64, u.part);
    int t;
    count_trailing_zeros_uintx(u.part, 1024 / 64, t);

    int n_bits;
    uint1024_t n0;
    shr_1_uintx(n->part, 1024 / 64, n0.part);
    sub_uintx_uint64(n0.part, 2, 1024 / 64, n0.part);

    count_leading_zeros_uintx(n0.part, 1024 / 64, n_bits);
    n_bits = 64 * 16 - n_bits;

    uint64_t ninv = inv(n);

    uint1024_t x;

    for (int i = 0; i < k; ++i)
    {
        __builtin_memset(&x, 0, sizeof(x));
        rng_nbits(x.part, n_bits);
        less_uintx_uintx(x.part, n->part, 1024 / 64, res);
        if (!res)
        {
            sub_uintx_uintx(x.part, n->part, 1024 / 64, x.part);
        }
        add_uintx_uint64(x.part, 3, 1024 / 64, x.part);
        if (miller_rabin_1(n, &x, &u, t, ninv) == 0)
        {
            return 0;
        }
    }
    return 1;
}

#define mod_uintx_uint64(num, mod, parts, res)                                                                         \
    {                                                                                                                  \
        assert(mod != 0);                                                                                              \
        res = 0;                                                                                                       \
        for (int i = parts - 1; i >= 0; i--)                                                                           \
        {                                                                                                              \
            res = (((double_part_t)res << 64) + num[i]) % mod;                                                         \
        }                                                                                                              \
    }

static const uint16_t primes[] = {
    2,   3,   5,   7,   11,  13,  17,  19,  23,  29,  31,  37,  41,  43,  47,  53,  59,  61,  67,  71,  73,  79,
    83,  89,  97,  101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193,
    197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317,
    331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457,
    461, 463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541, 547, 557, 563, 569, 571, 577, 587, 593, 599, 601,
    607, 613, 617, 619, 631, 641, 643, 647, 653, 659, 661, 673, 677, 683, 691, 701, 709, 719};

void probable_prime(uint1024_t* rnd, uint16_t* mods, int bits)
{
    part_t delta;
    part_t maxdelta = (part_t)(-1) - primes[127];

again:
    __builtin_memset(rnd, 0, sizeof(uint1024_t));
    rng_nbits(rnd->part, bits);
    rnd->part[0] |= 1;
    for (int i = 1; i < 127; i++)
    {
        part_t mod;
        mod_uintx_uint64(rnd->part, primes[i], 1024 / 64, mod);
        mods[i] = (uint16_t)mod;
    }
    delta = 0;
loop:
    for (int i = 1; i < 127; i++)
    {
        if ((mods[i] + delta) % primes[i] == 0)
        {
            delta += 2;
            if (delta > maxdelta)
                goto again;
            goto loop;
        }
    }
    add_uintx_uint64(rnd->part, delta, 1024 / 64, rnd->part);
}

void generate(uint1024_t* rnd, int bits)
{
    assert(32 <= bits && bits <= 1024);
    uint16_t mods[128];
    do
    {
        probable_prime(rnd, mods, bits);
    } while (!miller_rabin(rnd, 8));
}

void print_uint1024(uint1024_t const* number)
{
    uint1024_t n = *number;
    int res;
    int mod;
    int index = 0;
    char buf[400];
    for (;;)
    {
        equal_uintx_uint64(n.part, 0, 1024 / 64, res);
        if (res)
        {
            break;
        }
        mod_uintx_uint64(n.part, 10, 1024 / 64, mod);
        div_uintx_uint64(n.part, 10, 1024 / 64, n.part);
        buf[index++] = '0' + (char)mod;
    }
    while (index > 0)
    {
        printf("%c", buf[--index]);
    }
}

int read_uint1024(uint1024_t* number, char const* str)
{
    __builtin_memset(number, 0, sizeof(uint1024_t));
    int len = (int)__builtin_strlen(str);
    for (int i = 0; i < len; i++)
    {
        if (str[i] < '0' || str[i] > '9')
        {
            printf("无法识别字符 %c\n", str[i]);
            return 1;
        }
        uint64_t digit = str[i] - '0';
        mul_uintx_uint64(number->part, 10, 1024 / 64, number->part);
        add_uintx_uint64(number->part, digit, 1024 / 64, number->part);
    }
    return 0;
}

void generate_main(int bits)
{
    if (!(32 <= bits && bits <= 1024))
    {
        printf("bits 应该 >= 32 且 <= 1024\n");
        return;
    }
    uint1024_t n;
    generate(&n, bits);
    print_uint1024(&n);
    printf("\n");
}

void validate_main(char const* str)
{
    uint1024_t n;
    if (read_uint1024(&n, str))
    {
        return;
    }
    int res = miller_rabin(&n, 8);
    // 这里不直接输出 str, 因为低版本 gcc 错误优化 builtin adc/sbb 可能导致读入错误, 输出 n 可以排查问题
    if (res == 1)
    {
        print_uint1024(&n);
        printf(" \033[33m可能是素数\033[0m\n");
    }
    else if (res == 2)
    {
        print_uint1024(&n);
        printf(" \033[32m是素数\033[0m\n");
    }
    else
    {
        print_uint1024(&n);
        printf(" \033[31m不是素数\033[0m\n");
    }
}

void build_info(void)
{
    printf("build: %s with ", __TIMESTAMP__);

#if defined(__MINGW64__)
    printf("mingw64 ");
#elif defined(__MINGW32__)
    printf("mingw32 ");
#endif

#if defined(__INTEL_COMPILER)
    #undef CXX_INTEL
    printf("icc %d.%d.%d", __INTEL_COMPILER / 100, (__INTEL_COMPILER / 10) % 10, __INTEL_COMPILER % 10);
#elif defined(_MSC_VER) && defined(_MSC_FULL_VER)
    #if _MSC_VER == _MSC_FULL_VER / 10000
    printf("msvc %d.%d.%d", _MSC_VER / 100, _MSC_VER % 100, _MSC_FULL_VER % 10000);
    #else
    printf("msvc %d.%d.%d.%d", _MSC_VER / 100, _MSC_VER % 100, _MSC_FULL_VER / 100000, _MSC_FULL_VER % 100000);
    #endif
#elif defined(__clang__) && defined(__clang_minor__)
    printf("clang %d.%d.%d", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__) && defined(__GNUC_MINOR__) && defined(__GNUC_PATCHLEVEL__)
    printf("gcc %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    printf("unknown compiler");
#endif

    printf("\n");
}

void usage(char* executable)
{
    printf("usage: %s <command> [options]\n", executable);
    printf("\n");
    printf("commands:\n");
    printf("  g, generate <bits>   生成一个随机数, \033[33m可能\033[0m是素数\n");
    printf("  v, validate <number> 验证一个数字是否\033[33m可能\033[0m是素数\n");
    printf("  *可能: 概率在 99.99%% 以上\n");
    printf("\n");
    printf("options:\n");
    printf("  -s, --openssl        所以为什么不直接用 openssl 呢\n");
    printf("\n");
    build_info();
    printf("\n");
}

int rand_init(void)
{
#if defined(__linux__)
    FILE* rng = fopen("/dev/urandom", "rb");
    if (rng == NULL)
    {
        perror("无法打开 /dev/urandom");
        return 1;
    }
    unsigned long code = fread(s, sizeof(s), 1, rng);
    if (code != 1)
    {
        if (feof(rng))
        {
            printf("无法读取 /dev/urandom: EOF\n");
        }
        else if (ferror(rng))
        {
            perror("无法读取 /dev/urandom");
        }
        fclose(rng);
        return 1;
    }
    fclose(rng);
    return 0;
#else
    return 1;
#endif
}

void rand_init_fallback(void)
{
    srand((unsigned int)time(NULL));
    s[0] = rand();
    s[1] = rand();
    s[2] = rand();
    s[3] = rand();
}

int main(int argc, char* argv[])
{
    if (rand_init() != 0)
    {
        rand_init_fallback();
    }

    char* executable = argv[0];

    char generate_str[] = "generate";
    char generate_short_str[] = "g";
    char validate_str[] = "validate";
    char validate_short_str[] = "v";
    char openssl_str[] = "--openssl";
    char openssl_short_str[] = "-s";
    if (argc < 3)
    {
        usage(executable);
        return 0;
    }
    int use_openssl = 0;
    for (int i = 3; i < argc; i++)
    {
        if (__builtin_strcmp(argv[i], openssl_str) == 0)
        {
            use_openssl = 1;
        }
        else if (__builtin_strcmp(argv[i], openssl_short_str) == 0)
        {
            use_openssl = 1;
        }
        else
        {
            printf("未知选项 %s\n", argv[i]);
            usage(executable);
            return 0;
        }
    }
    if (__builtin_strcmp(argv[1], generate_str) == 0 || __builtin_strcmp(argv[1], generate_short_str) == 0)
    {
        if (use_openssl)
        {
            char openssl_cmd[] = "openssl prime -generate -bits ";
            unsigned long openssl_cmd_len = __builtin_strlen(openssl_cmd);
            char* buf = __builtin_malloc(openssl_cmd_len + __builtin_strlen(argv[2]) + 1);
            __builtin_memcpy(buf, openssl_cmd, openssl_cmd_len);
            __builtin_memcpy(buf + openssl_cmd_len, argv[2], __builtin_strlen(argv[2]) + 1);
            int status = system(buf);
            if (status != 0)
            {
                printf("openssl 调用失败\n");
            }
            __builtin_free(buf);
        }
        else
        {
            generate_main(atoi(argv[2]));
        }
    }
    else if (__builtin_strcmp(argv[1], validate_str) == 0 || __builtin_strcmp(argv[1], validate_short_str) == 0)
    {
        if (use_openssl)
        {
            char openssl_cmd[] = "openssl prime ";
            unsigned long openssl_cmd_len = __builtin_strlen(openssl_cmd);
            char* buf = __builtin_malloc(openssl_cmd_len + __builtin_strlen(argv[2]) + 1);
            __builtin_memcpy(buf, openssl_cmd, openssl_cmd_len);
            __builtin_memcpy(buf + openssl_cmd_len, argv[2], __builtin_strlen(argv[2]) + 1);
            int status = system(buf);
            if (status != 0)
            {
                printf("openssl 调用失败\n");
            }
            __builtin_free(buf);
        }
        else
        {
            validate_main(argv[2]);
        }
    }
    else
    {
        printf("未知命令 %s\n", argv[1]);
        usage(executable);
    }
    return 0;
}
