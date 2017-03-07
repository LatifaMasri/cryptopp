// rdrand.cpp - written and placed in public domain by Jeffrey Walton and Uri Blumenthal.

#include "pch.h"
#include "config.h"
#include "cryptlib.h"
#include "secblock.h"
#include "rdrand.h"
#include "cpu.h"

#include <iostream>

#if CRYPTOPP_MSC_VERSION
# pragma warning(disable: 4100)
#endif

// This file (and friends) provides both RDRAND and RDSEED. They were added at
//   Crypto++ 5.6.3. At compile time, it uses CRYPTOPP_BOOL_{X86|X32|X64}
//   to select an implementation or "throw NotImplemented". At runtime, the
//   class uses the result of CPUID to determine if RDRAND or RDSEED are
//   available. If not available, then a SIGILL will result.
// The original classes accepted a retry count. Retries were superflous for
//   RDRAND, and RDSEED encountered a failure about 1 in 256 bytes depending
//   on the processor. Retries were removed at Crypto++ 6.0 because the
//   functions always fulfill the request.

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

// For Linux, install NASM, run rdrand-nasm.asm, add the apppropriate
// object file to the Makefile's LIBOBJS (rdrand-x{86|32|64}.o). After
// that, define these. They are not enabled by default because they
// are not easy to cut-in in the Makefile.

#if 0
#define NASM_RDRAND_ASM_AVAILABLE 1
#define NASM_RDSEED_ASM_AVAILABLE 1
#endif

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

#if defined(CRYPTOPP_CPUID_AVAILABLE)
# if defined(CRYPTOPP_MSC_VERSION)
#  if (CRYPTOPP_MSC_VERSION >= 1700)
// #    define MASM_RDRAND_ASM_AVAILABLE 1
#    define ALL_RDRAND_INTRIN_AVAILABLE 1
#  else
#    define MASM_RDRAND_ASM_AVAILABLE 1
#  endif
#  if (CRYPTOPP_MSC_VERSION >= 1800)
// #    define MASM_RDSEED_ASM_AVAILABLE 1
#    define ALL_RDSEED_INTRIN_AVAILABLE 1
#  else
#    define MASM_RDSEED_ASM_AVAILABLE 1
#  endif
# elif defined(CRYPTOPP_LLVM_CLANG_VERSION) || defined(CRYPTOPP_APPLE_CLANG_VERSION)
#  define GCC_RDRAND_ASM_AVAILABLE 1
#  define GCC_RDSEED_ASM_AVAILABLE 1
# elif defined(__SUNPRO_CC)
#  if defined(__RDRND__) && (__SUNPRO_CC >= 0x5130)
#    define ALL_RDRAND_INTRIN_AVAILABLE 1
#  elif (__SUNPRO_CC >= 0x5100)
#    define GCC_RDRAND_ASM_AVAILABLE 1
#  endif
#  if defined(__RDSEED__) && (__SUNPRO_CC >= 0x5140)
#    define ALL_RDSEED_INTRIN_AVAILABLE 1
#  elif (__SUNPRO_CC >= 0x5100)
#    define GCC_RDSEED_ASM_AVAILABLE 1
#  endif
# elif defined(CRYPTOPP_GCC_VERSION)
#  if defined(__RDRND__) || (CRYPTOPP_GCC_VERSION >= 40600)
#    define GCC_RDRAND_ASM_AVAILABLE 1
#  endif
#  if defined(__RDSEED__) || (CRYPTOPP_GCC_VERSION >= 40600)
#    define GCC_RDSEED_ASM_AVAILABLE 1
#  endif
# endif
#endif

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

#if (ALL_RDRAND_INTRIN_AVAILABLE || ALL_RDSEED_INTRIN_AVAILABLE)
# include <immintrin.h> // rdrand, MSC, ICC, GCC, and SunCC
# if defined(__GNUC__) && (CRYPTOPP_GCC_VERSION >= 40600)
#  include <x86intrin.h> // rdseed for some compilers, like GCC
# endif
# if defined(__has_include)
#  if __has_include(<x86intrin.h>)
#   include <x86intrin.h>
#  endif
# endif
#endif

#if MASM_RDRAND_ASM_AVAILABLE
# ifdef _M_X64
extern "C" void CRYPTOPP_FASTCALL MASM_RDRAND_GenerateBlock(byte*, size_t);
// #  pragma comment(lib, "rdrand-x64.lib")
# else
extern "C" void MASM_RDRAND_GenerateBlock(byte*, size_t);
// #  pragma comment(lib, "rdrand-x86.lib")
# endif
#endif

#if MASM_RDSEED_ASM_AVAILABLE
# ifdef _M_X64
extern "C" void CRYPTOPP_FASTCALL MASM_RDSEED_GenerateBlock(byte*, size_t);
// #  pragma comment(lib, "rdrand-x64.lib")
# else
extern "C" void MASM_RDSEED_GenerateBlock(byte*, size_t);
// #  pragma comment(lib, "rdrand-x86.lib")
# endif
#endif

#if NASM_RDRAND_ASM_AVAILABLE
extern "C" void NASM_RDRAND_GenerateBlock(byte*, size_t);
#endif

#if NASM_RDSEED_ASM_AVAILABLE
extern "C" void NASM_RDSEED_GenerateBlock(byte*, size_t);
#endif

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

ANONYMOUS_NAMESPACE_BEGIN
// GCC, MSVC and SunCC has optimized calls to RDRAND away. We experieced it
//   under GCC and MSVC. Other have reported it for SunCC. This attempts
//   to tame the optimizer even though it abuses the volatile keyword.
static volatile int s_unused;
ANONYMOUS_NAMESPACE_END

NAMESPACE_BEGIN(CryptoPP)

// Fills 4 bytes
inline void RDRAND32(void* output)
{
#if defined(__SUNPRO_CC)
    __asm__ __volatile__
    (
        ".byte 0x0f, 0xc7, 0xf0;\n"
        ".byte 0x73, 0xfb;\n"
        : "=a" (*reinterpret_cast<word32*>(output))
        : : "cc"
    );
#elif defined(GCC_RDRAND_ASM_AVAILABLE)
    __asm__ __volatile__
    (
        INTEL_NOPREFIX
        ASL(1)
        AS1(rdrand eax)
        ASJ(jnc,   1, b)
        ATT_NOPREFIX
        : "=a" (*reinterpret_cast<word32*>(output))
        : : "cc"
    );
#elif defined(ALL_RDRAND_INTRIN_AVAILABLE)
    while(!_rdrand32_step(reinterpret_cast<word32*>(output))) {}
#else
    // RDRAND not detected at compile time, or no suitable compiler found
    throw NotImplemented("RDRAND: failed to find an implementation");
#endif
}

#if CRYPTOPP_BOOL_X64
// Fills 8 bytes
inline void RDRAND64(void* output)
{
#if defined(__SUNPRO_CC) && (__SUNPRO_CC >= 0x5100)
    __asm__ __volatile__
    (
        ".byte 0x48, 0x0f, 0xc7, 0xf0;\n"
        ".byte 0x73, 0xfa;\n"
        : "=a" (*reinterpret_cast<word64*>(output))
        : : "cc"
    );
#elif defined(GCC_RDRAND_ASM_AVAILABLE)
    __asm__ __volatile__
    (
        INTEL_NOPREFIX
        ASL(1)
        AS1(rdrand rax)
        ASJ(jnc,   1, b)
        ATT_NOPREFIX
        : "=a" (*reinterpret_cast<word64*>(output))
        : : "cc"
    );
#elif defined(ALL_RDRAND_INTRIN_AVAILABLE)
    while(!_rdrand64_step(reinterpret_cast<unsigned long long*>(output))) {}
#else
    // RDRAND not detected at compile time, or no suitable compiler found
    throw NotImplemented("RDRAND: failed to find an implementation");
#endif
}
#endif  // CRYPTOPP_BOOL_X64 and RDRAND64

void RDRAND::GenerateBlock(byte *output, size_t size)
{
    CRYPTOPP_ASSERT((output && size) || !(output || size));
    if (size == 0) return;

#if defined(NASM_RDRAND_ASM_AVAILABLE)

    NASM_RDRAND_GenerateBlock(output, size);

#elif defined(MASM_RDRAND_ASM_AVAILABLE)

    MASM_RDRAND_GenerateBlock(output, size);

#elif CRYPTOPP_BOOL_X64
    size_t i = 0;
    for (i = 0; i < size/8; i++)
        RDRAND64(reinterpret_cast<word64*>(output)+i);

    output += i*8;
    size -= i*8;

    if (size)
    {
        word64 val;
        RDRAND64(&val);
        std::memcpy(output, &val, size);
    }
#elif (CRYPTOPP_BOOL_X32 || CRYPTOPP_BOOL_X86)
    size_t i = 0;
    for (i = 0; i < size/4; i++)
        RDRAND32(reinterpret_cast<word32*>(output)+i);

    output += i*4;
    size -= i*4;

    if (size)
    {
        word32 val;
        RDRAND32(&val);
        std::memcpy(output, &val, size);
    }
#else
    // RDRAND not detected at compile time, or no suitable compiler found
    throw NotImplemented("RDRAND: failed to find a suitable implementation");
#endif

    // Size is not 0
    s_unused ^= output[0];
}

void RDRAND::DiscardBytes(size_t n)
{
    // RoundUpToMultipleOf is used because a full word is read, and its cheaper
    //   to discard full words. There's no sense in dealing with tail bytes.
    FixedSizeSecBlock<word64, 16> discard;
    n = RoundUpToMultipleOf(n, sizeof(word64));

    size_t count = STDMIN(n, discard.SizeInBytes());
    while (count)
    {
        GenerateBlock(discard.BytePtr(), count);
        n -= count;
        count = STDMIN(n, discard.SizeInBytes());
    }
}

/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////

// Fills 4 bytes
inline void RDSEED32(void* output)
{
#if defined(__SUNPRO_CC)
    __asm__ __volatile__
    (
        ".byte 0x0f, 0xc7, 0xf8;\n"
        ".byte 0x73, 0xfb;\n"
        : "=a" (*reinterpret_cast<word32*>(output))
        : : "cc"
    );
#elif defined(GCC_RDSEED_ASM_AVAILABLE)
    __asm__ __volatile__
    (
        INTEL_NOPREFIX
        ASL(1)
        AS1(rdseed eax)
        ASJ(jnc,   1, b)
        ATT_NOPREFIX
        : "=a" (*reinterpret_cast<word32*>(output))
        : : "cc"
    );
#elif defined(ALL_RDSEED_INTRIN_AVAILABLE)
    while(!_rdseed32_step(reinterpret_cast<unsigned long long*>(output))) {}
#else
    // RDSEED not detected at compile time, or no suitable compiler found
    throw NotImplemented("RDSEED: failed to find an implementation");
#endif
}

// Fills 8 bytes
inline void RDSEED64(void* output)
{
#if defined(__SUNPRO_CC) && (__SUNPRO_CC >= 0x5100)
    __asm__ __volatile__
    (
        ".byte 0x48, 0x0f, 0xc7, 0xf8;\n"
        ".byte 0x73, 0xfa;\n"
        : "=a" (*reinterpret_cast<word64*>(output))
        : : "cc"
    );
#elif defined(GCC_RDSEED_ASM_AVAILABLE)
    __asm__ __volatile__
    (
        INTEL_NOPREFIX
        ASL(1)
        AS1(rdseed rax)
        ASJ(jnc,   1, b)
        ATT_NOPREFIX
        : "=a" (*reinterpret_cast<word64*>(output))
        : : "cc"
    );
#elif defined(ALL_RDSEED_INTRIN_AVAILABLE)
    while(!_rdseed64_step(reinterpret_cast<unsigned long long*>(output))) {}
#else
    // RDSEED not detected at compile time, or no suitable compiler found
    throw NotImplemented("RDSEED: failed to find an implementation");
#endif
}

void RDSEED::GenerateBlock(byte *output, size_t size)
{
    CRYPTOPP_ASSERT((output && size) || !(output || size));
    if (size == 0) return;

#if defined(NASM_RDSEED_ASM_AVAILABLE)

    NASM_RDSEED_GenerateBlock(output, size);

#elif defined(MASM_RDSEED_ASM_AVAILABLE)

    MASM_RDSEED_GenerateBlock(output, size);

#elif CRYPTOPP_BOOL_X64
    size_t i = 0;
    for (i = 0; i < size/8; i++)
        RDSEED64(reinterpret_cast<word64*>(output)+i);

    output += i*8;
    size -= i*8;

    if (size)
    {
        word64 val;
        RDSEED64(&val);
        std::memcpy(output, &val, size);
    }
#elif (CRYPTOPP_BOOL_X32 || CRYPTOPP_BOOL_X86)
    size_t i = 0;
    for (i = 0; i < size/4; i++)
        RDSEED32(reinterpret_cast<word32*>(output)+i);

    output += i*4;
    size -= i*4;

    if (size)
    {
        word32 val;
        RDSEED32(&val);
        std::memcpy(output, &val, size);
    }
#endif

    // Size is not 0
    s_unused ^= output[0];
}

void RDSEED::DiscardBytes(size_t n)
{
    // RoundUpToMultipleOf is used because a full word is read, and its cheaper
    //   to discard full words. There's no sense in dealing with tail bytes.
    FixedSizeSecBlock<word64, 16> discard;
    n = RoundUpToMultipleOf(n, sizeof(word64));

    size_t count = STDMIN(n, discard.SizeInBytes());
    while (count)
    {
        GenerateBlock(discard.BytePtr(), count);
        n -= count;
        count = STDMIN(n, discard.SizeInBytes());
    }
}

NAMESPACE_END
