#undef NDEBUG
#include <assert.h>
#include "mubuf.h"

void t1()
{
    int8_t v;
    mu_buf *wbuf, *rbuf;
    size_t nread, nwrote;

    wbuf = mu_buffered_writer_new("test/output/t1.dat");
    assert((nwrote = mu_buf_write_i8(wbuf, 127)) == 1);
    mu_buf_destroy(wbuf);

    rbuf = mu_buffered_reader_new("test/output/t1.dat");
    assert((nread = mu_buf_read_i8(rbuf, &v)) == 1);
    assert(v == 127);
    assert((nread = mu_buf_read_i8(rbuf, &v)) == 0);
    mu_buf_destroy(rbuf);
}

void t2()
{
    int8_t v;
    mu_buf *wbuf, *rbuf;
    size_t nread, nwrote;

    wbuf = mu_buffered_writer_new("test/output/t2.dat");
    for (size_t i = 0; i < 1024; i++) {
        assert((nwrote = mu_buf_write_i8(wbuf, 127)) == 1);
    }
    mu_buf_destroy(wbuf);

    rbuf = mu_buffered_reader_new("test/output/t2.dat");
    for (size_t i = 0; i < 1024; i++) {
        assert((nread = mu_buf_read_i8(rbuf, &v)) == 1);
        assert(v == 127);
    }
    assert((nread = mu_buf_read_i8(rbuf, &v)) == 0);
    mu_buf_destroy(rbuf);
}

void t3()
{
    float v = 1.0f/3.0f;
    mu_buf *wbuf, *rbuf;
    int nread, nwrote;

    wbuf = mu_buffered_writer_new("test/output/t3.dat");
    for (size_t i = 0; i < 1024; i++) {
        assert((nwrote = mu_vf128_f32_write(wbuf, &v)) == 0);
    }
    mu_buf_destroy(wbuf);

    rbuf = mu_buffered_reader_new("test/output/t3.dat");
    for (size_t i = 0; i < 1024; i++) {
        assert((nread = mu_vf128_f32_read(rbuf, &v)) == 0);
        assert(v == 1.0f/3.0f);
    }
    assert((nread = mu_vf128_f32_read(rbuf, &v)) < 0);
    mu_buf_destroy(rbuf);
}

void t4()
{
    u64 v = 72057594037927935ull;
    mu_buf *wbuf, *rbuf;
    int nread, nwrote;

    wbuf = mu_buffered_writer_new("test/output/t4.dat");
    for (size_t i = 0; i < 1024; i++) {
        assert((nwrote = mu_vlu_u64_write(wbuf, &v)) == 0);
    }
    mu_buf_destroy(wbuf);

    rbuf = mu_buffered_reader_new("test/output/t4.dat");
    for (size_t i = 0; i < 1024; i++) {
        assert((nread = mu_vlu_u64_read(rbuf, &v)) == 0);
        assert(v == 72057594037927935ull);
    }
    assert((nread = mu_vlu_u64_read(rbuf, &v)) < 0);
    mu_buf_destroy(rbuf);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "-v") == 0) {
        mu_set_debug(1);
    }

    t1();
    t2();
    t3();
    t4();
}
