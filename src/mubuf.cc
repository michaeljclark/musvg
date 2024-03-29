#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cassert>
#include <cinttypes>

#include <string>
#include <limits>

#include "mubuf.h"
#include "ztdbits.h"
#include "ztdendian.h"

#define DEBUG_ENCODING 0

int debug = 0;

/*
 * structure of ASN.1 tagged data
 *
 * |------------------------|
 * |   identifier octets    |
 * |------------------------|
 * |     length octets      |
 * |------------------------|
 * |    contents octets     |
 * |------------------------|
 * | end-of-contents octets |
 * |------------------------|
 */

/*
 * ISO/IEC 8825-1:2003 8.1.2.4.2 subsequent octets
 *
 * read and write 56-bit identifier high tag
 * called by ident_read | ident_write if low tag == 0b11111
 */

size_t mu_asn1_ber_tag_length(u64 tag)
{
    return tag == 0 ? 1 : 8 - ((clz(tag) - 1) / 7) + 1;
}

int mu_asn1_ber_tag_read(mu_buf *buf, u64 *tag)
{
    int8_t b;
    size_t w = 0;
    u64 l = 0;

    do {
        if (mu_buf_read_i8(buf, &b) != 1) {
            goto err;
        }
        l <<= 7;
        l |= (uint8_t)b & 0x7f;
        w += 7;
    } while ((b & 0x80) && w < 56);

    if (w > 56) {
        goto err;
    }

    *tag = l;
    return 0;
err:
    *tag = 0;
    return -1;
}

int mu_asn1_ber_tag_write(mu_buf *buf, u64 tag)
{
    int8_t b;
    size_t llen;
    u64 l = 0;

    if (tag >= (1ull << 56)) {
        return -1;
    }

    llen = mu_asn1_ber_tag_length(tag);
    l = tag << (64 - llen * 7);
    for (size_t i = 0; i < llen; i++) {
        b = ((l >> 57) & 0x7f);
        b |= (i != llen - 1) << 7;
        l <<= 7;
        if (mu_buf_write_i8(buf, b) != 1) {
            return -1;
        }
    }

    return 0;
}

/*
 * ISO/IEC 8825-1:2003 8.1.2 identifier
 *
 * read and write identifier
 */

size_t mu_asn1_ber_ident_length(asn1_id _id)
{
    return 1 + ((_id._identifier >= 0x1f) ?
        mu_asn1_ber_tag_length(_id._identifier) : 0);
}

int mu_asn1_ber_ident_read(mu_buf *buf, asn1_id *_id)
{
    int8_t b;
    asn1_id r = { 0 };

    if (mu_buf_read_i8(buf, &b) != 1) {
        return -1;;
    }

    _id->_class =       (b >> 6) & 0x02;
    _id->_constructed = (b >> 5) & 0x01;
    _id->_identifier =   b       & 0x1f;

    if (_id->_identifier == 0x1f) {
        u64 ber_tag;
        if (mu_asn1_ber_tag_read(buf, &ber_tag) < 0) {
            return -1;
        }
        if (ber_tag < 0x1f) {
            return -1;
        }
        _id->_identifier = ber_tag;
    }

    return 0;
}

int mu_asn1_ber_ident_write(mu_buf *buf, asn1_id _id)
{
    int8_t b;

    b = ( (u8)(_id._class       & 0x02) << 6 ) |
        ( (u8)(_id._constructed & 0x01) << 5 ) |
        ( (u8)(_id._identifier < 0x1f ? _id._identifier : 0x1f) );

    if (mu_buf_write_i8(buf, b) != 1) {
        return -1;
    }

    if (_id._identifier >= 0x1f) {
        if (mu_asn1_ber_tag_write(buf, _id._identifier) < 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * ISO/IEC 8825-1:2003 8.1.3 length
 *
 * read and write 64-bit length
 */

size_t mu_asn1_ber_length_length(u64 length)
{
    return 1 + ((length >= 0x80) ? 8 - (clz(length) / 8) : 0);
}

int mu_asn1_ber_length_read(mu_buf *buf, u64 *length)
{
    int8_t b;
    size_t llen = 0;
    u64 l = 0;

    if (mu_buf_read_i8(buf, &b) != 1) {
        goto err;
    }
    if ((b & 0x80) == 0) {
        l = b & 0x7f;
        goto out;
    }
    llen = b & 0x7f;
    if (llen == 0) {
        // indefinate form not supported
        goto err;
    }
    else if (llen > 8) {
        goto err;
    }
    for (size_t i = 0; i < llen; i++) {
        if (mu_buf_read_i8(buf, &b) != 1) {
            goto err;
        }
        l <<= 8;
        l |= (uint8_t)b;
    }

out:
    *length = l;
    return 0;
err:
    *length = 0;
    return -1;
}

int mu_asn1_ber_length_write(mu_buf *buf, u64 length)
{
    int8_t b;
    size_t llen;
    u64 l = 0;

    if (length <= 0x7f) {
        b = (int8_t)length;
        if (mu_buf_write_i8(buf, b) != 1) {
            return -1;
        }
        return 0;
    }
    // indefinate form not supported

    llen = 8 - (clz(length) / 8);
    b = (u8)llen | (u8)0x80;
    if (mu_buf_write_i8(buf, b) != 1) {
        return -1;
    }

    l = length << (64 - llen * 8);
    for (size_t i = 0; i < llen; i++) {
        b = (l >> 56) & 0xff;
        l <<= 8;
        if (mu_buf_write_i8(buf, b) != 1) {
            return -1;
        }
    }

    return 0;
}

/*
 * ISO/IEC 8825-1:2003 8.3 integer
 *
 * read and write integer
 */

size_t mu_asn1_ber_integer_u64_length(const u64 *value)
{
    return *value == 0 ? 1 : 8 - (clz(*value) / 8);
}

size_t mu_asn1_ber_integer_u64_length_byval(const u64 value)
{
    return value == 0 ? 1 : 8 - (clz(value) / 8);
}

int mu_asn1_ber_integer_u64_read(mu_buf *buf, size_t len, u64 *value)
{
    u64 v = 0, o = 0;
    size_t shift = (64 - len * 8);

    if (len > 8) {
        goto err;
    }
    if (mu_buf_read_bytes(buf, (char*)&o, len) != len) {
        goto err;
    }

#if _BYTE_ORDER == _BIG_ENDIAN
    v = be64(o >> shift);
#elif _BYTE_ORDER == _LITTLE_ENDIAN
    v = be64(o << shift);
#endif

    *value = v;
    return 0;
err:
    *value = 0;
    return -1;
}

u64_result mu_asn1_ber_integer_u64_read_byval(mu_buf *buf, size_t len)
{
    u64 v = 0, o = 0;
    size_t shift = (64 - len * 8);

    if (len > 8) {
        return { 0, -1 };
    }
    if (mu_buf_read_bytes(buf, (char*)&o, len) != len) {
        return { 0, -1 };
    }

#if _BYTE_ORDER == _BIG_ENDIAN
    v = be64(o >> shift);
#elif _BYTE_ORDER == _LITTLE_ENDIAN
    v = be64(o << shift);
#endif

    return { v, 0 };
}

int mu_asn1_ber_integer_u64_write(mu_buf *buf, size_t len, const u64 *value)
{
    u64 v = 0, o = 0;
    size_t shift = (64 - len * 8);

    if (len < 1 || len > 8) {
        return -1;
    }

#if _BYTE_ORDER == _BIG_ENDIAN
    o = be64(*value) << shift;
#elif _BYTE_ORDER == _LITTLE_ENDIAN
    o = be64(*value) >> shift;
#endif

    if (mu_buf_write_bytes(buf, (const char*)&o, len) != len) {
        return -1;
    }

    return 0;
}

int mu_asn1_ber_integer_u64_write_byval(mu_buf *buf, size_t len, const u64 value)
{
    u64 v = 0, o = 0;
    size_t shift = (64 - len * 8);

    if (len < 1 || len > 8) {
        return -1;
    }

#if _BYTE_ORDER == _BIG_ENDIAN
    o = be64(value) << shift;
#elif _BYTE_ORDER == _LITTLE_ENDIAN
    o = be64(value) >> shift;
#endif

    if (mu_buf_write_bytes(buf, (const char*)&o, len) != len) {
        return -1;
    }

    return 0;
}
/*
 * ISO/IEC 8825-1:2003 8.3 integer
 *
 * read and write integer
 */

size_t mu_le_ber_integer_u64_length(const u64 *value)
{
    return *value == 0 ? 1 : 8 - (clz(*value) / 8);
}

size_t mu_le_ber_integer_u64_length_byval(const u64 value)
{
    return value == 0 ? 1 : 8 - (clz(value) / 8);
}

int mu_le_ber_integer_u64_read(mu_buf *buf, size_t len, u64 *value)
{
    u64 v = 0, o = 0;

    if (len > 8) {
        goto err;
    }

#if USE_UNALIGNED_ACCESSES
    *value = 0;
    /* unaligned accesses are enabled on little-endian targets */
    if (mu_buf_read_bytes(buf, (char*)value, len) != len) {
        goto err;
    }
#else
    if (mu_buf_read_bytes(buf, (char*)&o, len) != len) {
        goto err;
    }

    *value = le64(o);
#endif
    return 0;
err:
    *value = 0;
    return -1;
}

u64_result mu_le_ber_integer_u64_read_byval(mu_buf *buf, size_t len)
{
    u64 v = 0, o = 0;

    if (len > 8) {
        return u64_result { 0, -1 };
    }

    if (mu_buf_read_bytes(buf, (char*)&o, len) != len) {
        return u64_result { 0, -1 };
    }

    return u64_result { le64(o), 0 };
}

int mu_le_ber_integer_u64_write(mu_buf *buf, size_t len, const u64 *value)
{
    u64 v = 0, o = 0;

    if (len < 1 || len > 8) {
        return -1;
    }

#if USE_UNALIGNED_ACCESSES
    /* unaligned accesses are enabled on little-endian targets */
    if (mu_buf_write_bytes(buf, (const char*)value, len) != len) {
        return -1;
    }
#else
    o = le64(*value);

    if (mu_buf_write_bytes(buf, (const char*)&o, len) != len) {
        return -1;
    }
#endif

    return 0;
}

int mu_le_ber_integer_u64_write_byval(mu_buf *buf, size_t len, const u64 value)
{
    u64 v = 0, o = 0;

    if (len < 1 || len > 8) {
        return -1;
    }

    o = le64(value);

    if (mu_buf_write_bytes(buf, (const char*)&o, len) != len) {
        return -1;
    }

    return 0;
}

/*
 * ASN.1 does not distinguish between signed and unsigned integers.
 * signed number deserialization requires that values are sign-extended.
 * to calculate the length negative values are complemented and 1-bit is
 * reserved for the sign. the values are stored in two's complement.
 *
 * - 0x000000000000007f -> 0x7f
 * - 0x0000000000000080 -> 0x0080
 * - 0xffffffffffffff80 -> 0x80
 * - 0xffffffffffffff7f -> 0xff7f
 */
size_t mu_asn1_ber_integer_s64_length(const s64 *value)
{
    const s64 v = *value;
    return v == 0 ? 1 : 8 - ((clz(v < 0 ? ~v : v)-1) / 8);
}

size_t mu_asn1_ber_integer_s64_length_byval(const s64 value)
{
    const s64 v = value;
    return v == 0 ? 1 : 8 - ((clz(v < 0 ? ~v : v)-1) / 8);
}

static s64 _sign_extend_s64(s64 x, size_t y) { return ((s64)(x << y)) >> y; }

int mu_asn1_ber_integer_s64_read(mu_buf *buf, size_t len, s64 *value)
{
    int ret = mu_asn1_ber_integer_u64_read(buf, len, (u64*)value);
    if (ret == 0) {
        *value = _sign_extend_s64(*value, 64-(len << 3));
    }
    return ret;
}

s64_result mu_asn1_ber_integer_s64_read_byval(mu_buf *buf, size_t len)
{
    u64_result r = mu_asn1_ber_integer_u64_read_byval(buf, len);
    if (r.error == 0) {
        return s64_result { _sign_extend_s64(r.value, 64-(len << 3)), 0 };
    }
    return s64_result { 0, -1 };
}

int mu_asn1_ber_integer_s64_write(mu_buf *buf, size_t len, const s64 *value)
{
    return mu_asn1_ber_integer_u64_write(buf, len, (const u64*)value);
}

int mu_asn1_ber_integer_s64_write_byval(mu_buf *buf, size_t len, const s64 value)
{
    return mu_asn1_ber_integer_u64_write_byval(buf, len, (const u64)value);
}

size_t mu_le_ber_integer_s64_length(const s64 *value)
{
    return mu_asn1_ber_integer_s64_length(value);
}

size_t mu_le_ber_integer_s64_length_byval(const s64 value)
{
    return mu_asn1_ber_integer_s64_length_byval(value);
}

int mu_le_ber_integer_s64_read(mu_buf *buf, size_t len, s64 *value)
{
    int ret = mu_le_ber_integer_u64_read(buf, len, (u64*)value);
    if (ret == 0) {
        *value = _sign_extend_s64(*value, 64-(len << 3));
    }
    return ret;
}

s64_result mu_le_ber_integer_s64_read_byval(mu_buf *buf, size_t len)
{
    u64_result r = mu_le_ber_integer_u64_read_byval(buf, len);
    if (r.error == 0) {
        return s64_result { _sign_extend_s64(r.value, 64-(len << 3)), 0 };
    }
    return s64_result { 0, -1 };
}

int mu_le_ber_integer_s64_write(mu_buf *buf, size_t len, const s64 *value)
{
    return mu_le_ber_integer_u64_write(buf, len, (const u64*)value);
}

int mu_le_ber_integer_s64_write_byval(mu_buf *buf, size_t len, const s64 value)
{
    return mu_le_ber_integer_u64_write_byval(buf, len, value);
}

/*
 * read and write tagged integer
 */

int mu_asn1_der_integer_u64_read(mu_buf *buf, asn1_tag _tag, u64 *value)
{
    asn1_hdr hdr;
    if (mu_asn1_ber_ident_read(buf, &hdr._id) < 0) return -1;
    if (mu_asn1_ber_length_read(buf, &hdr._length) < 0) return -1;
    return mu_asn1_ber_integer_u64_read(buf, hdr._length, value);
}

u64_result mu_asn1_der_integer_u64_read_byval(mu_buf *buf, asn1_tag _tag)
{
    asn1_hdr hdr;
    if (mu_asn1_ber_ident_read(buf, &hdr._id) < 0) return u64_result { 0, -1 };
    if (mu_asn1_ber_length_read(buf, &hdr._length) < 0) return u64_result { 0, -1 };
    return mu_asn1_ber_integer_u64_read_byval(buf, hdr._length);
}

int mu_asn1_der_integer_u64_write(mu_buf *buf, asn1_tag _tag, const u64 *value)
{
    asn1_hdr hdr = {
        { (u64)_tag, 0, asn1_class_universal }, mu_asn1_ber_integer_u64_length(value)
    };

    if (mu_asn1_ber_ident_write(buf, hdr._id) < 0) return -1;
    if (mu_asn1_ber_length_write(buf, hdr._length) < 0) return -1;
    return mu_asn1_ber_integer_u64_write(buf, hdr._length, value);
}

int mu_asn1_der_integer_u64_write_byval(mu_buf *buf, asn1_tag _tag, const u64 value)
{
    asn1_hdr hdr = {
        { (u64)_tag, 0, asn1_class_universal }, mu_asn1_ber_integer_u64_length_byval(value)
    };

    if (mu_asn1_ber_ident_write(buf, hdr._id) < 0) return -1;
    if (mu_asn1_ber_length_write(buf, hdr._length) < 0) return -1;
    return mu_asn1_ber_integer_u64_write_byval(buf, hdr._length, value);
}

int mu_asn1_der_integer_s64_read(mu_buf *buf, asn1_tag _tag, s64 *value)
{
    asn1_hdr hdr;
    if (mu_asn1_ber_ident_read(buf, &hdr._id) < 0) return -1;
    if (mu_asn1_ber_length_read(buf, &hdr._length) < 0) return -1;
    return mu_asn1_ber_integer_s64_read(buf, hdr._length, value);
}

s64_result mu_asn1_der_integer_s64_read_byval(mu_buf *buf, asn1_tag _tag, s64 *value)
{
    asn1_hdr hdr;
    if (mu_asn1_ber_ident_read(buf, &hdr._id) < 0) return s64_result { 0, -1 };
    if (mu_asn1_ber_length_read(buf, &hdr._length) < 0) return s64_result { 0, -1 };
    return mu_asn1_ber_integer_s64_read_byval(buf, hdr._length);
}

int mu_asn1_der_integer_s64_write(mu_buf *buf, asn1_tag _tag, const s64 *value)
{
    asn1_hdr hdr = {
        { (u64)_tag, 0, asn1_class_universal }, mu_asn1_ber_integer_s64_length(value)
    };

    if (mu_asn1_ber_ident_write(buf, hdr._id) < 0) return -1;
    if (mu_asn1_ber_length_write(buf, hdr._length) < 0) return -1;
    return mu_asn1_ber_integer_s64_write(buf, hdr._length, value);
}

int mu_asn1_der_integer_s64_write_byval(mu_buf *buf, asn1_tag _tag, const s64 value)
{
    asn1_hdr hdr = {
        { (u64)_tag, 0, asn1_class_universal }, mu_asn1_ber_integer_s64_length_byval(value)
    };

    if (mu_asn1_ber_ident_write(buf, hdr._id) < 0) return -1;
    if (mu_asn1_ber_length_write(buf, hdr._length) < 0) return -1;
    return mu_asn1_ber_integer_s64_write_byval(buf, hdr._length, value);
}

/*
 * IEEE 754 encoding and decoding functions
 */

struct f32_struct { u32 mant : 23; u32 exp : 8;  u32 sign : 1; };
struct f64_struct { u64 mant : 52; u64 exp : 11; u64 sign : 1; };

enum : u32 {
    f32_exp_size = 8,
    f32_mant_size = 23,

    f32_mant_shift = 0,
    f32_exp_shift = f32_mant_size,
    f32_sign_shift = f32_mant_size + f32_exp_size,

    f32_mant_mask = (1 << f32_mant_size) - 1,
    f32_exp_mask = (1 << f32_exp_size) - 1,
    f32_sign_mask = 1,

    f32_mant_prefix = (1ull << f32_mant_size),
    f32_exp_bias = (1 << (f32_exp_size-1)) - 1
};

enum : u64 {
    f64_exp_size = 11,
    f64_mant_size = 52,

    f64_mant_shift = 0,
    f64_exp_shift = f64_mant_size,
    f64_sign_shift = f64_mant_size + f64_exp_size,

    f64_mant_mask = (1ull << f64_mant_size) - 1,
    f64_exp_mask = (1ull << f64_exp_size) - 1,
    f64_sign_mask = 1ull,

    f64_mant_prefix = (1ull << f64_mant_size),
    f64_exp_bias = (1 << (f64_exp_size-1)) - 1
};

static f32 f32_from_bits(u32 v) { union { u32 u; f32 f; } x = { v }; return x.f; }
static u32 f32_to_bits(f32 x)   { union { f32 f; u32 u; } v = { x }; return v.u; }

static u32 f32_mant_dec(f32 x)  { return ( f32_to_bits(x) >> f32_mant_shift ) & f32_mant_mask; }
static u32 f32_exp_dec(f32 x)   { return ( f32_to_bits(x) >> f32_exp_shift  ) & f32_exp_mask;  }
static u32 f32_sign_dec(f32 x)  { return ( f32_to_bits(x) >> f32_sign_shift ) & f32_sign_mask; }

static u32 f32_mant_enc(u32 v)  { return ((v & f32_mant_mask) << f32_mant_shift); }
static u32 f32_exp_enc(u32 v)   { return ((v & f32_exp_mask)  << f32_exp_shift);  }
static u32 f32_sign_enc(u32 v)  { return ((v & f32_sign_mask) << f32_sign_shift); }

static int f32_is_zero(f32 x)   { return f32_exp_dec(x) == 0            && f32_mant_dec(x) == 0; }
static int f32_is_inf(f32 x)    { return f32_exp_dec(x) == f32_exp_mask && f32_mant_dec(x) == 0; }
static int f32_is_nan(f32 x)    { return f32_exp_dec(x) == f32_exp_mask && f32_mant_dec(x) != 0; }
static int f32_is_denorm(f32 x) { return f32_exp_dec(x) == 0            && f32_mant_dec(x) != 0; }

static f32_struct f32_unpack_float(f32 x)
{
    return { f32_mant_dec(x), f32_exp_dec(x), f32_sign_dec(x) };
}

static f32 f32_pack_float(f32_struct s)
{
    return f32_from_bits(f32_mant_enc(s.mant) | f32_exp_enc(s.exp) | f32_sign_enc(s.sign));
}

static f64 f64_from_bits(u64 v) { union { u64 u; f64 f; } x = { v }; return x.f; }
static u64 f64_to_bits(f64 x)   { union { f64 f; u64 u; } v = { x }; return v.u; }

static u64 f64_mant_dec(f64 x)  { return ( f64_to_bits(x) >> f64_mant_shift ) & f64_mant_mask; }
static u64 f64_exp_dec(f64 x)   { return ( f64_to_bits(x) >> f64_exp_shift  ) & f64_exp_mask;  }
static u64 f64_sign_dec(f64 x)  { return ( f64_to_bits(x) >> f64_sign_shift ) & f64_sign_mask; }

static u64 f64_mant_enc(u64 v)  { return ((v & f64_mant_mask) << f64_mant_shift); }
static u64 f64_exp_enc(u64 v)   { return ((v & f64_exp_mask)  << f64_exp_shift);  }
static u64 f64_sign_enc(u64 v)  { return ((v & f64_sign_mask) << f64_sign_shift); }

static int f64_is_zero(f64 x)   { return f64_exp_dec(x) == 0            && f64_mant_dec(x) == 0; }
static int f64_is_inf(f64 x)    { return f64_exp_dec(x) == f64_exp_mask && f64_mant_dec(x) == 0; }
static int f64_is_nan(f64 x)    { return f64_exp_dec(x) == f64_exp_mask && f64_mant_dec(x) != 0; }
static int f64_is_denorm(f64 x) { return f64_exp_dec(x) == 0            && f64_mant_dec(x) != 0; }

static f64_struct f64_unpack_float(f64 x)
{
    return { f64_mant_dec(x), f64_exp_dec(x), f64_sign_dec(x) };
}

static f64 f64_pack_float(f64_struct s)
{
    return f64_from_bits( f64_mant_enc(s.mant) | f64_exp_enc(s.exp) | f64_sign_enc(s.sign) );
}

float _f32_inf() { return std::numeric_limits<float>::infinity(); }
float _f32_nan() { return std::numeric_limits<float>::quiet_NaN(); }
float _f32_snan() { return std::numeric_limits<float>::signaling_NaN(); }
double _f64_inf() { return std::numeric_limits<double>::infinity(); }
double _f64_nan() { return std::numeric_limits<double>::quiet_NaN(); }
double _f64_snan() { return std::numeric_limits<double>::signaling_NaN(); }

/*
 * ISO/IEC 8825-1:2003 8.5 real
 *
 * read and write real
 *
 * ASN.1 REAL encoding bits from first content byte
 *
 * - 8.5.6 encoding-format
 *   - [8]   0b'1 = binary
 *   - [8:7] 0b'00 = decimal
 *   - [8:7] 0b'01 = special real value
 * - 8.5.7.1 sign (if bit 8 = 0b1)
 *   - [7]   sign S
 * - 8.5.7.2 base (if bit 8 = 0b1)
 *   - [6:5] 0b'00 = base 2
 *   - [6:5] 0b'01 = base 8
 *   - [6:5] 0b'10 = base 16
 *   - [6:5] 0b'11 = reserved
 * - 8.5.7.3 scale-factor (if bit 8 = 0b1)
 *   - [4:3] unsigned scale factor F
 * - 8.5.7.4 exponent-format (if bit 8 = 0b1)
 *   - [2:1] 0b'00 = 1-byte signed exponent in 2nd octet
 *   - [2:1] 0b'01 = 2-byte signed exponent in 2nd to 3rd octet
 *   - [2:1] 0b'10 = 3-byte signed exponent in 2nd to 4rd octet
 *   - [2:1] 0b'11 = 2nd octet contains number of exponent octets
 * - 8.5.8 decimal encoding (if bit 8:7 = 0b00)
 *   - [8:1] 0b'00000001 ISO 6093 NR1 form
 *   - [8:1] 0b'00000010 ISO 6093 NR2 form
 *   - [8:1] 0b'00000011 ISO 6093 NR3 form
 * - 8.5.9 special real value (if bit 8:7 = 0b01)
 *   - [8:1] 0b'01000000 Value is PLUS-INFINITY
 *   - [8:1] 0b'01000001 Value is MINUS-INFINITY
 *   - [8:1] 0b'01000010 Value is NOT-A-NUMBER
 *   - [8:1] 0b'01000011 Value is minus zero
 *
 *  binary encoding: M = S × N × 2^F x (2,8,16)^E
 *  decimal encoding: /[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?/
 */

enum {
    _real_fmt_shift         = 6,
    _real_fmt_mask          = 0b11,
    _real_base_shift        = 4,
    _real_base_mask         = 0b11,
    _real_scale_shift       = 2,
    _real_scale_mask        = 0b11,
    _real_exp_shift         = 0,
    _real_exp_mask          = 0b11,
};

enum _real_fmt {
    _real_fmt_decimal       = 0b00,
    _real_fmt_special       = 0b01,
    _real_fmt_binary_pos    = 0b10,
    _real_fmt_binary_neg    = 0b11
};

enum _real_base {
    _real_base_2            = 0b00,
    _real_base_8            = 0b01,
    _real_base_16           = 0b10
};

enum _real_exp {
    _real_exp_1             = 0b00,
    _real_exp_2             = 0b01,
    _real_exp_3             = 0b10,
    _real_exp_n             = 0b11
};

enum _real_decimal_nr {
    _real_decimal_nr_1      = 0b00000001,
    _real_decimal_nr_2      = 0b00000010,
    _real_decimal_nr_3      = 0b00000011,
};

enum _real_special {
    _real_special_pos_inf   = 0b01000000,
    _real_special_neg_inf   = 0b01000001,
    _real_special_neg_zero  = 0b01000010,
    _real_special_nan       = 0b01000011,
};

static _real_fmt _asn1_real_format(u8 x)
{
    return (_real_fmt)((x >> _real_fmt_shift) & _real_fmt_mask);
}

static _real_exp _asn1_real_exp(u8 x)
{
    return (_real_exp)((x >> _real_exp_shift) & _real_exp_mask);
}

static u8 _asn1_real_binary_full(bool sign, _real_exp exp, _real_base base, u8 scale)
{
    return 0x80 | ((u8)sign<<6)  | (((u8)base)<<4) | ((scale&3)<<2) | (u8)exp;
}

static u8 _asn1_real_binary(bool sign, _real_exp exponent)
{
    return _asn1_real_binary_full(sign, exponent, _real_base_2, 0);
}

/*
 * f64_real_data contains fraction, signed exponent, their
 * encoded lengths and flags for sign, infinity, nan and zero.
 */
struct f64_real_data
{
    u64 frac;
    s64 sexp;
    size_t frac_len, exp_len;
    bool sign : 1, inf : 1, nan : 1, zero : 1;
};

/*
 * IEEE 754 exponent is relative to the msb of the mantissa
 * ASN.1 exponent is relative to the lsb of the mantissa
 *
 * right-justify the fraction with the least significant set bit in bit 0,
 * first adding the IEEE 754 implied leading digit 0b1.xxx
 */
static f64_real_data f64_asn1_data_get(double value)
{
    u64 frac;
    s64 sexp;
    size_t frac_tz, frac_lz;

    sexp = (s64)f64_exp_dec(value);
    frac = (s64)f64_mant_dec(value) | (-(s64)(sexp > 0) & f64_mant_prefix);
    frac_tz = ctz(frac);
    frac_lz = clz(frac);
    frac >>= frac_tz;

    if (sexp > 0) {
        sexp += frac_lz + frac_tz - 63 - f64_exp_bias;
    }

    return f64_real_data {
        frac, sexp,
        mu_asn1_ber_integer_u64_length(&frac),
        mu_asn1_ber_integer_s64_length(&sexp),
        !!f64_sign_dec(value), !!f64_is_inf(value),
        !!f64_is_nan(value), !!f64_is_zero(value)
    };
}

size_t mu_asn1_ber_real_f64_length(const double *value)
{
    f64_real_data d = f64_asn1_data_get(*value);

    if (d.zero) {
        return d.sign ? 1 : 3;
    } else if (d.inf || d.nan) {
        return 1;
    } else {
        return 1 + d.exp_len + d.frac_len;
    }
}

size_t mu_asn1_ber_real_f64_length_byval(const double value)
{
    f64_real_data d = f64_asn1_data_get(value);

    if (d.zero) {
        return d.sign ? 1 : 3;
    } else if (d.inf || d.nan) {
        return 1;
    } else {
        return 1 + d.exp_len + d.frac_len;
    }
}

int mu_asn1_ber_real_f64_read(mu_buf *buf, size_t len, double *value)
{
    int8_t b;
    double v = 0;
    _real_fmt fmt;
    _real_exp exp_mode;
    size_t frac_len;
    size_t exp_len;
    u64 frac;
    s64 sexp;
    u64 fexp;
    bool sign;
    size_t frac_lz;

    if (mu_buf_read_i8(buf, &b) != 1) {
        goto err;
    }
    fmt = _asn1_real_format(b);
    switch (b) {
    case _real_special_pos_inf:  *value = std::numeric_limits<f64>::infinity();  return 0;
    case _real_special_neg_inf:  *value = -std::numeric_limits<f64>::infinity(); return 0;
    case _real_special_neg_zero: *value = -0.0;     return 0;
    case _real_special_nan:      *value = std::numeric_limits<f64>::quiet_NaN();  return 0;
    default: break;
    }
    switch (fmt) {
    case _real_fmt_binary_pos: sign = false; break;
    case _real_fmt_binary_neg: sign = true; break;
    default: goto err;
    }
    exp_mode = _asn1_real_exp(b);
    switch(exp_mode) {
    case _real_exp_1: exp_len = 1; break;
    case _real_exp_2: exp_len = 2; break;
    default: goto err;
    }
    frac_len = len - exp_len - 1;

    if (mu_asn1_ber_integer_s64_read(buf, exp_len, &sexp) < 0) {
        goto err;
    }
    if (mu_asn1_ber_integer_u64_read(buf, frac_len, &frac) < 0) {
        goto err;
    }
    frac_lz = clz(frac);

    /*
     * IEEE 754 exponent is relative to the msb of the mantissa
     * ASN.1 exponent is relative to the lsb of the mantissa
     *
     * left-justify the fraction with the most significant set bit in bit 51
     * (0-indexed) cropping off the IEEE 754 implied leading digit 0b1.xxx
     */
    if (frac == 1 && sexp == 0) {
        frac = 0;
        fexp = f64_exp_bias;
    } else if (frac == 0 && sexp == 0) {
        fexp = 0;
    } else {
        frac = (frac << (frac_lz + 1)) >> (64 - f64_mant_size);
        fexp = f64_exp_bias + 63 + sexp - frac_lz;
    }
    if (fexp > f64_exp_mask || frac > f64_mant_mask) {
        return -1;
    }
    v = f64_pack_float(f64_struct{frac, fexp, sign});

    *value = v;
    return 0;
err:
    *value = 0;
    return -1;
}

f64_result mu_asn1_ber_real_f64_read_byval(mu_buf *buf, size_t len)
{
    int8_t b;
    double v = 0;
    _real_fmt fmt;
    _real_exp exp_mode;
    size_t frac_len;
    size_t exp_len;
    u64 frac;
    s64 sexp;
    u64 fexp;
    bool sign;
    size_t frac_lz;
    s64_result rexp;
    u64_result rfrac;

    if (mu_buf_read_i8(buf, &b) != 1) {
        return f64_result { 0, -1 };
    }
    fmt = _asn1_real_format(b);
    switch (b) {
    case _real_special_pos_inf:  return f64_result { std::numeric_limits<f64>::infinity(), 0 };
    case _real_special_neg_inf:  return f64_result { -std::numeric_limits<f64>::infinity(), 0 };
    case _real_special_neg_zero: return f64_result { -0.0, 0 };
    case _real_special_nan:      return f64_result { std::numeric_limits<f64>::quiet_NaN(), 0 };
    default: break;
    }
    switch (fmt) {
    case _real_fmt_binary_pos: sign = false; break;
    case _real_fmt_binary_neg: sign = true; break;
    default: return f64_result { 0, -1 };
    }
    exp_mode = _asn1_real_exp(b);
    switch(exp_mode) {
    case _real_exp_1: exp_len = 1; break;
    case _real_exp_2: exp_len = 2; break;
    default: return f64_result { 0, -1 };
    }
    frac_len = len - exp_len - 1;

    rexp = mu_asn1_ber_integer_s64_read_byval(buf, exp_len);
    if (rexp.error < 0) {
        return f64_result { 0, rexp.error };
    }
    sexp = rexp.value;
    rfrac = mu_asn1_ber_integer_u64_read_byval(buf, frac_len);
    if (rfrac.error < 0) {
        return f64_result { 0, rfrac.error };
    }
    frac = rfrac.value;
    frac_lz = clz(frac);

    /*
     * IEEE 754 exponent is relative to the msb of the mantissa
     * ASN.1 exponent is relative to the lsb of the mantissa
     *
     * left-justify the fraction with the most significant set bit in bit 51
     * (0-indexed) cropping off the IEEE 754 implied leading digit 0b1.xxx
     */
    if (frac == 1 && sexp == 0) {
        frac = 0;
        fexp = f64_exp_bias;
    } else if (frac == 0 && sexp == 0) {
        fexp = 0;
    } else {
        frac = (frac << (frac_lz + 1)) >> (64 - f64_mant_size);
        fexp = f64_exp_bias + 63 + sexp - frac_lz;
    }
    if (fexp > f64_exp_mask || frac > f64_mant_mask) {
        return f64_result { 0, -1 };
    }
    v = f64_pack_float(f64_struct{frac, fexp, sign});

    return f64_result { v, 0 };
}

int mu_asn1_ber_real_f64_write(mu_buf *buf, size_t len, const double *value)
{
    f64_real_data d = f64_asn1_data_get(*value);

    int8_t b;

    if (d.zero && d.sign) {
        b = _real_special_neg_zero;
    }
    else if (d.inf) {
        b = d.sign ? _real_special_neg_inf : _real_special_pos_inf;
    }
    else if (d.nan) {
        b = _real_special_nan;
    } else {
        _real_exp exp_code;
        switch(d.exp_len) {
        case 1: exp_code = _real_exp_1; break;
        case 2: exp_code = _real_exp_2; break;
        default: return -1;
        }
        b = _asn1_real_binary(d.sign, exp_code);
    }
    if (mu_buf_write_i8(buf, b) != 1) {
        return -1;
    }
    if ((d.zero && d.sign) || d.inf || d.nan) {
        return 0;
    }
    if (mu_asn1_ber_integer_s64_write(buf, d.exp_len, &d.sexp) < 0) {
        return -1;
    }
    if (mu_asn1_ber_integer_u64_write(buf, d.frac_len, &d.frac) < 0) {
        return -1;
    }

    return 0;
}

int mu_asn1_ber_real_f64_write_byval(mu_buf *buf, size_t len, const double value)
{
    f64_real_data d = f64_asn1_data_get(value);

    int8_t b;

    if (d.zero && d.sign) {
        b = _real_special_neg_zero;
    }
    else if (d.inf) {
        b = d.sign ? _real_special_neg_inf : _real_special_pos_inf;
    }
    else if (d.nan) {
        b = _real_special_nan;
    } else {
        _real_exp exp_code;
        switch(d.exp_len) {
        case 1: exp_code = _real_exp_1; break;
        case 2: exp_code = _real_exp_2; break;
        default: return -1;
        }
        b = _asn1_real_binary(d.sign, exp_code);
    }
    if (mu_buf_write_i8(buf, b) != 1) {
        return -1;
    }
    if ((d.zero && d.sign) || d.inf || d.nan) {
        return 0;
    }
    if (mu_asn1_ber_integer_s64_write_byval(buf, d.exp_len, d.sexp) < 0) {
        return -1;
    }
    if (mu_asn1_ber_integer_u64_write_byval(buf, d.frac_len, d.frac) < 0) {
        return -1;
    }

    return 0;
}

int mu_asn1_der_real_f64_read(mu_buf *buf, asn1_tag _tag, double *value)
{
    asn1_hdr hdr;
    if (mu_asn1_ber_ident_read(buf, &hdr._id) < 0) return -1;
    if (mu_asn1_ber_length_read(buf, &hdr._length) < 0) return -1;
    return mu_asn1_ber_real_f64_read(buf, hdr._length, value);
}

f64_result mu_asn1_der_real_f64_read_byval(mu_buf *buf, asn1_tag _tag)
{
    asn1_hdr hdr;
    if (mu_asn1_ber_ident_read(buf, &hdr._id) < 0) return f64_result { 0, -1 };
    if (mu_asn1_ber_length_read(buf, &hdr._length) < 0) return f64_result { 0, -1 };
    return mu_asn1_ber_real_f64_read_byval(buf, hdr._length);
}

int mu_asn1_der_real_f64_write(mu_buf *buf, asn1_tag _tag, const double *value)
{
    asn1_hdr hdr = {
        { (u64)_tag, 0, asn1_class_universal }, mu_asn1_ber_real_f64_length(value)
    };

    if (mu_asn1_ber_ident_write(buf, hdr._id) < 0) return -1;
    if (mu_asn1_ber_length_write(buf, hdr._length) < 0) return -1;
    return mu_asn1_ber_real_f64_write(buf, hdr._length, value);
}

int mu_asn1_der_real_f64_write_byval(mu_buf *buf, asn1_tag _tag, const double value)
{
    asn1_hdr hdr = {
        { (u64)_tag, 0, asn1_class_universal }, mu_asn1_ber_real_f64_length_byval(value)
    };

    if (mu_asn1_ber_ident_write(buf, hdr._id) < 0) return -1;
    if (mu_asn1_ber_length_write(buf, hdr._length) < 0) return -1;
    return mu_asn1_ber_real_f64_write_byval(buf, hdr._length, value);
}

/*
 * vf8 compressed float - f64
 */

/*
 * mu_vf128_f64_resultdata contains fraction, signed exponent, their
 * encoded lengths and flags for sign, infinity, nan and zero.
 */
struct mu_vf128_f64_resultdata
{
    bool sign;
    s64 sexp;
    u64 frac;
};

/*
 * extract exponent and left-justified fraction
 */
static mu_vf128_f64_resultdata mu_vf128_f64_resultdata_get(double value)
{
    bool sign = !!f64_sign_dec(value);
    s64 sexp = (s64)(f64_exp_dec(value) - f64_exp_bias);
    u64 frac = (u64)f64_mant_dec(value) << (f64_exp_size + 1);

    return mu_vf128_f64_resultdata { sign, sexp, frac };
}

#if DEBUG_ENCODING
static void _mu_vf128_f64_debug(double v, u8 pre, s64 vp_exp, u64 vp_man, s64 vd_exp, u64 vd_man)
{
    bool vf_inl = ! ((pre >> 7) & 1);
    bool vf_sgn =    (pre >> 6) & 1;
    int  vf_exp =    (pre >> 4) & 3;
    int  vf_man =     pre       & 15;

    printf("\n%16s %20s -> %18s %5s -> %1s %1s %2s %4s %4s\n",
        "value (dec)", "value (hex)", "fraction", "exp",
        "i", "s", "ex", "mant", "len");
    printf("%16f %20a    0x%016llx %05lld    %1u %1u %c%c %c%c%c%c",
        v, v, vp_man, vp_exp, vf_inl, vf_sgn,
        '0' + ((vf_exp >> 1) & 1),
        '0' + ((vf_exp >> 0) & 1),
        '0' + ((vf_man >> 3) & 1),
        '0' + ((vf_man >> 2) & 1),
        '0' + ((vf_man >> 1) & 1),
        '0' + ((vf_man >> 0) & 1));

    printf(" [%02d] { pre=0x%02hhx", 1 + (vf_inl ? 0 : vf_exp + vf_man), pre);
    if (!vf_inl && vf_man) {
        printf(" man=0x%02llx", vd_man);
    }
    if (!vf_inl && vf_exp) {
        printf(" exp=%lld", vd_exp);
    }
    printf(" }\n");
}
#endif

int mu_vf128_f64_read(mu_buf *buf, double *value)
{
    s8 pre;
    double v = 0;
    bool vf_inl;
    bool vf_sgn;
    int vf_exp;
    int vf_man;
    u64 vr_man = 0;
    s64 vr_exp = 0;
    u64 vp_man = 0;
    s64 vp_exp = 0;

    if (mu_buf_read_i8(buf, &pre) != 1) {
        goto err;
    }

    vf_inl = ! ((pre >> 7) & 1);
    vf_sgn =    (pre >> 6) & 1;
    vf_exp =    (pre >> 4) & 3;
    vf_man =     pre       & 15;

    if (!vf_inl) {
        if (vf_exp && mu_le_ber_integer_s64_read(buf, vf_exp, &vr_exp) < 0) {
            goto err;
        }
        if (vf_man && mu_le_ber_integer_u64_read(buf, vf_man, &vr_man) < 0) {
            goto err;
        }
    }

    /* inline exponent and mantissa using float7 */
    if (vf_inl) {
        if (vf_exp == 0) {
            if (vf_man > 0) {
                size_t lz = clz((u64)vf_man);
                /* inline subnormal - normalize by calculating exponent
                 * based on the leading zero count for the 4 bits right
                 * of the point hence 59 = (63 - 4) then left-justify
                 * the mantissa and truncate the leading 1. */
                vp_exp = f64_exp_bias + 59 - lz;
                vp_man = ((u64)vf_man << (lz + 1)) >> (f64_exp_size + 1);
            } else {
                /* Zero */
                vp_exp = 0;
                vp_man = 0;
            }
        }
        else if (vf_exp == 3) {
            /* inline Inf/NaN - set exponent then left-justify the mantissa,
             * containing 0b0000 for infinity or 0b1000 for canonical NaN. */
            vp_exp = f64_exp_mask;
            vp_man = (u64)vf_man << (f64_mant_size - 4);
        }
        else {
            /* inline normal - adjust exponent bias from 2-bit bias 1 to
             * the IEEE 754 bias then left-justify the mantissa. */
            vp_exp = f64_exp_bias + vf_exp - 1;
            vp_man = (u64)vf_man << (f64_mant_size - 4);
        }
    }
    /* out-of-line little-endian exponent and mantissa */
    else {
        size_t lz = clz(vr_man), tz = ctz(vr_man);
        if (vr_exp <= -(s64)f64_exp_bias) {
            /* normal to subnormal - calculate shift using exponent delta
             * then left-justify the mantissa preserving the leading 1. */
            assert(vr_exp >= -(s64)f64_exp_bias - f64_mant_size);
            size_t sh = f64_exp_bias + vr_exp + lz - f64_exp_size;
            vp_exp = 0;
            vp_man = (u64)vr_man << sh;
        } else {
            /* normal - if no exponent, mantissa is a fraction in the range
             * +/-0.9900.. with a unary prefix containing the exponent. */
            if (vf_exp == 0) vr_exp = -tz - 1;
            vp_exp = f64_exp_bias + vr_exp;
            vp_man = (u64)vr_man << (lz + 1) >> (f64_exp_size + 1);
        }
    }

    v = f64_pack_float(f64_struct{vp_man, (u64)vp_exp, vf_sgn});
    *value = v;

#if DEBUG_ENCODING
    _mu_vf128_f64_debug(v, pre, vp_exp - f64_exp_bias, vp_man << 12, vr_exp, vr_man);
#endif

    return 0;
err:
    *value = 0;
    return -1;
}

enum : u64 {
    u64_msb = 0x8000000000000000ull,
    u64_msn = 0xf000000000000000ull
};

f64_result mu_vf128_f64_read_byval(mu_buf *buf)
{
    s8 pre;
    double v = 0;
    bool vf_inl;
    bool vf_sgn;
    int vf_exp;
    int vf_man;
    u64 vr_man = 0;
    s64 vr_exp = 0;
    u64 vp_man = 0;
    s64 vp_exp = 0;

    if (mu_buf_read_i8(buf, &pre) != 1) {
        return f64_result { 0, -1 };
    }

    vf_inl = ! ((pre >> 7) & 1);
    vf_sgn =    (pre >> 6) & 1;
    vf_exp =    (pre >> 4) & 3;
    vf_man =     pre       & 15;

    if (!vf_inl) {
        if (vf_exp) {
            s64_result r = mu_le_ber_integer_s64_read_byval(buf, vf_exp);
            if (r.error < 0) return f64_result { 0, r.error };
            vr_exp = r.value;
        }
        if (vf_man) {
            u64_result r = mu_le_ber_integer_u64_read_byval(buf, vf_man);
            if (r.error < 0) return f64_result { 0, r.error };
            vr_man = r.value;
        }
    }

    /* inline exponent and mantissa using float7 */
    if (vf_inl) {
        if (vf_exp == 0) {
            if (vf_man > 0) {
                size_t lz = clz((u64)vf_man);
                /* inline subnormal - normalize by calculating exponent
                 * based on the leading zero count for the 4 bits right
                 * of the point hence 59 = (63 - 4) then left-justify
                 * the mantissa and truncate the leading 1. */
                vp_exp = f64_exp_bias + 59 - lz;
                vp_man = ((u64)vf_man << (lz + 1)) >> (f64_exp_size + 1);
            } else {
                /* Zero */
                vp_exp = 0;
                vp_man = 0;
            }
        }
        else if (vf_exp == 3) {
            /* inline Inf/NaN - set exponent then left-justify the mantissa,
             * containing 0b0000 for infinity or 0b1000 for canonical NaN. */
            vp_exp = f64_exp_mask;
            vp_man = (u64)vf_man << (f64_mant_size - 4);
        }
        else {
            /* inline normal - adjust exponent bias from 2-bit bias 1 to
             * the IEEE 754 bias then left-justify the mantissa. */
            vp_exp = f64_exp_bias + vf_exp - 1;
            vp_man = (u64)vf_man << (f64_mant_size - 4);
        }
    }
    /* out-of-line little-endian exponent and mantissa */
    else {
        size_t lz = clz(vr_man), tz = ctz(vr_man);
        if (vr_exp <= -(s64)f64_exp_bias) {
            /* normal to subnormal - calculate shift using exponent delta
             * then left-justify the mantissa preserving the leading 1. */
            assert(vr_exp >= -(s64)f64_exp_bias - f64_mant_size);
            size_t sh = f64_exp_bias + vr_exp + lz - f64_exp_size;
            vp_exp = 0;
            vp_man = (u64)vr_man << sh;
        } else {
            /* normal - if no exponent, mantissa is a fraction in the range
             * +/-0.9900.. with a unary prefix containing the exponent. */
            if (vf_exp == 0) vr_exp = -tz - 1;
            vp_exp = f64_exp_bias + vr_exp;
            vp_man = (u64)vr_man << (lz + 1) >> (f64_exp_size + 1);
        }
    }

    v = f64_pack_float(f64_struct{vp_man, (u64)vp_exp, vf_sgn});

#if DEBUG_ENCODING
    _mu_vf128_f64_debug(v, pre, vp_exp - f64_exp_bias, vp_man << 12, vr_exp, vr_man);
#endif

    return f64_result { v, 0 };
}

int mu_vf128_f64_write(mu_buf *buf, const double *value)
{
    s8 pre;
    double v = *value;
    mu_vf128_f64_resultdata d = mu_vf128_f64_resultdata_get(v);
    int vf_exp = 0;
    int vf_man = 0;
    u64 vw_man = 0;
    s64 vw_exp = 0;

    // Inf/NaN
    if (d.sexp == f64_exp_bias + 1) {
        vf_exp = 3;
        vf_man = (d.frac != 0) << 3;
        pre = (d.sign << 6) | (vf_exp << 4) | vf_man;
    }
    // Zero
    else if (d.sexp == -(s64)f64_exp_bias && d.frac == 0) {
        pre = (d.sign << 6);
    }
    // Inline (normal)
    else if (d.sexp <= 1 && d.sexp >= 0 &&
             (d.frac & u64_msn) == d.frac) {
        pre = (d.sign << 6) | (u8)((d.sexp+1) << 4) | (u8)(d.frac >> 60);
    }
    // Inline (subnormal)
    else if (d.sexp <= -1 && d.sexp >= -4 &&
             ((d.frac >> -d.sexp) & u64_msn) == (d.frac >> -d.sexp)) {
        pre = (d.sign << 6) | (u8)((0x10 | (d.frac >> 60)) >> -d.sexp);
    }
    // Out-of-line
    else {
        size_t tz = ctz(d.frac), lz = clz(d.frac);
        /*
         * 1. renormalize subnormal fraction (leading one preserved)
         * 2. omit fraction for powers of two (fraction is zero).
         * 3. omit exponent for some normal values (exponent unary prefix)
         * 4. otherwise encode both exponent and fraction
         */
        if (d.sexp == -(s64)f64_exp_bias) {
            vw_man = d.frac >> tz;
            vw_exp = d.sexp - lz - 1;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            vf_man = (u8)mu_le_ber_integer_u64_length_byval(vw_man);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        else if (d.frac == 0) {
            vw_exp = d.sexp;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4);
        }
        else if (d.sexp < 0 && d.sexp >= -8) {
            /*
             * - compressing -0.99999.. - 0.99999.. excluding +/-0.
             * - fixed point fraction with implied exponent of e0 relative
             *   to the most significant bit in the encoded mantissa.
             * - bit 7 of the left-most byte of the fraction is 0.5.
             * - prepend exponent as a unary code in the mantissa lsb.
             * - compare/choose compressed or normal representation.
             */
            size_t sh = -d.sexp - 1;
            u64 vw_man_a = (d.frac >> tz) | (u64_msb >> (tz - 1));
            u64 vw_man_b = ((d.frac >> tz) << sh) | ((u64_msb >> (tz - 1)) << sh);
            int vf_exp_a = (u8)mu_le_ber_integer_s64_length_byval(d.sexp);
            int vf_man_a = (u8)mu_le_ber_integer_u64_length_byval(vw_man_a);
            int vf_man_b = (u8)mu_le_ber_integer_u64_length_byval(vw_man_b);
            if (vf_man_a + vf_exp_a < vf_man_b) {
                vw_man = vw_man_a;
                vw_exp = d.sexp;
                vf_exp = vf_exp_a;
                vf_man = vf_man_a;
            } else {
                vw_man = vw_man_b;
                vf_man = vf_man_b;
            }
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        else {
            vw_man = (d.frac >> tz) | (u64_msb >> (tz - 1));
            vw_exp = d.sexp;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            vf_man = (u8)mu_le_ber_integer_u64_length_byval(vw_man);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        /* vf_exp and vf_man contain length of exponent and fraction in bytes */
    }

    if (mu_buf_write_i8(buf, pre) != 1) {
        return -1;
    }

    if ((pre & 0x80)) {
        if (vf_exp && mu_le_ber_integer_s64_write_byval(buf, vf_exp, vw_exp) < 0) {
            return -1;
        }
        if (vf_man && mu_le_ber_integer_u64_write_byval(buf, vf_man, vw_man) < 0) {
            return -1;
        }
    }

#if DEBUG_ENCODING
    _mu_vf128_f64_debug(v, pre, d.sexp, d.frac, vw_exp, vw_man);
#endif

    return 0;
}

int mu_vf128_f64_write_byval(mu_buf *buf, const double value)
{
    s8 pre;
    const double v = value;
    mu_vf128_f64_resultdata d = mu_vf128_f64_resultdata_get(v);
    int vf_exp = 0;
    int vf_man = 0;
    u64 vw_man = 0;
    s64 vw_exp = 0;

    // Inf/NaN
    if (d.sexp == f64_exp_bias + 1) {
        vf_exp = 3;
        vf_man = (d.frac != 0) << 3;
        pre = (d.sign << 6) | (vf_exp << 4) | vf_man;
    }
    // Zero
    else if (d.sexp == -(s64)f64_exp_bias && d.frac == 0) {
        pre = (d.sign << 6);
    }
    // Inline (normal)
    else if (d.sexp <= 1 && d.sexp >= 0 &&
             (d.frac & u64_msn) == d.frac) {
        pre = (d.sign << 6) | (u8)((d.sexp+1) << 4) | (u8)(d.frac >> 60);
    }
    // Inline (subnormal)
    else if (d.sexp <= -1 && d.sexp >= -4 &&
             ((d.frac >> -d.sexp) & u64_msn) == (d.frac >> -d.sexp)) {
        pre = (d.sign << 6) | (u8)((0x10 | (d.frac >> 60)) >> -d.sexp);
    }
    // Out-of-line
    else {
        size_t tz = ctz(d.frac), lz = clz(d.frac);
        /*
         * 1. renormalize subnormal fraction (leading one preserved)
         * 2. omit fraction for powers of two (fraction is zero).
         * 3. omit exponent for some normal values (exponent unary prefix)
         * 4. otherwise encode both exponent and fraction
         */
        if (d.sexp == -(s64)f64_exp_bias) {
            vw_man = d.frac >> tz;
            vw_exp = d.sexp - lz - 1;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            vf_man = (u8)mu_le_ber_integer_u64_length_byval(vw_man);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        else if (d.frac == 0) {
            vw_exp = d.sexp;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4);
        }
        else if (d.sexp < 0 && d.sexp >= -8) {
            /*
             * - compressing -0.99999.. - 0.99999.. excluding +/-0.
             * - fixed point fraction with implied exponent of e0 relative
             *   to the most significant bit in the encoded mantissa.
             * - bit 7 of the left-most byte of the fraction is 0.5.
             * - prepend exponent as a unary code in the mantissa lsb.
             * - compare/choose compressed or normal representation.
             */
            size_t sh = -d.sexp - 1;
            u64 vw_man_a = (d.frac >> tz) | (u64_msb >> (tz - 1));
            u64 vw_man_b = ((d.frac >> tz) << sh) | ((u64_msb >> (tz - 1)) << sh);
            int vf_exp_a = (u8)mu_le_ber_integer_s64_length_byval(d.sexp);
            int vf_man_a = (u8)mu_le_ber_integer_u64_length_byval(vw_man_a);
            int vf_man_b = (u8)mu_le_ber_integer_u64_length_byval(vw_man_b);
            if (vf_man_a + vf_exp_a < vf_man_b) {
                vw_man = vw_man_a;
                vw_exp = d.sexp;
                vf_exp = vf_exp_a;
                vf_man = vf_man_a;
            } else {
                vw_man = vw_man_b;
                vf_man = vf_man_b;
            }
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        else {
            vw_man = (d.frac >> tz) | (u64_msb >> (tz - 1));
            vw_exp = d.sexp;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            vf_man = (u8)mu_le_ber_integer_u64_length_byval(vw_man);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        /* vf_exp and vf_man contain length of exponent and fraction in bytes */
    }

    if (mu_buf_write_i8(buf, pre) != 1) {
        return -1;
    }

    if ((pre & 0x80)) {
        if (vf_exp && mu_le_ber_integer_s64_write_byval(buf, vf_exp, vw_exp) < 0) {
            return -1;
        }
        if (vf_man && mu_le_ber_integer_u64_write_byval(buf, vf_man, vw_man) < 0) {
            return -1;
        }
    }

#if DEBUG_ENCODING
    _mu_vf128_f64_debug(v, pre, d.sexp, d.frac, vw_exp, vw_man);
#endif

    return 0;
}

/*
 * vf8 compressed float - f32
 */

/*
 * mu_vf128_f32_resultdata contains fraction, signed exponent, their
 * encoded lengths and flags for sign, infinity, nan and zero.
 */
struct mu_vf128_f32_resultdata
{
    bool sign;
    s32 sexp;
    u32 frac;
};

/*
 * extract exponent and left-justified fraction
 */
static mu_vf128_f32_resultdata mu_vf128_f32_resultdata_get(float value)
{
    bool sign = !!f32_sign_dec(value);
    s32 sexp = (s32)(f32_exp_dec(value) - f32_exp_bias);
    u32 frac = (u32)f32_mant_dec(value) << (f32_exp_size + 1);

    return mu_vf128_f32_resultdata { sign, sexp, frac };
}

#if DEBUG_ENCODING
static void _mu_vf128_f32_debug(float v, u8 pre, s32 vp_exp, u32 vp_man, s32 vd_exp, u32 vd_man)
{
    bool vf_inl = ! ((pre >> 7) & 1);
    bool vf_sgn =    (pre >> 6) & 1;
    int  vf_exp =    (pre >> 4) & 3;
    int  vf_man =     pre       & 15;

    printf("\n%9s %20s -> %18s %5s -> %1s %1s %2s %4s %4s\n",
        "value (dec)", "value (hex)", "fraction", "exp",
        "i", "s", "ex", "mant", "len");
    printf("%8f %20a    0x%08x %05d    %1u %1u %c%c %c%c%c%c",
        v, v, vp_man, vp_exp, vf_inl, vf_sgn,
        '0' + ((vf_exp >> 1) & 1),
        '0' + ((vf_exp >> 0) & 1),
        '0' + ((vf_man >> 3) & 1),
        '0' + ((vf_man >> 2) & 1),
        '0' + ((vf_man >> 1) & 1),
        '0' + ((vf_man >> 0) & 1));

    printf(" [%02d] { pre=0x%02hhx", 1 + (vf_inl ? 0 : vf_exp + vf_man), pre);
    if (!vf_inl && vf_man) {
        printf(" man=0x%02x", vd_man);
    }
    if (!vf_inl && vf_exp) {
        printf(" exp=%d", vd_exp);
    }
    printf(" }\n");
}
#endif

int mu_vf128_f32_read(mu_buf *buf, float *value)
{
    s8 pre;
    float v = 0;
    bool vf_inl;
    bool vf_sgn;
    int vf_exp;
    int vf_man;
    u32 vr_man = 0;
    s32 vr_exp = 0;
    u32 vp_man = 0;
    s32 vp_exp = 0;

    if (mu_buf_read_i8(buf, &pre) != 1) {
        goto err;
    }

    vf_inl = ! ((pre >> 7) & 1);
    vf_sgn =    (pre >> 6) & 1;
    vf_exp =    (pre >> 4) & 3;
    vf_man =     pre       & 15;

    if (!vf_inl) {
        if (vf_exp) {
            s64_result r = mu_le_ber_integer_s64_read_byval(buf, vf_exp);
            if (r.error < 0) goto err;
            vr_exp = (s32)r.value;
        }
        if (vf_man) {
            u64_result r = mu_le_ber_integer_u64_read_byval(buf, vf_man);
            if (r.error < 0) goto err;

            /* if there are less than 32 leading zeros, then we must
             * truncate some precision from the right-most bits. */
            size_t lz = clz(r.value);
            size_t sh = lz < 32 ? 32 - lz : 0;
            vr_man = (u32)(r.value >> sh);
        }
    }

    /* inline exponent and mantissa using float7 */
    if (vf_inl) {
        if (vf_exp == 0) {
            if (vf_man > 0) {
                size_t lz = clz((u32)vf_man);
                /* inline subnormal - normalize by calculating exponent
                 * based on the leading zero count for the 4 bits right
                 * of the point hence 27 = (31 - 4) then left-justify
                 * the mantissa and truncate the leading 1. */
                vp_exp = f32_exp_bias + 27 - (u32)lz;
                vp_man = ((u32)vf_man << (lz + 1)) >> (f32_exp_size + 1);
            } else {
                /* Zero */
                vp_exp = 0;
                vp_man = 0;
            }
        }
        else if (vf_exp == 3) {
            /* inline Inf/NaN - set exponent then left-justify the mantissa,
             * containing 0b0000 for infinity or 0b1000 for canonical NaN. */
            vp_exp = f32_exp_mask;
            vp_man = (u32)vf_man << (f32_mant_size - 4);
        }
        else {
            /* inline normal - adjust exponent bias from 2-bit bias 1 to
             * the IEEE 754 bias then left-justify the mantissa. */
            vp_exp = f32_exp_bias + vf_exp - 1;
            vp_man = (u32)vf_man << (f32_mant_size - 4);
        }
    }
    /* out-of-line little-endian exponent and mantissa */
    else {
        size_t lz = clz(vr_man), tz = ctz(vr_man);
        if (vr_exp <= -(s32)f32_exp_bias) {
            /* normal to subnormal - calculate shift using exponent delta
             * then left-justify the mantissa preserving the leading 1. */
            assert(vr_exp >= -(s32)f32_exp_bias - f32_mant_size);
            size_t sh = f32_exp_bias + vr_exp + (u32)lz - f32_exp_size;
            vp_exp = 0;
            vp_man = (u32)vr_man << sh;
        } else {
            /* normal - if no exponent, mantissa is a fraction in the range
             * +/-0.9900.. with a unary prefix containing the exponent. */
            if (vf_exp == 0) vr_exp = -(s32)tz - 1;
            vp_exp = f32_exp_bias + vr_exp;
            vp_man = (u32)vr_man << (lz + 1) >> (f32_exp_size + 1);
        }
    }

    v = f32_pack_float(f32_struct{vp_man, (u32)vp_exp, vf_sgn});
    *value = v;

#if DEBUG_ENCODING
    _mu_vf128_f32_debug(v, pre, vp_exp - f32_exp_bias, vp_man << 9, vr_exp, vr_man);
#endif

    return 0;
err:
    *value = 0;
    return -1;
}

enum : u32 {
    u32_msb = 0x80000000u,
    u32_msn = 0xf0000000u
};

f32_result mu_vf128_f32_read_byval(mu_buf *buf)
{
    s8 pre;
    float v = 0;
    bool vf_inl;
    bool vf_sgn;
    int vf_exp;
    int vf_man;
    u32 vr_man = 0;
    s32 vr_exp = 0;
    u32 vp_man = 0;
    s32 vp_exp = 0;

    if (mu_buf_read_i8(buf, &pre) != 1) {
        return f32_result { 0, -1 };
    }

    vf_inl = ! ((pre >> 7) & 1);
    vf_sgn =    (pre >> 6) & 1;
    vf_exp =    (pre >> 4) & 3;
    vf_man =     pre       & 15;

    if (!vf_inl) {
        if (vf_exp) {
            s64_result r = mu_le_ber_integer_s64_read_byval(buf, vf_exp);
            if (r.error < 0) return f32_result { 0, (s32)r.error };
            vr_exp = (s32)r.value;
        }
        if (vf_man) {
            u64_result r = mu_le_ber_integer_u64_read_byval(buf, vf_man);
            if (r.error < 0) return f32_result { 0, (s32)r.error };

            /* if there are less than 32 leading zeros, then we must
             * truncate some precision from the right-most bits. */
            size_t lz = clz(r.value);
            size_t sh = lz < 32 ? 32 - lz : 0;
            vr_man = (u32)(r.value >> sh);
        }
    }

    /* inline exponent and mantissa using float7 */
    if (vf_inl) {
        if (vf_exp == 0) {
            if (vf_man > 0) {
                size_t lz = clz((u32)vf_man);
                /* inline subnormal - normalize by calculating exponent
                 * based on the leading zero count for the 4 bits right
                 * of the point hence 27 = (31 - 4) then left-justify
                 * the mantissa and truncate the leading 1. */
                vp_exp = f32_exp_bias + 27 - (u32)lz;
                vp_man = ((u32)vf_man << (lz + 1)) >> (f32_exp_size + 1);
            } else {
                /* Zero */
                vp_exp = 0;
                vp_man = 0;
            }
        }
        else if (vf_exp == 3) {
            /* inline Inf/NaN - set exponent then left-justify the mantissa,
             * containing 0b0000 for infinity or 0b1000 for canonical NaN. */
            vp_exp = f32_exp_mask;
            vp_man = (u32)vf_man << (f32_mant_size - 4);
        }
        else {
            /* inline normal - adjust exponent bias from 2-bit bias 1 to
             * the IEEE 754 bias then left-justify the mantissa. */
            vp_exp = f32_exp_bias + vf_exp - 1;
            vp_man = (u32)vf_man << (f32_mant_size - 4);
        }
    }
    /* out-of-line little-endian exponent and mantissa */
    else {
        size_t lz = clz(vr_man), tz = ctz(vr_man);
        if (vr_exp <= -(s32)f32_exp_bias) {
            /* normal to subnormal - calculate shift using exponent delta
             * then left-justify the mantissa preserving the leading 1. */
            assert(vr_exp >= -(s32)f32_exp_bias - f32_mant_size);
            size_t sh = f32_exp_bias + vr_exp + (u32)lz - f32_exp_size;
            vp_exp = 0;
            vp_man = (u32)vr_man << sh;
        } else {
            /* normal - if no exponent, mantissa is a fraction in the range
             * +/-0.9900.. with a unary prefix containing the exponent. */
            if (vf_exp == 0) vr_exp = -(s32)tz - 1;
            vp_exp = f32_exp_bias + vr_exp;
            vp_man = (u32)vr_man << (lz + 1) >> (f32_exp_size + 1);
        }
    }

    v = f32_pack_float(f32_struct{vp_man, (u32)vp_exp, vf_sgn});

#if DEBUG_ENCODING
    _mu_vf128_f32_debug(v, pre, vp_exp - f32_exp_bias, vp_man << 9, vr_exp, vr_man);
#endif

    return f32_result { v, 0 };
}

int mu_vf128_f32_write(mu_buf *buf, const float *value)
{
    s8 pre;
    float v = *value;
    mu_vf128_f32_resultdata d = mu_vf128_f32_resultdata_get(v);
    int vf_exp = 0;
    int vf_man = 0;
    u32 vw_man = 0;
    s32 vw_exp = 0;

    // Inf/NaN
    if (d.sexp == f32_exp_bias + 1) {
        vf_exp = 3;
        vf_man = (d.frac != 0) << 3;
        pre = (d.sign << 6) | (vf_exp << 4) | vf_man;
    }
    // Zero
    else if (d.sexp == -(s32)f32_exp_bias && d.frac == 0) {
        pre = (d.sign << 6);
    }
    // Inline (normal)
    else if (d.sexp <= 1 && d.sexp >= 0 &&
             (d.frac & u32_msn) == d.frac) {
        pre = (d.sign << 6) | (u8)((d.sexp+1) << 4) | (u8)(d.frac >> 28);
    }
    // Inline (subnormal)
    else if (d.sexp <= -1 && d.sexp >= -4 &&
             ((d.frac >> -d.sexp) & u32_msn) == (d.frac >> -d.sexp)) {
        pre = (d.sign << 6) | (u8)((0x10 | (d.frac >> 28)) >> -d.sexp);
    }
    // Out-of-line
    else {
        size_t tz = ctz(d.frac), lz = clz(d.frac);
        /*
         * 1. renormalize subnormal fraction (leading one preserved)
         * 2. omit fraction for powers of two (fraction is zero).
         * 3. omit exponent for some normal values (exponent unary prefix)
         * 4. otherwise encode both exponent and fraction
         */
        if (d.sexp == -(s32)f32_exp_bias) {
            vw_man = d.frac >> tz;
            vw_exp = d.sexp - (u32)lz - 1;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            vf_man = (u8)mu_le_ber_integer_u64_length_byval(vw_man);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        else if (d.frac == 0) {
            vw_exp = d.sexp;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4);
        }
        else if (d.sexp < 0 && d.sexp >= -8) {
            /*
             * - compressing -0.99999.. - 0.99999.. excluding +/-0.
             * - fixed point fraction with implied exponent of e0 relative
             *   to the most significant bit in the encoded mantissa.
             * - bit 7 of the left-most byte of the fraction is 0.5.
             * - prepend exponent as a unary code in the mantissa lsb.
             * - compare/choose compressed or normal representation.
             */
            size_t sh = -d.sexp - 1;
            u32 vw_man_a = (d.frac >> tz) | (u32_msb >> (tz - 1));
            u32 vw_man_b = ((d.frac >> tz) << sh) | ((u32_msb >> (tz - 1)) << sh);
            int vf_exp_a = (u8)mu_le_ber_integer_s64_length_byval(d.sexp);
            int vf_man_a = (u8)mu_le_ber_integer_u64_length_byval(vw_man_a);
            int vf_man_b = (u8)mu_le_ber_integer_u64_length_byval(vw_man_b);
            if (vf_man_a + vf_exp_a < vf_man_b) {
                vw_man = vw_man_a;
                vw_exp = d.sexp;
                vf_exp = vf_exp_a;
                vf_man = vf_man_a;
            } else {
                vw_man = vw_man_b;
                vf_man = vf_man_b;
            }
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        else {
            vw_man = (d.frac >> tz) | (u32_msb >> (tz - 1));
            vw_exp = d.sexp;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            vf_man = (u8)mu_le_ber_integer_u64_length_byval(vw_man);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        /* vf_exp and vf_man contain length of exponent and fraction in bytes */
    }

    if (mu_buf_write_i8(buf, pre) != 1) {
        return -1;
    }

    if ((pre & 0x80)) {
        if (vf_exp && mu_le_ber_integer_s64_write_byval(buf, vf_exp, vw_exp) < 0) {
            return -1;
        }
        if (vf_man && mu_le_ber_integer_u64_write_byval(buf, vf_man, vw_man) < 0) {
            return -1;
        }
    }

#if DEBUG_ENCODING
    _mu_vf128_f32_debug(v, pre, d.sexp, d.frac, vw_exp, vw_man);
#endif

    return 0;
}

int mu_vf128_f32_write_byval(mu_buf *buf, const float value)
{
    s8 pre;
    const float v = value;
    mu_vf128_f32_resultdata d = mu_vf128_f32_resultdata_get(v);
    int vf_exp = 0;
    int vf_man = 0;
    u32 vw_man = 0;
    s32 vw_exp = 0;

    // Inf/NaN
    if (d.sexp == f32_exp_bias + 1) {
        vf_exp = 3;
        vf_man = (d.frac != 0) << 3;
        pre = (d.sign << 6) | (vf_exp << 4) | vf_man;
    }
    // Zero
    else if (d.sexp == -(s32)f32_exp_bias && d.frac == 0) {
        pre = (d.sign << 6);
    }
    // Inline (normal)
    else if (d.sexp <= 1 && d.sexp >= 0 &&
             (d.frac & u32_msn) == d.frac) {
        pre = (d.sign << 6) | (u8)((d.sexp+1) << 4) | (u8)(d.frac >> 28);
    }
    // Inline (subnormal)
    else if (d.sexp <= -1 && d.sexp >= -4 &&
             ((d.frac >> -d.sexp) & u32_msn) == (d.frac >> -d.sexp)) {
        pre = (d.sign << 6) | (u8)((0x10 | (d.frac >> 28)) >> -d.sexp);
    }
    // Out-of-line
    else {
        size_t tz = ctz(d.frac), lz = clz(d.frac);
        /*
         * 1. renormalize subnormal fraction (leading one preserved)
         * 2. omit fraction for powers of two (fraction is zero).
         * 3. omit exponent for some normal values (exponent unary prefix)
         * 4. otherwise encode both exponent and fraction
         */
        if (d.sexp == -(s32)f32_exp_bias) {
            vw_man = d.frac >> tz;
            vw_exp = d.sexp - (u32)lz - 1;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            vf_man = (u8)mu_le_ber_integer_u64_length_byval(vw_man);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        else if (d.frac == 0) {
            vw_exp = d.sexp;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4);
        }
        else if (d.sexp < 0 && d.sexp >= -8) {
            /*
             * - compressing -0.99999.. - 0.99999.. excluding +/-0.
             * - fixed point fraction with implied exponent of e0 relative
             *   to the most significant bit in the encoded mantissa.
             * - bit 7 of the left-most byte of the fraction is 0.5.
             * - prepend exponent as a unary code in the mantissa lsb.
             * - compare/choose compressed or normal representation.
             */
            size_t sh = -d.sexp - 1;
            u32 vw_man_a = (d.frac >> tz) | (u32_msb >> (tz - 1));
            u32 vw_man_b = ((d.frac >> tz) << sh) | ((u32_msb >> (tz - 1)) << sh);
            int vf_exp_a = (u8)mu_le_ber_integer_s64_length_byval(d.sexp);
            int vf_man_a = (u8)mu_le_ber_integer_u64_length_byval(vw_man_a);
            int vf_man_b = (u8)mu_le_ber_integer_u64_length_byval(vw_man_b);
            if (vf_man_a + vf_exp_a < vf_man_b) {
                vw_man = vw_man_a;
                vw_exp = d.sexp;
                vf_exp = vf_exp_a;
                vf_man = vf_man_a;
            } else {
                vw_man = vw_man_b;
                vf_man = vf_man_b;
            }
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        else {
            vw_man = (d.frac >> tz) | (u32_msb >> (tz - 1));
            vw_exp = d.sexp;
            vf_exp = (u8)mu_le_ber_integer_s64_length_byval(vw_exp);
            vf_man = (u8)mu_le_ber_integer_u64_length_byval(vw_man);
            pre = 0x80 | (d.sign << 6) | (vf_exp << 4) | vf_man;
        }
        /* vf_exp and vf_man contain length of exponent and fraction in bytes */
    }

    if (mu_buf_write_i8(buf, pre) != 1) {
        return -1;
    }

    if ((pre & 0x80)) {
        if (vf_exp && mu_le_ber_integer_s64_write_byval(buf, vf_exp, vw_exp) < 0) {
            return -1;
        }
        if (vf_man && mu_le_ber_integer_u64_write_byval(buf, vf_man, vw_man) < 0) {
            return -1;
        }
    }

#if DEBUG_ENCODING
    _mu_vf128_f32_debug(v, pre, d.sexp, d.frac, vw_exp, vw_man);
#endif

    return 0;
}

int mu_vf128_f64_read_vec(mu_buf *buf, double *value, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (mu_vf128_f64_read(buf, value + i) < 0) return -1;
    }
    return 0;
}

int mu_vf128_f64_write_vec(mu_buf *buf, const double *value, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (mu_vf128_f64_write(buf, value + i) < 0) return -1;
    }
    return 0;
}

int mu_vf128_f32_read_vec(mu_buf *buf, float *value, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (mu_vf128_f32_read(buf, value + i) < 0) return -1;
    }
    return 0;
}

int mu_vf128_f32_write_vec(mu_buf *buf, const float *value, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (mu_vf128_f32_write(buf, value + i) < 0) return -1;
    }
    return 0;
}

/*
 * IEEE 754
 */

int mu_ieee754_f64_read(mu_buf *buf, double *value)
{
    if (mu_buf_read_i64(buf, (int64_t*)value) != sizeof(f64)) {
        return -1;
    }
    return 0;
}

int mu_ieee754_f64_write(mu_buf *buf, const double *value)
{
    if (mu_buf_write_i64(buf, (int64_t)f64_to_bits(*value)) != sizeof(f64)) {
        return -1;
    }
    return 0;
}

struct f64_result mu_ieee754_f64_read_byval(mu_buf *buf)
{
    f64 v;
    if (mu_buf_read_i64(buf, (int64_t*)&v) != sizeof(f64)) {
        return f64_result { 0, -1 };
    }
    return f64_result { v, 0 };
}

int mu_ieee754_f64_write_byval(mu_buf *buf, const double value)
{
    if (mu_buf_write_i64(buf, (int64_t)f64_to_bits(value)) != sizeof(f64)) {
        return -1;
    }
    return 0;
}

int mu_ieee754_f64_read_vec(mu_buf *buf, double *value, size_t count)
{
    if (mu_buf_read_vec_i64(buf, (int64_t*)value, count) != sizeof(f32) * count) {
        return -1;
    }
    return 0;
}

int mu_ieee754_f64_write_vec(mu_buf *buf, const double *value, size_t count)
{
    if (mu_buf_write_vec_i64(buf, (const int64_t*)value, count) != sizeof(f32) * count) {
        return -1;
    }
    return 0;
}

int mu_ieee754_f32_read(mu_buf *buf, float *value)
{
    if (mu_buf_read_i32(buf, (int32_t*)value) != sizeof(f32)) {
        return -1;
    }
    return 0;
}

int mu_ieee754_f32_write(mu_buf *buf, const float *value)
{
    if (mu_buf_write_i32(buf, (int32_t)f32_to_bits(*value)) != sizeof(f32)) {
        return -1;
    }
    return 0;
}

struct f32_result mu_ieee754_f32_read_byval(mu_buf *buf)
{
    f32 v;
    if (mu_buf_read_i32(buf, (int32_t*)&v) != sizeof(f32)) {
        return f32_result { 0, -1 };
    }
    return f32_result { v, 0 };
}

int mu_ieee754_f32_write_byval(mu_buf *buf, const float value)
{
    if (mu_buf_write_i32(buf, (int32_t)f32_to_bits(value)) != sizeof(f32)) {
        return -1;
    }
    return 0;
}

int mu_ieee754_f32_read_vec(mu_buf *buf, float *value, size_t count)
{
    if (mu_buf_read_vec_i32(buf, (int32_t*)value, count) != sizeof(f32) * count) {
        return -1;
    }
    return 0;
}

int mu_ieee754_f32_write_vec(mu_buf *buf, const float *value, size_t count)
{
    if (mu_buf_write_vec_i32(buf, (const int32_t*)value, count) != sizeof(f32) * count) {
        return -1;
    }
    return 0;
}

/*
 * LEB128
 */

int mu_leb_u64_read(mu_buf *buf, u64 *value)
{
    int8_t b;
    size_t w = 0;
    u64 v = 0;

    do {
        if (mu_buf_read_i8(buf, &b) != 1) {
            goto err;
        }
        v |= ((u64)b & 0x7f) << w;
        w += 7;
    } while ((b & 0x80) && w < 56);

    if (w > 56) {
        goto err;
    }

    *value = v;
    return 0;
err:
    *value = 0;
    return -1;
}

u64_result mu_leb_u64_read_byval(mu_buf *buf)
{
    int8_t b;
    size_t w = 0;
    u64 v = 0;

    do {
        if (mu_buf_read_i8(buf, &b) != 1) {
            return u64_result { 0, -1 };
        }
        v |= ((u64)b & 0x7f) << w;
        w += 7;
    } while ((b & 0x80) && w < 56);

    if (w > 56) {
        return u64_result { 0, -1 };
    }

    return u64_result { v, 0 };
}

int mu_leb_u64_write(mu_buf *buf, const u64 *value)
{
    size_t len, i;
    u64 x = *value;
    u64 v = 0;

    if (x >= (1ull << 56)) {
        return -1;
    }

    len = (x == 0) ? 1 : 8 - ((clz(x) - 1) / 7) + 1;
    if (buf->write_check(buf, len)) {
        return -1;
    }
    for (i = 0; i < len - 1; i++) {
        if (mu_buf_write_unchecked_i8(buf, ((x & 0x7f) | 0x80)) != 1) {
            return -1;
        }
        x >>= 7;
    }
    if (mu_buf_write_unchecked_i8(buf, (x & 0x7f)) != 1) {
        return -1;
    }

    return 0;
}

int mu_leb_u64_write_byval(mu_buf *buf, const u64 value)
{
    size_t len, i;
    u64 x = value;
    u64 v = 0;

    if (x >= (1ull << 56)) {
        return -1;
    }

    len = (x == 0) ? 1 : 8 - ((clz(x) - 1) / 7) + 1;
    if (buf->write_check(buf, len)) {
        return -1;
    }
    for (i = 0; i < len - 1; i++) {
        if (mu_buf_write_unchecked_i8(buf, ((x & 0x7f) | 0x80)) != 1) {
            return -1;
        }
        x >>= 7;
    }
    if (mu_buf_write_unchecked_i8(buf, (x & 0x7f)) != 1) {
        return -1;
    }

    return 0;
}

/*
 * VLU
 */

int mu_vlu_u64_read(mu_buf *buf, u64 *value)
{
    size_t len;
    int8_t b;
    u64 v = 0;

    if (mu_buf_read_i8(buf, &b) != 1) {
        goto err;
    }

    len = ctz(~(u64)b) + 1;
    if (len > 8) {
        goto err;
    }
    if (len > 1 && mu_le_ber_integer_u64_read(buf, len - 1, &v) < 0) {
        goto err;
    }
    v = ((u64)(u8)b >> len) | v << (8 - len);

    *value = v;
    return 0;
err:
    *value = 0;
    return -1;
}

u64_result mu_vlu_u64_read_byval(mu_buf *buf)
{
    size_t len;
    int8_t b;
    u64_result r;
    u64 v = 0;

    if (mu_buf_read_i8(buf, &b) != 1) {
        return u64_result { 0, -1 };
    }

    len = ctz(~(u64)b) + 1;
    if (len > 8) {
        return u64_result { 0, -1 };
    }
    if (len > 1) {
        r = mu_le_ber_integer_u64_read_byval(buf, len - 1);
        if (r.error < 0) {
            return u64_result { 0, r.error };
        }
        v = r.value;
    }
    return u64_result { ((u64)(u8)b >> len) | v << (8 - len), 0 };
}

int mu_vlu_u64_write(mu_buf *buf, const u64 *value)
{
    size_t len;
    const u64 x = *value;
    u64 v = 0;

    if (x >= (1ull << 56)) {
        return -1;
    }

    len = (x == 0) ? 1 : 8 - ((clz(x) - 1) / 7) + 1;
    v = (x << len) | ((1ull << (len-1))-1);

    if (mu_le_ber_integer_u64_write(buf, len, &v) < 0) {
        return -1;
    }

    return 0;
}

int mu_vlu_u64_write_byval(mu_buf *buf, const u64 value)
{
    size_t len;
    const u64 x = value;
    u64 v = 0;

    if (x >= (1ull << 56)) {
        return -1;
    }

    len = (x == 0) ? 1 : 8 - ((clz(x) - 1) / 7) + 1;
    v = (x << len) | ((1ull << (len-1))-1);

    if (mu_le_ber_integer_u64_write_byval(buf, len, v) < 0) {
        return -1;
    }

    return 0;
}
