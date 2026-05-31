#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#if defined(_WIN32) || defined(_WIN64)
#include <string.h>
#else
#include <strings.h>
#define _stricmp strcasecmp
#endif

static int _argc; static char** _argv;

typedef struct ABlock { struct ABlock* next; int cap; int used; } ABlock;
static ABlock* _arena = 0;
typedef struct AllocHdr { struct AllocHdr* next; uint32_t size; uint32_t mark; } AllocHdr;
static AllocHdr* _alloc_head = 0;
static char* _stack_bottom = 0;
static long _gc_arena_bytes = 0;
static long _gc_threshold = 268435456L;
static int _gc_enabled = -1;
static void _gc_collect(void);
static char* _arena_addr_lo = (char*)~(uintptr_t)0;
static char* _arena_addr_hi = 0;
static char* _alloc(int n) __attribute__((noinline));
static char* _alloc(int n) {
    n = (n + 7) & ~7;
    if (_gc_enabled < 0) {
        const char* env = getenv("KR_NOGC");
        _gc_enabled = (env && *env) ? 0 : 1;
    }
    int total = sizeof(AllocHdr) + n;
    if (!_arena || _arena->used + total > _arena->cap) {
        if (_gc_enabled && _gc_arena_bytes > _gc_threshold) {
            _gc_collect();
            if (_arena && _arena->used + total <= _arena->cap) goto have_room;
        }
        int cap = 64*1024*1024;
        if (total > cap) cap = total;
        ABlock* b = (ABlock*)malloc(sizeof(ABlock) + cap);
        if (!b) { fprintf(stderr, "out of memory\n"); exit(1); }
        b->cap = cap; b->used = 0; b->next = _arena; _arena = b;
        _gc_arena_bytes += cap;
    }
    have_room:;
    AllocHdr* h = (AllocHdr*)((char*)(_arena + 1) + _arena->used);
    h->next = _alloc_head;
    h->size = (uint32_t)n;
    h->mark = 0;
    _alloc_head = h;
    _arena->used += total;
    char* user = (char*)(h + 1);
    if (user < _arena_addr_lo) _arena_addr_lo = user;
    if (user + n > _arena_addr_hi) _arena_addr_hi = user + n;
    return user;
}

#include <setjmp.h>
static int _gc_in_range(ABlock* b, char* p) {
    char* base = (char*)(b + 1);
    return p >= base && p < base + b->cap;
}
static AllocHdr** _gc_sorted = 0;
static int _gc_sorted_cap = 0;
static int _gc_sorted_n = 0;
static int _gc_alloc_cmp(const void* a, const void* b) {
    char* pa = (char*)*(AllocHdr* const*)a;
    char* pb = (char*)*(AllocHdr* const*)b;
    return pa < pb ? -1 : (pa > pb ? 1 : 0);
}
static void _gc_mark_word(void* word) {
    char* p = (char*)word;
    if (p < _arena_addr_lo || p >= _arena_addr_hi) return;
    if (_gc_sorted_n == 0) return;
    // Binary search for greatest alloc with start <= p.
    int lo = 0, hi = _gc_sorted_n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        char* mid_start = (char*)_gc_sorted[mid];
        if (mid_start <= p) lo = mid + 1; else hi = mid;
    }
    if (lo == 0) return;
    AllocHdr* a = _gc_sorted[lo - 1];
    char* user = (char*)(a + 1);
    if (p >= user && p < user + a->size) a->mark = 1;
}
static void _gc_collect(void) {
    // Clear all marks + count.
    AllocHdr* a = _alloc_head;
    long n_total = 0, n_live = 0;
    while (a) { a->mark = 0; n_total++; a = a->next; }
    // Build sorted-by-address index for binary-search mark phase.
    if (n_total > _gc_sorted_cap) {
        int newcap = _gc_sorted_cap ? _gc_sorted_cap : 1024;
        while (newcap < n_total) newcap *= 2;
        _gc_sorted = (AllocHdr**)realloc(_gc_sorted, newcap * sizeof(AllocHdr*));
        _gc_sorted_cap = newcap;
    }
    int idx = 0;
    for (a = _alloc_head; a; a = a->next) _gc_sorted[idx++] = a;
    _gc_sorted_n = idx;
    qsort(_gc_sorted, idx, sizeof(AllocHdr*), _gc_alloc_cmp);
    // Mark roots: spill regs to jmp_buf, then scan stack.
    jmp_buf regs;
    setjmp(regs);
    char* lo = (char*)&regs;
    char* hi = _stack_bottom;
    if (lo > hi) { char* t = lo; lo = hi; hi = t; }
    char* p = (char*)(((uintptr_t)lo + 7) & ~(uintptr_t)7);
    while (p < hi) {
        _gc_mark_word(*(void**)p);
        p += 8;
    }
    // Sweep: unlink unmarked from the global list.
    AllocHdr** pp = &_alloc_head;
    while (*pp) {
        if ((*pp)->mark == 0) { *pp = (*pp)->next; }
        else { n_live++; pp = &(*pp)->next; }
    }
    // Free any fully-dead arena blocks (no live allocs in their range).
    ABlock** bpp = &_arena;
    while (*bpp) {
        int has_live = 0;
        AllocHdr* la = _alloc_head;
        while (la) { if (_gc_in_range(*bpp, (char*)la)) { has_live = 1; break; } la = la->next; }
        if (!has_live && *bpp != _arena) {
            ABlock* dead = *bpp;
            *bpp = dead->next;
            _gc_arena_bytes -= dead->cap;
            free(dead);
        } else {
            bpp = &(*bpp)->next;
        }
    }
    // Reset _gc_threshold to roughly 4x current live size, min 64 MB.
    long live_bytes = 0;
    a = _alloc_head; while (a) { live_bytes += a->size; a = a->next; }
    _gc_threshold = live_bytes * 4;
    if (_gc_threshold < 536870912L) _gc_threshold = 536870912L;
    if (getenv("KR_GC_LOG")) fprintf(stderr, "[gc] swept %ld/%ld allocs, %ldMB live, threshold->%ldMB\n", n_total - n_live, n_total, live_bytes/1048576, _gc_threshold/1048576);
}

typedef struct { ABlock* block; int used; } _AllocMark;
static char* _alloc_mark(void) {
    _AllocMark m;
    m.block = _arena;
    m.used = _arena ? _arena->used : 0;
    char* tok = _alloc(sizeof(_AllocMark));
    memcpy(tok, &m, sizeof(_AllocMark));
    return tok;
}
static char* _kr_itoa_cache[1024];
static char* _alloc_reset(const char* tok) {
    _AllocMark m;
    memcpy(&m, tok, sizeof(_AllocMark));
    // Prune the global alloc list: anything in a block we're about
    // to free (or above m.used within m.block) gets dropped first so
    // the GC mark phase doesn't dereference freed memory.
    AllocHdr** pp = &_alloc_head;
    while (*pp) {
        AllocHdr* h = *pp;
        int dead = 0;
        ABlock* b = _arena;
        while (b && b != m.block) {
            if (_gc_in_range(b, (char*)h)) { dead = 1; break; }
            b = b->next;
        }
        if (!dead && m.block && _gc_in_range(m.block, (char*)h)
             && (char*)h >= (char*)(m.block + 1) + m.used) dead = 1;
        if (dead) *pp = h->next;
        else pp = &h->next;
    }
    while (_arena && _arena != m.block) {
        ABlock* deadb = _arena;
        _arena = deadb->next;
        _gc_arena_bytes -= deadb->cap;
        free(deadb);
    }
    if (_arena) _arena->used = m.used;
    // kr_itoa cached strings point into arena; reset invalidates them.
    for (int i = 0; i < 1024; i++) _kr_itoa_cache[i] = 0;
    return "";
}

static long _intSlots[32];
static char* intSlotStore(const char* slot, const char* val) {
    int s = atoi(slot);
    if (s < 0 || s >= 32) return "";
    _intSlots[s] = atol(val);
    return "";
}
static char* kr_str(const char*);
static char* intSlotLoad(const char* slot) {
    int s = atoi(slot);
    char buf[32];
    if (s < 0 || s >= 32) { buf[0]='0'; buf[1]=0; return kr_str(buf); }
    snprintf(buf, sizeof(buf), "%ld", _intSlots[s]);
    return kr_str(buf);
}

static char _K_EMPTY[] = "";
static char _K_ZERO[] = "0";
static char _K_ONE[] = "1";

static char* kr_str(const char* s) {
if (!s[0]) return _K_EMPTY;
    if (s[0] == '0' && !s[1]) return _K_ZERO;
    if (s[0] == '1' && !s[1]) return _K_ONE;
    int n = (int)strlen(s) + 1;
    char* p = _alloc(n);
    memcpy(p, s, n);
    return p;
}

static char* kr_cat(const char* a, const char* b) {
int la = (int)strlen(a), lb = (int)strlen(b);
    char* p = _alloc(la + lb + 1);
    memcpy(p, a, la);
    memcpy(p + la, b, lb + 1);
    return p;
}

static int kr_isnum(const char* s) {
    if (!*s) return 0;
    const char* p = s;
    if (*p == '-') p++;
    if (!*p) return 0;
    while (*p) { if (*p < '0' || *p > '9') return 0; p++; }
    return 1;
}

static char* kr_itoa(int v) {
    if (v == 0) return _K_ZERO;
    if (v == 1) return _K_ONE;
    if (v >= 0 && v < 1024) {
        char* c = _kr_itoa_cache[v];
        if (c) return c;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", v);
        c = kr_str(buf);
        _kr_itoa_cache[v] = c;
        return c;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", v);
    r