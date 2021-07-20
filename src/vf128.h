#pragma once

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "stdbits.h"
#include "stdendian.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
typedef signed long long s64;
typedef unsigned long long u64;
typedef float f32;
typedef double f64;

/*
 * Debug
 */

extern int debug;

static void vf_set_debug(int level) { debug = level; }

static void log_printf(const char* fmt, ...)
{
    int len;
    char buf[128];
    char *pbuf = buf;
    char *hbuf = NULL;
    va_list ap;
    va_start(ap, fmt);
    len = vsnprintf(pbuf, sizeof(buf)-1, fmt, ap);
    pbuf[sizeof(buf)-1] = '\0';
    va_end(ap);
    if (len >= sizeof(buf)) {
        pbuf = hbuf = (char*)malloc(len + 1);
        va_start(ap, fmt);
        len = vsnprintf(pbuf, len + 1, fmt, ap);
        pbuf[len] = '\0';
        va_end(ap);
    }
    fwrite(pbuf, 1, len, stderr);
    if (hbuf) free(hbuf);
}

#define debugf(...) if(debug) log_printf(__VA_ARGS__)

/*
 * buffer interface
 */

struct vf_buf;

typedef struct vf_buf vf_buf;

typedef int (*check_fn)(vf_buf*,size_t);
typedef int (*sync_fn)(vf_buf*);

struct vf_buf
{
    char *data;           /* buffer */
    size_t read_marker;   /* read marker */
    size_t write_marker;  /* write marker */
    size_t buffer_size;   /* buffer size */
    check_fn read_check;  /* read underflow check */
    check_fn write_check; /* write overflow check */
    sync_fn  sync;        /* buffer read/write */
    int fd;               /* file descriptor */
    int retain;           /* buffer ownership */
    void *userdata;       /* user data */
};

static vf_buf* vf_buffered_reader_fd(int fd);
static vf_buf* vf_buffered_writer_fd(int fd);
static vf_buf* vf_buffered_reader_new(const char* filename);
static vf_buf* vf_buffered_writer_new(const char* filename);
static vf_buf* vf_buf_memory_new(char *data, size_t size);
static vf_buf* vf_buf_new(size_t size);
static vf_buf* vf_resizable_buf_new();
static int vf_buf_resize(vf_buf *buf, size_t new_size);
static int vf_buf_get_fd(vf_buf *buf);
static void vf_buf_set_fd(vf_buf *buf, int fd);
static void* vf_buf_get_userdata(vf_buf *buf);
static void vf_buf_set_userdata(vf_buf *buf, void *userdata);
static void vf_buf_destroy(vf_buf* buf);

static size_t vf_buf_write_i8(vf_buf* buf, int8_t val);
static size_t vf_buf_write_i16(vf_buf* buf, int16_t val);
static size_t vf_buf_write_i32(vf_buf* buf, int32_t val);
static size_t vf_buf_write_i64(vf_buf* buf, int64_t val);
static size_t vf_buf_write_bytes(vf_buf* buf, const char *s, size_t len);
static size_t vf_buf_write_string(vf_buf* buf, const char *s);
static size_t vf_buf_write_bytes_unchecked(vf_buf* buf, const char *s, size_t len);

static size_t vf_buf_write_vec_i16(vf_buf* buf, const int16_t *val, size_t count);
static size_t vf_buf_write_vec_i32(vf_buf* buf, const int32_t *val, size_t count);
static size_t vf_buf_write_vec_i64(vf_buf* buf, const int64_t *val, size_t count);

static size_t vf_buf_read_i8(vf_buf* buf, int8_t *val);
static size_t vf_buf_read_i16(vf_buf* buf, int16_t *val);
static size_t vf_buf_read_i32(vf_buf* buf, int32_t *val);
static size_t vf_buf_read_i64(vf_buf* buf, int64_t *val);
static size_t vf_buf_read_bytes(vf_buf* buf, char *s, size_t len);
static size_t vf_buf_read_bytes_unchecked(vf_buf* buf, char *s, size_t len);

static size_t vf_buf_read_vec_i16(vf_buf* buf, int16_t *val, size_t count);
static size_t vf_buf_read_vec_i32(vf_buf* buf, int32_t *val, size_t count);
static size_t vf_buf_read_vec_i64(vf_buf* buf, int64_t *val, size_t count);

/*
 * buffer inline functions
 */

static inline u64 pow2_ge(u64 x)
{
    return 1ull << (64 - clz(x-1));
}

static inline int vf_buf_resize(vf_buf *buf, size_t new_size)
{
    size_t old_size = buf->buffer_size;
    buf->data = (char*)realloc(buf->data, (buf->buffer_size = new_size));
    memset(buf->data + old_size, 0, new_size - old_size);
    return buf->data == NULL ? -1 : 0;
}

static inline int vf_buf_capacity_error(vf_buf *buf, size_t len)
{
    return -1;
}

static inline int vf_buf_fixed_check_read_capacity(vf_buf *buf, size_t len)
{
    return (buf->read_marker + len > buf->write_marker) ? -1 : 0;
}

static inline int vf_buf_fixed_check_write_capacity(vf_buf *buf, size_t len)
{
    return (buf->write_marker + len > buf->buffer_size) ? -1 : 0;
}

static inline int vf_buf_resizable_check_write_capacity(vf_buf *buf, size_t len)
{
    if (buf->write_marker + len > buf->buffer_size) {
        size_t new_size = pow2_ge(buf->write_marker + len);
        if (vf_buf_resize(buf, new_size) < 0) return -1;
    }
    return 0;
}

static inline int vf_buf_reader_check_capacity(vf_buf *buf, size_t len)
{
    if (buf->read_marker + len > buf->write_marker) {
        if (buf->read_marker > 0) {
            memmove(buf->data, buf->data + buf->read_marker,
                    buf->write_marker - buf->read_marker);
            buf->write_marker -= buf->read_marker;
            buf->read_marker = 0;
        }
        if (buf->sync && buf->sync(buf) < 0) return -1;
    }
    return (buf->read_marker + len > buf->write_marker) ? -1 : 0;
}

static inline int vf_buf_writer_check_capacity(vf_buf *buf, size_t len)
{
    if (buf->write_marker + len > buf->buffer_size) {
        if (buf->sync && buf->sync(buf) < 0) return -1;
        if (buf->read_marker > 0) {
            memmove(buf->data, buf->data + buf->read_marker,
                    buf->write_marker - buf->read_marker);
            buf->write_marker -= buf->read_marker;
            buf->read_marker = 0;
        }
    }
    return (buf->write_marker + len > buf->buffer_size) ? -1 : 0;
}

static inline int vf_buf_reader_sync(vf_buf *buf)
{
    /* buf ----- read_marker ----- write_marker <===> buffer_size */
    size_t bytes_to_read = buf->buffer_size - buf->write_marker;
    if (bytes_to_read > 0) {
        ssize_t nread = read(buf->fd, buf->data + buf->write_marker, bytes_to_read);
        if (nread < 0) return -1;
        buf->write_marker += nread;
        debugf("vf_buf_reader_sync: read_marker=%zu write_marker=%zu buffer_size=%zu: read %zu bytes\n",
            buf->read_marker, buf->write_marker, buf->buffer_size, nread);
    }
    return 0;
}

static inline int vf_buf_writer_sync(vf_buf *buf)
{
    /* buf ----- read_marker <===> write_marker ----- buffer_size */
    size_t bytes_to_write = buf->write_marker - buf->read_marker;
    if (bytes_to_write > 0) {
        ssize_t nwritten = write(buf->fd, buf->data + buf->read_marker, bytes_to_write);
        if (nwritten < 0) return -1;
        buf->read_marker += nwritten;
        debugf("vf_buf_writer_sync: read_marker=%zu write_marker=%zu buffer_size=%zu: wrote %zu bytes\n",
            buf->read_marker, buf->write_marker, buf->buffer_size, nwritten);
    }
    return 0;
}

static inline vf_buf* vf_buffered_reader_fd(int fd)
{
    const size_t size = 4096;
    vf_buf *buf = (vf_buf*)malloc(sizeof(vf_buf));
    vf_buf b = {
        .data = (char*)malloc(size),
        .read_marker = 0,
        .write_marker = 0,
        .buffer_size = size,
        .read_check = vf_buf_reader_check_capacity,
        .write_check = vf_buf_capacity_error,
        .sync = vf_buf_reader_sync,
        .fd = fd,
        .retain = 0,
        .userdata = NULL
    };
    *buf = b;
    return buf;
}

static inline vf_buf* vf_buffered_writer_fd(int fd)
{
    const size_t size = 4096;
    vf_buf *buf = (vf_buf*)malloc(sizeof(vf_buf));
    vf_buf b = {
        .data = (char*)malloc(size),
        .read_marker = 0,
        .write_marker = 0,
        .buffer_size = size,
        .read_check = vf_buf_capacity_error,
        .write_check = vf_buf_writer_check_capacity,
        .sync = vf_buf_writer_sync,
        .fd = fd,
        .retain = 0,
        .userdata = NULL
    };
    *buf = b;
    return buf;
}

static vf_buf* vf_buffered_reader_new(const char* filename)
{
    return vf_buffered_reader_fd(open(filename, O_RDONLY));
}

static vf_buf* vf_buffered_writer_new(const char* filename)
{
    return vf_buffered_writer_fd(open(filename, O_CREAT|O_TRUNC|O_WRONLY, 0666));
}

static inline vf_buf* vf_buf_new(size_t size)
{
    vf_buf *buf = (vf_buf*)malloc(sizeof(vf_buf));
    vf_buf b = {
        .data = (char*)malloc(size),
        .read_marker = 0,
        .write_marker = 0,
        .buffer_size = size,
        .read_check = vf_buf_fixed_check_read_capacity,
        .write_check = vf_buf_fixed_check_write_capacity,
        .sync = NULL,
        .fd = -1,
        .retain = 0,
        .userdata = NULL
    };
    *buf = b;
    return buf;
}

static inline vf_buf* vf_buf_memory_new(char *data, size_t size)
{
    vf_buf *buf = (vf_buf*)malloc(sizeof(vf_buf));
    vf_buf b = {
        .data = data,
        .read_marker = 0,
        .write_marker = size,
        .buffer_size = size,
        .read_check = vf_buf_fixed_check_read_capacity,
        .write_check = vf_buf_fixed_check_write_capacity,
        .sync = NULL,
        .fd = -1,
        .retain = 1,
        .userdata = NULL
    };
    *buf = b;
    return buf;
}

static inline vf_buf* vf_resizable_buf_new()
{
    const size_t size = 4096;
    vf_buf *buf = (vf_buf*)malloc(sizeof(vf_buf));
    vf_buf b = {
        .data = (char*)malloc(size),
        .read_marker = 0,
        .write_marker = 0,
        .buffer_size = size,
        .read_check = vf_buf_fixed_check_read_capacity,
        .write_check = vf_buf_resizable_check_write_capacity,
        .sync = NULL,
        .fd = -1,
        .retain = 0,
        .userdata = NULL
    };
    *buf = b;
    return buf;
}

static inline int vf_buf_get_fd(vf_buf *buf)
{
    return buf->fd;
}

static inline void vf_buf_set_fd(vf_buf *buf, int fd)
{
    buf->fd = fd;
}

static inline void* vf_buf_get_userdata(vf_buf *buf)
{
    return buf->userdata;
}

static inline void vf_buf_set_userdata(vf_buf *buf, void *userdata)
{
    buf->userdata = userdata;
}

static inline void vf_buf_destroy(vf_buf* buf)
{
    if (buf->fd >= 0) {
        if (buf->write_marker > buf->read_marker && buf->sync) buf->sync(buf);
        close(buf->fd);
        buf->fd = -1;
    }
    if (!buf->retain && buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    free(buf);
}

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64) || defined(_M_AMD64)
#ifndef USE_UNALIGNED_ACCESSES
#define USE_UNALIGNED_ACCESSES 1
#endif
#endif

#define USE_CRT_MEMCPY 0
#if USE_CRT_MEMCPY
#define vf_memcpy memcpy
#else
static inline void vf_memcpy(void *dst, const void *src, size_t l)
{
    char *d = (char*)dst;
    const char *s = (const char*)src;
    while (l-- > 0) *d++ = *s++;
}
#endif

#define FN(Y,X) vf_ ## Y ## _ ## X

#if USE_UNALIGNED_ACCESSES

/* use word accesses for all copies between buffers and user memory */

#define CREFL_BUF_WRITE_IMPL(suffix,T,swap)                                    \
static inline size_t FN(buf_write,suffix)(vf_buf *buf, T val)                  \
{                                                                              \
    if (buf->write_check(buf, sizeof(T))) return 0;                            \
    T t = swap(val);                                                           \
    *(T*)(buf->data + buf->write_marker) = t;                                  \
    buf->write_marker += sizeof(T);                                            \
    return sizeof(T);                                                          \
}                                                                              \
static inline size_t FN(buf_write_unchecked,suffix)(vf_buf *buf, T val)        \
{                                                                              \
    T t = swap(val);                                                           \
    *(T*)(buf->data + buf->write_marker) = t;                                  \
    buf->write_marker += sizeof(T);                                            \
    return sizeof(T);                                                          \
}                                                                              \
static inline size_t FN(buf_write_vec,suffix)(vf_buf *buf, const T *val, size_t count)\
{                                                                              \
    if (buf->write_check(buf, sizeof(T) * count)) return 0;                    \
    for (size_t i = 0; i < count; i++) {                                       \
        ((T*)(buf->data + buf->write_marker))[i] = swap(val[i]);               \
    }                                                                          \
    buf->write_marker += sizeof(T) * count;                                    \
    return sizeof(T) * count;                                                  \
}

#define CREFL_BUF_READ_IMPL(suffix,T,swap)                                     \
static inline size_t FN(buf_read,suffix)(vf_buf *buf, T* val)                  \
{                                                                              \
    if (buf->read_check(buf, sizeof(T))) return 0;                             \
    T t = *(T*)(buf->data + buf->read_marker);                                 \
    *val = swap(t);                                                            \
    buf->read_marker += sizeof(T);                                             \
    return sizeof(T);                                                          \
}                                                                              \
static inline size_t FN(buf_read_unchecked,suffix)(vf_buf *buf, T* val)        \
{                                                                              \
    T t = *(T*)(buf->data + buf->read_marker);                                 \
    *val = swap(t);                                                            \
    buf->read_marker += sizeof(T);                                             \
    return sizeof(T);                                                          \
}                                                                              \
static inline size_t FN(buf_read_vec,suffix)(vf_buf *buf, T* val, size_t count)\
{                                                                              \
    if (buf->read_check(buf, sizeof(T) * count)) return 0;                     \
    for (size_t i = 0; i < count; i++) {                                       \
        val[i] = swap(((T*)(buf->data + buf->read_marker))[i]);                \
    }                                                                          \
    buf->read_marker += sizeof(T) * count;                                     \
    return sizeof(T) * count;                                                  \
}

#else

/* use vf_memcpy for small copies and the host memcpy for vector copies */

#define CREFL_BUF_WRITE_IMPL(suffix,T,swap)                                    \
static inline size_t FN(buf_write,suffix)(vf_buf *buf, T val)                  \
{                                                                              \
    if (buf->write_check(buf, sizeof(T))) return 0;                            \
    T t = swap(val);                                                           \
    vf_memcpy(buf->data + buf->write_marker, &t, sizeof(T));                   \
    buf->write_marker += sizeof(T);                                            \
    return sizeof(T);                                                          \
}                                                                              \
static inline size_t FN(buf_write_unchecked,suffix)(vf_buf *buf, T val)        \
{                                                                              \
    T t = swap(val);                                                           \
    vf_memcpy(buf->data + buf->write_marker, &t, sizeof(T));                   \
    buf->write_marker += sizeof(T);                                            \
    return sizeof(T);                                                          \
}                                                                              \
static inline size_t FN(buf_write_vec,suffix)(vf_buf *buf, const T *val, size_t count)\
{                                                                              \
    if (buf->write_check(buf, sizeof(T) * count)) return 0;                    \
    for (size_t i = 0; i < count; i++) {                                       \
        T t = swap(val[i]);                                                    \
        memcpy(buf->data + buf->write_marker + sizeof(T) * i, &t, sizeof(T));  \
    }                                                                          \
    buf->write_marker += sizeof(T) * count;                                    \
    return sizeof(T) * count;                                                  \
}

#define CREFL_BUF_READ_IMPL(suffix,T,swap)                                     \
static inline size_t FN(buf_read,suffix)(vf_buf *buf, T* val)                  \
{                                                                              \
    if (buf->read_check(buf, sizeof(T))) return 0;                             \
    T t;                                                                       \
    vf_memcpy(&t, buf->data + buf->read_marker, sizeof(T));                    \
    *val = swap(t);                                                            \
    buf->read_marker += sizeof(T);                                             \
    return sizeof(T);                                                          \
}                                                                              \
static inline size_t FN(buf_read_unchecked,suffix)(vf_buf *buf, T* val)        \
{                                                                              \
    T t;                                                                       \
    vf_memcpy(&t, buf->data + buf->read_marker, sizeof(T));                    \
    *val = swap(t);                                                            \
    buf->read_marker += sizeof(T);                                             \
    return sizeof(T);                                                          \
}                                                                              \
static inline size_t FN(buf_read_vec,suffix)(vf_buf *buf, T* val, size_t count)\
{                                                                              \
    if (buf->read_check(buf, sizeof(T) * count)) return 0;                     \
    for (size_t i = 0; i < count; i++) {                                       \
        T t;                                                                   \
        memcpy(&t, buf->data + buf->read_marker + sizeof(T) * i, sizeof(T));   \
        val[i] = swap(t);                                                      \
    }                                                                          \
    buf->read_marker += sizeof(T) * count;                                     \
    return sizeof(T) * count;                                                  \
}

#endif

CREFL_BUF_WRITE_IMPL(i16,int16_t,le16)
CREFL_BUF_WRITE_IMPL(i32,int32_t,le32)
CREFL_BUF_WRITE_IMPL(i64,int64_t,le64)

CREFL_BUF_READ_IMPL(i16,int16_t,le16)
CREFL_BUF_READ_IMPL(i32,int32_t,le32)
CREFL_BUF_READ_IMPL(i64,int64_t,le64)

#undef FN

static inline size_t vf_buf_write_i8(vf_buf *buf, int8_t val)
{
    if (buf->write_check(buf, 1)) return 0;
    *(int8_t*)(buf->data + buf->write_marker) = val;
    buf->write_marker++;
    return 1;
}
static inline size_t vf_buf_write_unchecked_i8(vf_buf *buf, int8_t val)
{
    *(int8_t*)(buf->data + buf->write_marker) = val;
    buf->write_marker++;
    return 1;
}

static inline size_t vf_buf_write_bytes(vf_buf* buf, const char *src, size_t len)
{
    if (buf->write_check(buf, len)) return 0;
    vf_memcpy(&buf->data[buf->write_marker], src, len);
    buf->write_marker += len;
    return len;
}

static size_t vf_buf_write_string(vf_buf* buf, const char *s)
{
    return vf_buf_write_bytes(buf, s, strlen(s));
}

static size_t vf_buf_write_format(vf_buf* buf, const char* fmt, ...)
{
    if (buf->write_check(buf, strlen(fmt)*2)) return 0;

    size_t remaining = buf->buffer_size - buf->write_marker;

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf->data + buf->write_marker, remaining, fmt, args);
    va_end(args);
    if ((size_t)len > remaining) {
        if (buf->write_check(buf, len)) return 0;
        remaining = buf->buffer_size - buf->write_marker;
        va_list args;
        va_start(args, fmt);
        len = vsnprintf(buf->data + buf->write_marker, remaining, fmt, args);
        va_end(args);
    }
    buf->write_marker += len;
    return len;
}

static inline size_t vf_buf_write_bytes_unchecked(vf_buf* buf, const char *src, size_t len)
{
    vf_memcpy(&buf->data[buf->write_marker], src, len);
    buf->write_marker += len;
    return len;
}

static inline size_t vf_buf_read_i8(vf_buf *buf, int8_t* val)
{
    if (buf->read_check(buf, 1)) return 0;
    *val = *(int8_t*)(buf->data + buf->read_marker);
    buf->read_marker++;
    return 1;
}

static inline size_t vf_buf_read_unchecked_i8(vf_buf *buf, int8_t* val)
{
    *val = *(int8_t*)(buf->data + buf->read_marker);
    buf->read_marker++;
    return 1;
}

static inline size_t vf_buf_read_bytes(vf_buf* buf, char *dst, size_t len)
{
    if (buf->read_check(buf, len)) return 0;
    vf_memcpy(dst, &buf->data[buf->read_marker], len);
    buf->read_marker += len;
    return len;
}

static inline size_t vf_buf_read_bytes_unchecked(vf_buf* buf, char *dst, size_t len)
{
    vf_memcpy(dst, &buf->data[buf->read_marker], len);
    buf->read_marker += len;
    return len;
}

/*
 * floating point helpers
 */

float _f32_inf();
float _f32_nan();
float _f32_snan();
double _f64_inf();
double _f64_nan();
double _f64_snan();

/*
 * integer and floating-point serialization
 */

typedef enum {
    asn1_class_universal        = 0b00,
    asn1_class_application      = 0b01,
    asn1_class_context_specific = 0b10,
    asn1_class_private          = 0b11
} asn1_class;

typedef enum {
    asn1_tag_reserved           = 0,
    asn1_tag_boolean            = 1,
    asn1_tag_integer            = 2,
    asn1_tag_bit_string         = 3,
    asn1_tag_octet_string       = 4,
    asn1_tag_null               = 5,
    asn1_tag_object_identifier  = 6,
    asn1_tag_object_descriptor  = 7,
    asn1_tag_external           = 8,
    asn1_tag_real               = 9,
} asn1_tag;

struct asn1_id
{
    u64 _identifier  : 56;
    u64 _constructed : 1;
    u64 _class       : 2;
};
typedef struct asn1_id asn1_id;

struct asn1_hdr
{
    asn1_id _id;
    u64 _length;
};
typedef struct asn1_hdr asn1_hdr;

struct f32_result { f32 value; s32 error; };
struct f64_result { f64 value; s64 error; };
struct s64_result { s64 value; s64 error; };
struct u64_result { u64 value; s64 error; };

size_t vf_asn1_ber_tag_length(u64 len);
int vf_asn1_ber_tag_read(vf_buf *buf, u64 *len);
int vf_asn1_ber_tag_write(vf_buf *buf, u64 len);

size_t vf_asn1_ber_ident_length(asn1_id _id);
int vf_asn1_ber_ident_read(vf_buf *buf, asn1_id *_id);
int vf_asn1_ber_ident_write(vf_buf *buf, asn1_id _id);

size_t vf_asn1_ber_length_length(u64 length);
int vf_asn1_ber_length_read(vf_buf *buf, u64 *length);
int vf_asn1_ber_length_write(vf_buf *buf, u64 length);

size_t vf_asn1_ber_integer_u64_length(const u64 *value);
int vf_asn1_ber_integer_u64_read(vf_buf *buf, size_t len, u64 *value);
int vf_asn1_ber_integer_u64_write(vf_buf *buf, size_t len, const u64 *value);
int vf_asn1_der_integer_u64_read(vf_buf *buf, asn1_tag _tag, u64 *value);
int vf_asn1_der_integer_u64_write(vf_buf *buf, asn1_tag _tag, const u64 *value);

size_t vf_asn1_ber_integer_u64_length_byval(const u64 value);
struct u64_result vf_asn1_ber_integer_u64_read_byval(vf_buf *buf, size_t len);
int vf_asn1_ber_integer_u64_write_byval(vf_buf *buf, size_t len, const u64 value);
struct u64_result vf_asn1_der_integer_u64_read_byval(vf_buf *buf, asn1_tag _tag);
int vf_asn1_der_integer_u64_write_byval(vf_buf *buf, asn1_tag _tag, const u64 value);

size_t vf_asn1_ber_integer_s64_length(const s64 *value);
int vf_asn1_ber_integer_s64_read(vf_buf *buf, size_t len, s64 *value);
int vf_asn1_ber_integer_s64_write(vf_buf *buf, size_t len, const s64 *value);
int vf_asn1_der_integer_s64_read(vf_buf *buf, asn1_tag _tag, s64 *value);
int vf_asn1_der_integer_s64_write(vf_buf *buf, asn1_tag _tag, const s64 *value);

size_t vf_asn1_ber_integer_s64_length_byval(const s64 value);
struct s64_result vf_asn1_ber_integer_s64_read_byval(vf_buf *buf, size_t len);
int vf_asn1_ber_integer_s64_write_byval(vf_buf *buf, size_t len, const s64 value);
struct s64_result vf_asn1_der_integer_s64_read_byval(vf_buf *buf, asn1_tag _tag);
int vf_asn1_der_integer_s64_write_byval(vf_buf *buf, asn1_tag _tag, const s64 value);

size_t vf_le_ber_integer_u64_length(const u64 *value);
int vf_le_ber_integer_u64_read(vf_buf *buf, size_t len, u64 *value);
int vf_le_ber_integer_u64_write(vf_buf *buf, size_t len, const u64 *value);

size_t vf_le_ber_integer_u64_length_byval(const u64 value);
struct u64_result vf_le_ber_integer_u64_read_byval(vf_buf *buf, size_t len);
int vf_le_ber_integer_u64_write_byval(vf_buf *buf, size_t len, const u64 value);

size_t vf_le_ber_integer_s64_length(const s64 *value);
int vf_le_ber_integer_s64_read(vf_buf *buf, size_t len, s64 *value);
int vf_le_ber_integer_s64_write(vf_buf *buf, size_t len, const s64 *value);

size_t vf_le_ber_integer_s64_length_byval(const s64 *value);
struct s64_result vf_le_ber_integer_s64_read_byval(vf_buf *buf, size_t len);
int vf_le_ber_integer_s64_write_byval(vf_buf *buf, size_t len, const s64 value);

size_t vf_asn1_ber_real_f64_length(const double *value);
int vf_asn1_ber_real_f64_read(vf_buf *buf, size_t len, double *value);
int vf_asn1_ber_real_f64_write(vf_buf *buf, size_t len, const double *value);
int vf_asn1_der_real_f64_read(vf_buf *buf, asn1_tag _tag, double *value);
int vf_asn1_der_real_f64_write(vf_buf *buf, asn1_tag _tag, const double *value);

size_t vf_asn1_ber_real_f64_length_byval(const double value);
struct f64_result vf_asn1_ber_real_f64_read_byval(vf_buf *buf, size_t len);
int vf_asn1_ber_real_f64_write_byval(vf_buf *buf, size_t len, const double value);
struct f64_result vf_asn1_der_real_f64_read_byval(vf_buf *buf, asn1_tag _tag);
int vf_asn1_der_real_f64_write_byval(vf_buf *buf, asn1_tag _tag, const double value);

int vf_f64_read(vf_buf *buf, double *value);
int vf_f64_write(vf_buf *buf, const double *value);
struct f64_result vf_f64_read_byval(vf_buf *buf);
int vf_f64_write_byval(vf_buf *buf, const double value);

int vf_f64_read_vec(vf_buf *buf, double *value, size_t count);
int vf_f64_write_vec(vf_buf *buf, const double *value, size_t count);

int vf_f32_read(vf_buf *buf, float *value);
int vf_f32_write(vf_buf *buf, const float *value);
struct f32_result vf_f32_read_byval(vf_buf *buf);
int vf_f32_write_byval(vf_buf *buf, const float value);

int vf_f32_read_vec(vf_buf *buf, float *value, size_t count);
int vf_f32_write_vec(vf_buf *buf, const float *value, size_t count);

int ieee754_f64_read(vf_buf *buf, float *value);
int ieee754_f64_write(vf_buf *buf, const float *value);
struct f64_result ieee754_f64_read_byval(vf_buf *buf);
int ieee754_f64_write_byval(vf_buf *buf, const float value);

int ieee754_f64_read_vec(vf_buf *buf, double *value, size_t count);
int ieee754_f64_write_vec(vf_buf *buf, const double *value, size_t count);

int ieee754_f32_read(vf_buf *buf, float *value);
int ieee754_f32_write(vf_buf *buf, const float *value);
struct f32_result ieee754_f32_read_byval(vf_buf *buf);
int ieee754_f32_write_byval(vf_buf *buf, const float value);

int ieee754_f32_read_vec(vf_buf *buf, float *value, size_t count);
int ieee754_f32_write_vec(vf_buf *buf, const float *value, size_t count);

int leb_u64_read(vf_buf *buf, u64 *value);
int leb_u64_write(vf_buf *buf, const u64 *value);

struct u64_result leb_u64_read_byval(vf_buf *buf);
int leb_u64_write_byval(vf_buf *buf, const u64 value);

int vlu_u64_read(vf_buf *buf, u64 *value);
int vlu_u64_write(vf_buf *buf, const u64 *value);

struct u64_result vlu_u64_read_byval(vf_buf *buf);
int vlu_u64_write_byval(vf_buf *buf, const u64 value);

#ifdef __cplusplus
}
#endif
