#undef NDEBUG
#include <assert.h>
#include "muvec.h"

typedef signed long long llong;

#define t1_init(mv) mu_vec_init(&mv,sizeof(llong),0)
#define t1_count(mv) mu_vec_count(&mv)
#define t1_size(mv) mu_vec_size(&mv,sizeof(llong))
#define t1_capacity(mv) mu_vec_capacity(&mv,sizeof(llong))
#define t1_get(mv,idx) ((llong*)mu_vec_get(&mv,sizeof(llong),idx))
#define t1_alloc(mv,count) mu_vec_alloc_relaxed(&mv,sizeof(llong),count)
#define t1_destroy(mv) mu_vec_destroy(&mv)


void t1(size_t count)
{
    mu_vec mv;

    t1_init(mv);
    for (size_t i = 0; i < count; i++) {
        size_t idx = t1_alloc(mv,1);
        llong *p = t1_get(mv,idx);
        *p = i;
    }
    for (size_t i = 0; i < count; i++) {
        llong *p = t1_get(mv, i);
        assert(*p == i);
    }
    t1_destroy(mv);
}

int main(int argc, char **argv)
{
    t1(1024*1024);
}
