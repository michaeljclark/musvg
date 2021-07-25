#undef NDEBUG
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cmath>
#include <chrono>

#include "musvg.h"
#include "mubuf.h"

#ifdef _WIN32
#include <Windows.h>
#include <synchapi.h>
#else
#include <time.h>
#endif

using namespace std::chrono;

typedef signed long long llong;
typedef unsigned long long ullong;

static void _millisleep(llong sleep_ms)
{
#ifdef _WIN32
    HANDLE hTimer;
    LARGE_INTEGER liDueTime;
    liDueTime.QuadPart = -10000LL * sleep_ms;
    assert((hTimer = CreateWaitableTimer(NULL, TRUE, NULL)));
    assert(SetWaitableTimer(hTimer, &liDueTime, 0, NULL, NULL, 0));
    assert(WaitForSingleObject(hTimer, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(hTimer);
#else
    struct timespec ts = {
        (time_t)(sleep_ms / 1000),
        (long)((sleep_ms * 1000000ll) % 1000000000ll)
    };
    nanosleep(&ts, nullptr);
#endif
}

struct bench_result { const char *name; llong count; double t; llong size; };
struct bench_info { const char *name; const char *path; musvg_format_t format; };
typedef bench_result (*bench_fn)(llong count, bench_info *info);
struct benchmark { bench_fn fn; bench_info info; };

static bench_result bench_parse(llong count, bench_info *info)
{
    musvg_span span = musvg_read_file(info->path);

    auto st = high_resolution_clock::now();
    for (llong i = 0; i < count; i++) {
        mu_buf *buf = mu_buf_memory_new(span.data, span.size);
        musvg_parser *p = musvg_parser_create();
        assert(!musvg_parse_buffer(p, info->format, buf));
        musvg_parser_destroy(p);
        mu_buf_destroy(buf);
    }
    auto et = high_resolution_clock::now();

    free(span.data);

    double t = (double)duration_cast<nanoseconds>(et - st).count();
    return bench_result { info->name, count, t, (llong)span.size * count };
}

static benchmark benchmarks[] = {
    { &bench_parse, { "parse-svg-xml",      "test/output/tiger.svg" , musvg_format_xml         } },
    { &bench_parse, { "parse-svgv-vf128",   "test/output/tiger.svgv", musvg_format_binary_vf   } },
    { &bench_parse, { "parse-svgb-ieee754", "test/output/tiger.svgb", musvg_format_binary_ieee } }
};

static const char* format_unit(llong count)
{
    static char buf[32];
    if (count % 1000000000 == 0) {
        snprintf(buf, sizeof(buf), "%lluG", count / 1000000000);
    } else if (count % 1000000 == 0) {
        snprintf(buf, sizeof(buf), "%lluM", count / 1000000);
    } else if (count % 1000 == 0) {
        snprintf(buf, sizeof(buf), "%lluK", count / 1000);
    } else {
        snprintf(buf, sizeof(buf), "%llu", count);
    }
    return buf;
}

static const char* format_comma(llong count)
{
    static char buf[32];
    char buf1[32];

    snprintf(buf1, sizeof(buf1), "%llu", count);

    llong l = strlen(buf1), i = 0, j = 0;
    for (; i < l; i++, j++) {
        buf[j] = buf1[i];
        if ((l-i-1) % 3 == 0 && i != l -1) {
            buf[++j] = ',';
        }
    }
    buf[j] = '\0';

    return buf;
}

#define array_size(arr) ((sizeof(arr)/sizeof(arr[0])))

static void print_header(const char *prefix)
{
    printf("%s%-24s %7s %7s %7s %7s %9s\n",
        prefix,
        "benchmark",
        "count",
        "time(s)",
        "op(ms)",
        "ops/s",
        "MiB/s"
    );
}

static void print_rules(const char *prefix)
{
    printf("%s%-24s %7s %7s %7s %7s %9s\n",
        prefix,
        "------------------------",
        "-------",
        "-------",
        "-------",
        "-------",
        "---------"
    );
}

static void print_result(const char *prefix, const char *name,
    llong count, double t, llong size)
{
    printf("%s%-24s %7s %7.3f %7.3f %7s %9.3f\n",
        prefix,
        name,
        format_unit(count),
        t / 1e9,
        t / count / 1e6,
        format_comma((llong)(count * (1e9 / t))),
        size * (1e9 / t) / (1024*1024)
    );
}

static void run_benchmark(size_t n, llong repeat, llong count, llong pause_ms)
{
    double min_t = 0., max_t = 0., sum_t = 0.;
    const char* name = "";
    size_t size;
    if (repeat > 0) {
        char num[32];
        snprintf(num, sizeof(num), "  [%2zu] ", n);
        print_header(num);
        print_rules("       ");
    }
    for (llong i = 0; i < llabs(repeat); i++) {
        benchmark *bench = benchmarks + n;
        bench_result r = bench->fn(count, &bench->info);
        name = r.name;
        size = r.size;
        if (min_t == 0. || r.t < min_t) min_t = r.t;
        if (max_t == 0. || r.t > max_t) max_t = r.t;
        sum_t += r.t;
        if (repeat > 0) {
            char run[32];
            snprintf(run, sizeof(run), "%3llu/%-3llu", i+1, repeat);
            print_result(run, name, count, r.t, size);
        }
    }
    if (repeat > 0) {
        print_rules("       ");
        print_result("worst: ", name, count, max_t, size);
        print_result("  avg: ", name, count, sum_t / repeat, size);
        print_result(" best: ", name, count, min_t, size);
        puts("");
    } else if (llabs(repeat) >= 1) {
        char num[32];
        snprintf(num, sizeof(num), "[%2zu] ", n);
        print_result(num, name, count, min_t, size);
    }
}

#if defined(_WIN32)
# define strtok_r strtok_s
#endif

int main(int argc, char **argv)
{
    llong bench_num = -1, repeat = -10, count = 100, pause_ms = 0;
    if (argc != 5 && argc != 1) {
        fprintf(stderr, "usage: %s [bench_num(,…)] [repeat] [count] [pause_ms]\n", argv[0]);
        fprintf(stderr, "\ne.g.   %s -1 -10 100 100\n", argv[0]);
        exit(0);
    }
    if (argc > 1) {
        bench_num = atoll(argv[1]);
    }
    if (argc > 2) {
        repeat = atoi(argv[2]);
    }
    if (argc > 3) {
        count = atoll(argv[3]);
    }
    if (argc > 4) {
        pause_ms = atoll(argv[4]);
    }
    if (repeat < 0) {
        print_header("     ");
        print_rules("     ");
    }
    if (bench_num == -1) {
        for (llong n = 0; n < array_size(benchmarks); n++) {
            if (pause_ms > 0 && n > 0) _millisleep(pause_ms);
            run_benchmark(n, repeat, count, pause_ms);
        }
    } else {
        char *save, *comp = strtok_r(argv[1], ",", &save);
        while (comp) {
            bench_num = atoll(comp);
            if (bench_num >= 0 && bench_num < array_size(benchmarks)) {
                run_benchmark(bench_num, repeat, count, pause_ms);
            }
            comp = strtok_r(nullptr, ",", &save);
        }
    }
}
