// Compatibility shim for OpenSSL 1.0.x built with VS2013 or earlier,
// linked against a VS2015+ toolchain (/MT or /MD).
//
// The VS2015+ UCRT changed stdio exports, causing LNK2001 for:
//   __iob_func, _vsnwprintf, vfprintf, sprintf, sscanf
//
// We provide direct implementations for symbols that are truly missing,
// and use /alternatename for symbols that may be provided by other libraries
// (e.g., Lua) to avoid duplicate symbol errors.

#include <stdarg.h>
#include <stddef.h>

// ---- minimal type forward declarations (avoids pulling in stdio.h) --------
#ifndef _FILE_DEFINED
#define _FILE_DEFINED
typedef struct _iobuf
{
    void* _Placeholder;
} FILE;
#endif

// ---- UCRT internal functions we delegate to --------------------------------
extern "C" {

// __acrt_iob_func is the real VS2015+ accessor for stdin/stdout/stderr
FILE* __cdecl __acrt_iob_func(unsigned _Index);

// __stdio_common_* are the true exported implementations in ucrt.lib
int __cdecl __stdio_common_vfprintf(
    unsigned __int64 _Options, FILE* _Stream,
    const char* _Format, void* _Locale, va_list _ArgList);

int __cdecl __stdio_common_vsprintf(
    unsigned __int64 _Options, char* _Buffer, size_t _BufferCount,
    const char* _Format, void* _Locale, va_list _ArgList);

int __cdecl __stdio_common_vsscanf(
    unsigned __int64 _Options, const char* _Buffer, size_t _BufferCount,
    const char* _Format, void* _Locale, va_list _ArgList);

int __cdecl __stdio_common_vsnwprintf_s(
    unsigned __int64 _Options, wchar_t* _Buffer, size_t _BufferCount,
    size_t _MaxCount, const wchar_t* _Format, void* _Locale, va_list _ArgList);

// ---- shim implementations --------------------------------------------------

// __iob_func: Removed entirely in VS2015+; OpenSSL uses it for stdin/stdout/stderr.
// This symbol is unique to old OpenSSL and won't conflict with other libs.
FILE* __cdecl __iob_func(void)
{
    static FILE iob[3];
    iob[0] = *__acrt_iob_func(0);
    iob[1] = *__acrt_iob_func(1);
    iob[2] = *__acrt_iob_func(2);
    return iob;
}

// _vsnwprintf: a _CRT_STDIO_INLINE in VS2015+ UCRT; never a real export.
// Unlikely to conflict with other libs.
int __cdecl _vsnwprintf(wchar_t* buffer, size_t count,
                        const wchar_t* format, va_list ap)
{
    return __stdio_common_vsnwprintf_s(0, buffer, count, count,
                                       format, nullptr, ap);
}

// vfprintf: a _CRT_STDIO_INLINE in VS2015+ UCRT; never a real export.
// Unlikely to conflict with other libs.
int __cdecl vfprintf(FILE* stream, const char* format, va_list ap)
{
    return __stdio_common_vfprintf(0, stream, format, nullptr, ap);
}

// ---------------------------------------------------------------------------
// sprintf and sscanf: These may be provided by other libraries (e.g., Lua).
// We provide fallback implementations with different names and use
// /alternatename to let the linker pick them up only if no other definition
// exists. This avoids LNK2005 duplicate symbol errors.
// ---------------------------------------------------------------------------

// Fallback implementations with unique names
int __cdecl legacy_sprintf_impl(char* buffer, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int result = __stdio_common_vsprintf(0, buffer, (size_t)-1,
                                         format, nullptr, ap);
    va_end(ap);
    return result;
}

int __cdecl legacy_sscanf_impl(const char* buffer, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int result = __stdio_common_vsscanf(0, buffer, (size_t)-1,
                                         format, nullptr, ap);
    va_end(ap);
    return result;
}

} // extern "C"

// Tell the linker: if sprintf/sscanf are unresolved, use our fallback impls.
// If another library (like Lua) already provides them, use that instead.
#if defined(_M_X64) || defined(_M_AMD64)
// x64: symbols are not decorated
#pragma comment(linker, "/alternatename:sprintf=legacy_sprintf_impl")
#pragma comment(linker, "/alternatename:sscanf=legacy_sscanf_impl")
#else
// x86: __cdecl symbols have underscore prefix
#pragma comment(linker, "/alternatename:_sprintf=_legacy_sprintf_impl")
#pragma comment(linker, "/alternatename:_sscanf=_legacy_sscanf_impl")
#endif
