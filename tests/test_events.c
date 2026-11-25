#include <string.h>
#include "check.h"
#include "loader/events.h"

static char g_seq[16];
static int g_nseq;
static void *g_obj, *g_user;

static void cb_a(RonEvent ev, void *obj, void *user)
{
    (void)ev;
    g_seq[g_nseq++] = 'a';
    g_obj = obj;
    g_user = user;
}

static void cb_b(RonEvent ev, void *obj, void *user)
{
    (void)ev;
    (void)obj;
    (void)user;
    g_seq[g_nseq++] = 'b';
}

static void start(void)
{
    ev_reset();
    memset(g_seq, 0, sizeof g_seq);
    g_nseq = 0;
    g_obj = g_user = NULL;
}

TEST(fire_order)
{
    start();
    CHECK_EQ(ev_register(2, RON_EV_SIM_FRAME, cb_b, NULL), RON_OK);
    CHECK_EQ(ev_register(1, RON_EV_SIM_FRAME, cb_a, (void *)0x1234), RON_OK);
    ev_fire(RON_EV_SIM_FRAME, (void *)0xBEEF);
    CHECK_STR(g_seq, "ab");
    CHECK(g_obj == (void *)0xBEEF);
    CHECK(g_user == (void *)0x1234);
    ev_fire(RON_EV_POST_INIT, NULL);  /* nobody registered */
    CHECK_STR(g_seq, "ab");
    ev_reset();
}

TEST(drop_owner)
{
    start();
    ev_register(1, RON_EV_SHUTDOWN, cb_a, NULL);
    ev_register(2, RON_EV_SHUTDOWN, cb_b, NULL);
    ev_drop_owner(1);
    ev_fire(RON_EV_SHUTDOWN, NULL);
    CHECK_STR(g_seq, "b");
    ev_reset();
}

TEST(locked_refuses)
{
    start();
    ev_lock();
    CHECK_EQ(ev_register(1, RON_EV_SIM_FRAME, cb_a, NULL), RON_E_STATE);
    ev_fire(RON_EV_SIM_FRAME, NULL);
    CHECK_STR(g_seq, "");
    ev_reset();
}

TEST(game_hook_table_shape)
{
    int n = 0;
    const GameHook *h = ev_game_hooks(&n);
    CHECK_EQ(n, 6);
    /* sim_frame hooks the only call of Game::do_frame (Game::loop+0x14E), not the function's first
       bytes, so a tool that hooks do_frame's entry (rondbg's Frida agent does) doesn't silence it */
    const struct { RonEvent ev; uint32_t va; uint32_t len; int mask_from, mask_to; int call_site; } want[6] = {
        {RON_EV_POST_INIT, 0x595BE0, 5, -1, -1, 0},    {RON_EV_MATCH_BUILT, 0x5AC190, 6, -1, -1, 0},
        {RON_EV_SIM_FRAME, 0x5916BE, 1, -1, -1, 1},    {RON_EV_RENDER_FRAME, 0x5917E0, 10, 5, 8, 0},
        /* shutdown: the call of Main::run_game_loop in Main::main returns on quit; Main::shutdown is
           never reached there (the process ends in SteamAuthentication first; traced) */
        {RON_EV_MATCH_END, 0x594B80, 6, -1, -1, 0},    {RON_EV_SHUTDOWN, 0x561603, 1, -1, -1, 1},
    };
    for (int i = 0; i < n && i < 6; i++) {
        CHECK_EQ(h[i].ev, want[i].ev);
        CHECK_EQ(h[i].va, want[i].va);
        CHECK_EQ(h[i].sig_len, want[i].len);
        CHECK_EQ(h[i].call_site, want[i].call_site);
        if (h[i].call_site) {
            CHECK_EQ(h[i].sig[0], 0xE8);
            CHECK_EQ(h[i].stolen, 0);
            continue;
        }
        CHECK_EQ(h[i].stolen, want[i].len);
        CHECK(h[i].stolen >= 5 && h[i].stolen <= h[i].sig_len);
        for (uint32_t b = 0; b < h[i].sig_len; b++) {
            int masked = want[i].mask_from >= 0 && (int)b >= want[i].mask_from && (int)b <= want[i].mask_to;
            CHECK_EQ(h[i].mask[b] == 0, masked);
        }
    }
    CHECK(memcmp(h[3].sig, "\x55\x8B\xEC\x83\x3D", 5) == 0 && h[3].sig[9] == 0x00);
    CHECK_EQ(h[5].sig[0], 0xE8);
    CHECK(memcmp(h[1].sig, "\x53\x8B\xDC\x83\xEC\x08", 6) == 0);
}

static int g_built, g_ended;
static void cb_count(RonEvent ev, void *obj, void *user)
{
    (void)obj;
    (void)user;
    if (ev == RON_EV_MATCH_BUILT)
        g_built++;
    if (ev == RON_EV_MATCH_END)
        g_ended++;
}

/* The game calls Game::close before it builds a match (to clear the last one) and twice when a match
   ends: match_end fires once, and only for a match that was built. */
TEST(match_end_once_per_match)
{
    start();
    g_built = g_ended = 0;
    ev_register(1, RON_EV_MATCH_BUILT, cb_count, NULL);
    ev_register(1, RON_EV_MATCH_END, cb_count, NULL);
    ev_game_closed((void *)0x1000);   /* clearing before the first match */
    CHECK_EQ(g_ended, 0);
    ev_match_built((void *)0x2000);
    CHECK_EQ(g_built, 1);
    ev_game_closed((void *)0x1000);
    ev_game_closed((void *)0x1000);
    CHECK_EQ(g_ended, 1);
    ev_match_built((void *)0x2000);   /* a second match */
    ev_game_closed((void *)0x1000);
    CHECK_EQ(g_ended, 2);
    ev_reset();
}
