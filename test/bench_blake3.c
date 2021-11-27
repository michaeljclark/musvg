#undef NDEBUG
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "blake3.h"

#define buffer_size 1024

void bench_blake3(size_t count)
{
    uint8_t buf[buffer_size];
    blake3_hasher ctx;
    size_t s = 0;

    blake3_hasher_init(&ctx);
    while (s < count) {
        size_t l = count - s > sizeof(buf) ? sizeof(buf) : count - s;
	blake3_hasher_update(&ctx, buf, l);
        s += l;
    }
    blake3_hasher_finalize(&ctx, buf, BLAKE3_OUT_LEN);
}

void benchmark(const char *name, void (*bench_fn)(size_t), size_t count)
{
    clock_t start, end;
    double gbsec, ns;

    start = clock();
    bench_fn(count);
    end = clock();
    ns = (double)(end - start) / (double)CLOCKS_PER_SEC * 1e9;
    gbsec = ((double)count / (double)(1<<30)) / ((double)ns / 1e9);

    printf("|%-8s|%8zu|%8.2f|%8.2f|\n",
        name, count/(1<<20), ns/(double)count, gbsec);
}

void run_benchmarks()
{
    printf("\n-----\n");
    printf("|%-8s|%8s|%8s|%8s|\n",
        "algo", "sz_mib", "ns_byte", "gb_sec");
    printf("|%-8s|%8s|%8s|%8s|\n",
        ":---", "-----:", "------:", "------:");
    benchmark("blake3", bench_blake3, 1<<29);
    printf("-----\n");
}

int main(int argc, char **argv)
{
    run_benchmarks();
}
