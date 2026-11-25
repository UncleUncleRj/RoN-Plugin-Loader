#include <windows.h>
#include <string.h>
#include "check.h"
#include "loader/hooks.h"

int hk_add1(int x);
int hk_caller(int x);
extern int (*hk_slot)(int);

typedef int (*AddFn)(int);
static const uint8_t k_stock[11] = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x83, 0xC0, 0x01, 0x5D, 0xC3};

static AddFn o1, o2;
static int d1(int x) { return o1(x) * 10; }
static int d2(int x) { return o2(x) + 100; }

static int n_owner, n_other, n_kind;
static uintptr_t n_at;
static void notify(int owner, int other, uintptr_t at, int kind)
{
    n_owner = owner;
    n_other = other;
    n_at = at;
    n_kind = kind;
}

static void start(void)
{
    hm_reset();
    hm_set_notify(notify);
    n_owner = n_other = n_kind = -1;
    n_at = 0;
}

TEST(targets_bytes_and_export)
{
    CHECK(memcmp((void *)hk_add1, k_stock, sizeof k_stock) == 0);
    CHECK_EQ(((uint8_t *)hk_caller)[4], 0xE8);
    CHECK((void *)GetProcAddress(GetModuleHandleW(NULL), "hk_add1") == (void *)hk_add1);
}

TEST(inline_single)
{
    start();
    CHECK_EQ(hm_inline(1, (uintptr_t)hk_add1, k_stock, NULL, 11, 6, (void *)d1, (void **)&o1), RON_OK);
    CHECK_EQ(hk_add1(1), 20);
    CHECK_EQ(o1(1), 2);
    hm_reset();
    CHECK_EQ(hk_add1(1), 2);
}

TEST(inline_chain_lowest_first)
{
    start();
    CHECK_EQ(hm_inline(1, (uintptr_t)hk_add1, k_stock, NULL, 11, 6, (void *)d1, (void **)&o1), RON_OK);
    CHECK_EQ(hm_inline(2, (uintptr_t)hk_add1, k_stock, NULL, 11, 6, (void *)d2, (void **)&o2), RON_OK);
    CHECK_EQ(hk_add1(1), 120);
    CHECK_EQ(n_owner, 2);
    CHECK_EQ(n_other, 1);
    CHECK_EQ(n_at, (uintptr_t)hk_add1);
    CHECK_EQ(n_kind, HM_SHARED);
    hm_reset();
}

TEST(inline_sig_mismatch)
{
    start();
    uint8_t bad[11];
    memcpy(bad, k_stock, sizeof bad);
    bad[3] = 0x90;
    CHECK_EQ(hm_inline(1, (uintptr_t)hk_add1, bad, NULL, 11, 6, (void *)d1, (void **)&o1), RON_E_SIG);
    CHECK(memcmp((void *)hk_add1, k_stock, sizeof k_stock) == 0);
}

TEST(inline_mask)
{
    start();
    uint8_t sig[11], mask[11];
    memcpy(sig, k_stock, sizeof sig);
    memset(mask, 0xFF, sizeof mask);
    sig[3] = 0x90;
    mask[3] = 0x00;
    CHECK_EQ(hm_inline(1, (uintptr_t)hk_add1, sig, mask, 11, 6, (void *)d1, (void **)&o1), RON_OK);
    CHECK_EQ(hk_add1(1), 20);
    hm_reset();
}

TEST(inline_overlap_conflict)
{
    start();
    CHECK_EQ(hm_inline(1, (uintptr_t)hk_add1, k_stock, NULL, 11, 6, (void *)d1, (void **)&o1), RON_OK);
    CHECK_EQ(hm_inline(2, (uintptr_t)hk_add1 + 1, k_stock + 1, NULL, 10, 5, (void *)d2, (void **)&o2),
             RON_E_CONFLICT);
    CHECK_EQ(n_kind, HM_CONFLICT);
    CHECK_EQ(n_owner, 2);
    CHECK_EQ(n_other, 1);
    CHECK_EQ(hk_add1(1), 20);
    hm_reset();
}

TEST(rollback_newest)
{
    start();
    CHECK_EQ(hm_inline(1, (uintptr_t)hk_add1, k_stock, NULL, 11, 6, (void *)d1, (void **)&o1), RON_OK);
    CHECK_EQ(hm_inline(2, (uintptr_t)hk_add1, k_stock, NULL, 11, 6, (void *)d2, (void **)&o2), RON_OK);
    hm_rollback_owner(2);
    CHECK_EQ(hk_add1(1), 20);
    hm_rollback_owner(1);
    CHECK(memcmp((void *)hk_add1, k_stock, sizeof k_stock) == 0);
    CHECK_EQ(hk_add1(1), 2);
}

static AddFn c1, c2;
static int cd1(int x) { return c1(x) * 10; }
static int cd2(int x) { return c2(x) + 100; }

TEST(call_chain)
{
    start();
    uintptr_t site = (uintptr_t)hk_caller + 4;
    CHECK_EQ(hm_call(1, site, (void *)cd1, (void **)&c1), RON_OK);
    CHECK_EQ(hm_call(2, site, (void *)cd2, (void **)&c2), RON_OK);
    CHECK_EQ(hk_caller(1), 120);
    CHECK((void *)c1 == (void *)hk_add1);
    hm_reset();
    CHECK_EQ(hk_caller(1), 2);
    CHECK_EQ(hm_call(1, (uintptr_t)hk_caller, (void *)cd1, (void **)&c1), RON_E_SIG);  /* not an E8 */
}

static AddFn p1, p2;
static int pd1(int x) { return p1(x) * 10; }
static int pd2(int x) { return p2(x) + 100; }

TEST(ptr_chain_and_expected)
{
    start();
    CHECK_EQ(hm_ptr(1, (uintptr_t)&hk_slot, (uintptr_t)hk_add1, (void *)pd1, (void **)&p1), RON_OK);
    CHECK_EQ(hk_slot(1), 20);
    CHECK_EQ(hm_ptr(2, (uintptr_t)&hk_slot, (uintptr_t)hk_add1, (void *)pd2, (void **)&p2), RON_OK);
    CHECK_EQ(hk_slot(1), 120);
    hm_reset();
    CHECK((void *)hk_slot == (void *)hk_add1);
    CHECK_EQ(hm_ptr(1, (uintptr_t)&hk_slot, 0x1234, (void *)pd1, (void **)&p1), RON_E_SIG);
}

TEST(patch_conflicts_with_hook)
{
    start();
    const uint8_t expect[2] = {0x8B, 0x45}, bytes[2] = {0x90, 0x90};
    CHECK_EQ(hm_inline(1, (uintptr_t)hk_add1, k_stock, NULL, 11, 6, (void *)d1, (void **)&o1), RON_OK);
    CHECK_EQ(hm_patch(2, (uintptr_t)hk_add1 + 3, expect, bytes, 2), RON_E_CONFLICT);
    CHECK_EQ(n_kind, HM_CONFLICT);
    hm_reset();
}

TEST(patch_expect_mismatch)
{
    start();
    const uint8_t expect[3] = {0x83, 0xC0, 0x07}, bytes[3] = {0x83, 0xC0, 0x02};
    CHECK_EQ(hm_patch(1, (uintptr_t)hk_add1 + 6, expect, bytes, 3), RON_E_SIG);
    CHECK_EQ(hk_add1(1), 2);
}

TEST(patch_applies_and_rolls_back)
{
    start();
    const uint8_t expect[3] = {0x83, 0xC0, 0x01}, bytes[3] = {0x83, 0xC0, 0x02};
    CHECK_EQ(hm_patch(1, (uintptr_t)hk_add1 + 6, expect, bytes, 3), RON_OK);
    CHECK_EQ(hk_add1(1), 3);
    hm_rollback_owner(1);
    CHECK_EQ(hk_add1(1), 2);
}

TEST(locked_refuses)
{
    start();
    const uint8_t expect[3] = {0x83, 0xC0, 0x01}, bytes[3] = {0x83, 0xC0, 0x02};
    hm_lock();
    CHECK_EQ(hm_inline(1, (uintptr_t)hk_add1, k_stock, NULL, 11, 6, (void *)d1, (void **)&o1), RON_E_STATE);
    CHECK_EQ(hm_call(1, (uintptr_t)hk_caller + 4, (void *)cd1, (void **)&c1), RON_E_STATE);
    CHECK_EQ(hm_ptr(1, (uintptr_t)&hk_slot, 0, (void *)pd1, (void **)&p1), RON_E_STATE);
    CHECK_EQ(hm_patch(1, (uintptr_t)hk_add1 + 6, expect, bytes, 3), RON_E_STATE);
    CHECK_EQ(hk_add1(1), 2);
    hm_reset();
}
