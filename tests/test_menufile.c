#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "loader/menufile.h"

/* Shaped like the game's topmenu.xml (synthetic; the game's own file is never copied here). */
#define HEAD "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<ROOT>\r\n<TOPMENU>\r\n" \
             "    <MAINMENU name=\"Main Menu\" title=\"Main Menu\">\r\n" \
             "      <BUTTON name=\"options_menu\" title=\"Tools &amp; Extras\" width=\"454\" height=\"35\"/>\r\n" \
             "    </MAINMENU>\r\n" \
             "    <OPTIONS name=\"Tools &amp; Extras\" title=\"Tools &amp; Extras\" left=\"30\" top=\"40\" width=\"548\" height=\"768\">\r\n" \
             "      <POSITIONING x=\"41\" y=\"114\">\r\n" \
             "        <POSITIONING inc_y=\"36\">\r\n" \
             "          <BUTTON name=\"editor\" title=\"Scenario Editor\" width=\"454\" height=\"35\"/>\r\n"
#define WORKSHOP "\t\t  <BUTTON name=\"workshop\" title=\"Steam Workshop\" width=\"454\" height=\"35\"/>\r\n"
#define COMMENTED "          <!--<BUTTON name=\"workshop\" title=\"Old\" width=\"454\" height=\"35\"/>-->\r\n"
#define TAIL "          <BUTTON name=\"skilltest\" title=\"Skill Test\" width=\"454\" height=\"35\"/>\r\n" \
             "          <!--<BUTTON name=\"map_balance\" title=\"Map Balance Tool\" width=\"454\" height=\"35\"/>-->\r\n" \
             "          <BUTTON name=\"back\" title=\"Back to Main Menu\" width=\"454\" height=\"35\"/>\r\n" \
             "        </POSITIONING>\r\n      </POSITIONING>\r\n    </OPTIONS>\r\n</TOPMENU>\r\n"
#define TRIAL "<TOPMENU_TRIAL>\r\n    <OPTIONS name=\"Tools &amp; Extras\" title=\"Tools &amp; Extras\">\r\n" \
              "          <BUTTON name=\"editor\" title=\"Scenario Editor\" width=\"454\" height=\"35\"/>\r\n" \
              "          <BUTTON name=\"back\" title=\"Back to Main Menu\" width=\"454\" height=\"35\"/>\r\n" \
              "    </OPTIONS>\r\n</TOPMENU_TRIAL>\r\n</ROOT>\r\n"

#define PLUGINS_BUTTON "<BUTTON name=\"plugins\" title=\"Plugins\" width=\"454\" height=\"35\"/>"

static char g_out[65536];

static size_t run(const char *in, MfWhere *where)
{
    *where = (MfWhere)99;
    memset(g_out, 0, sizeof g_out);
    return menufile_add_plugins(in, strlen(in), g_out, sizeof g_out, where);
}

static int count(const char *hay, const char *needle)
{
    int n = 0;
    for (const char *p = strstr(hay, needle); p; p = strstr(p + 1, needle))
        n++;
    return n;
}

TEST(mf_after_workshop)
{
    MfWhere w;
    const char *in = HEAD WORKSHOP TAIL TRIAL;
    size_t n = run(in, &w);
    CHECK(n > strlen(in));
    CHECK_EQ(n, strlen(g_out));
    CHECK_EQ(w, MF_AFTER_WORKSHOP);
    CHECK(strstr(g_out, "\"Steam Workshop\" width=\"454\" height=\"35\"/>\r\n\t\t  " PLUGINS_BUTTON
                        "\r\n          <BUTTON name=\"skilltest\"") != NULL);
    CHECK_EQ(count(g_out, "name=\"plugins\""), 1);
    CHECK_STR(strstr(g_out, "<TOPMENU_TRIAL>"), strstr(in, "<TOPMENU_TRIAL>"));
}

TEST(mf_panel_before_topmenu_end)
{
    MfWhere w;
    run(HEAD WORKSHOP TAIL TRIAL, &w);
    const char *panel = strstr(g_out, "<PLUGINS name=\"Plugins\"");
    CHECK(panel != NULL);
    CHECK_EQ(count(g_out, "<PLUGINS name=\"Plugins\""), 1);
    CHECK(panel > strstr(g_out, "</OPTIONS>"));
    CHECK(panel < strstr(g_out, "</TOPMENU>"));
    CHECK(strstr(g_out, "    <PLUGINS name=\"Plugins\" title=\"Plugins\" left=\"30\" top=\"40\" width=\"548\" height=\"768\">\r\n") != NULL);
    CHECK(strstr(g_out, "          <BUTTON name=\"plugins_apply\" title=\"Apply and Restart\" width=\"454\" height=\"35\"/>\r\n") != NULL);
    CHECK(strstr(g_out, "    </PLUGINS>\r\n</TOPMENU>\r\n") != NULL);
}

TEST(mf_above_back_without_workshop)
{
    MfWhere w;
    run(HEAD TAIL TRIAL, &w);
    CHECK_EQ(w, MF_ABOVE_BACK);
    CHECK(strstr(g_out, "-->\r\n          " PLUGINS_BUTTON "\r\n          <BUTTON name=\"back\"") != NULL);
    CHECK_EQ(count(g_out, "name=\"plugins\""), 1);
}

TEST(mf_skips_comments)
{
    MfWhere w;
    run(HEAD COMMENTED WORKSHOP TAIL TRIAL, &w);
    CHECK_EQ(w, MF_AFTER_WORKSHOP);
    CHECK(strstr(g_out, "\"Steam Workshop\" width=\"454\" height=\"35\"/>\r\n\t\t  " PLUGINS_BUTTON) != NULL);
    CHECK(strstr(g_out, COMMENTED WORKSHOP) != NULL);  /* the commented line is untouched */
    run(HEAD COMMENTED TAIL TRIAL, &w);
    CHECK_EQ(w, MF_ABOVE_BACK);
}

TEST(mf_single_quotes_and_case)
{
    MfWhere w;
    const char *in = "<root>\r\n<topmenu>\r\n  <options name='Tools'>\r\n"
                     "    <button NAME='workshop' title='Steam Workshop'/>\r\n"
                     "    <button name='back' title='Back'/>\r\n  </options>\r\n</topmenu>\r\n</root>\r\n";
    run(in, &w);
    CHECK_EQ(w, MF_AFTER_WORKSHOP);
    CHECK(strstr(g_out, "title='Steam Workshop'/>\r\n    " PLUGINS_BUTTON "\r\n    <button name='back'") != NULL);
    CHECK(strstr(g_out, "    </PLUGINS>\r\n</topmenu>") != NULL);
}

TEST(mf_keeps_lf)
{
    MfWhere w;
    const char *in = "<ROOT>\n<TOPMENU>\n  <OPTIONS name=\"T\">\n"
                     "    <BUTTON name=\"workshop\"/>\n    <BUTTON name=\"back\"/>\n  </OPTIONS>\n</TOPMENU>\n</ROOT>\n";
    size_t n = run(in, &w);
    CHECK(n > 0);
    CHECK_EQ(w, MF_AFTER_WORKSHOP);
    CHECK(strchr(g_out, '\r') == NULL);
    CHECK(strstr(g_out, "<BUTTON name=\"workshop\"/>\n    " PLUGINS_BUTTON "\n") != NULL);
}

TEST(mf_idempotent)
{
    MfWhere w;
    run(HEAD WORKSHOP TAIL TRIAL, &w);
    char *once = _strdup(g_out);
    size_t n = run(once, &w);
    CHECK_EQ(w, MF_ALREADY);
    CHECK_EQ(n, strlen(once));
    CHECK_STR(g_out, once);
    free(once);
}

TEST(mf_no_options_fails)
{
    MfWhere w;
    const char *in = "<ROOT>\r\n<TOPMENU>\r\n    <MAINMENU name=\"Main Menu\"/>\r\n</TOPMENU>\r\n" TRIAL;
    CHECK_EQ(run(in, &w), 0);
    CHECK_EQ(w, MF_FAILED);
}

TEST(mf_small_cap_fails)
{
    MfWhere w = (MfWhere)99;
    const char *in = HEAD WORKSHOP TAIL TRIAL;
    size_t len = strlen(in);
    CHECK_EQ(menufile_add_plugins(in, len, g_out, len + 10, &w), 0);
    CHECK_EQ(w, MF_FAILED);
}

/* The list and the notice sit inside the panel's frame, not edge to edge with it: the stock
   buttons fill x 41..495 (width 454) from y 114, and the frame's border is drawn right there. */
TEST(mf_list_inset_from_frame)
{
    MfWhere w;
    run(HEAD WORKSHOP TAIL TRIAL, &w);
    static const char *const names[] = {"plugin_list", "plugin_notice"};
    for (int i = 0; i < 2; i++) {
        char key[64];
        snprintf(key, sizeof key, "name=\"%s\"", names[i]);
        const char *p = strstr(g_out, key);
        CHECK(p != NULL);
        int l = 0, t = 0, wd = 0, h = 0;
        if (p)
            CHECK_EQ(sscanf(p + strlen(key), " left=\"%d\" top=\"%d\" width=\"%d\" height=\"%d\"", &l, &t, &wd, &h), 4);
        CHECK(l >= 41 + 8);
        CHECK(l + wd <= 41 + 454 - 8);
        if (i == 0)
            CHECK(t >= 114 + 4);
    }
}

/* The panel's dark pane holds, top to bottom: the Load Order title and list, the Description title
   and box, and the notice; all inside the pane, none overlapping, and the pane ends above the
   panel's buttons (POSITIONING y 492). */
static int rect_of(const char *name, int r[4])
{
    char key[64];
    snprintf(key, sizeof key, "name=\"%s\"", name);
    const char *p = strstr(g_out, key);
    return p && sscanf(p + strlen(key), " left=\"%d\" top=\"%d\" width=\"%d\" height=\"%d\"", &r[0], &r[1], &r[2], &r[3]) == 4;
}

TEST(mf_panel_sections)
{
    MfWhere w;
    run(HEAD WORKSHOP TAIL TRIAL, &w);
    CHECK(strstr(g_out, "<POSITIONING x=\"41\" y=\"492\">") != NULL);
    int pane[4] = {0};
    CHECK(rect_of("plugin_pane", pane));
    CHECK(pane[1] + pane[3] <= 492);
    static const char *const order[] = {"plugin_list_title", "plugin_list", "plugin_description_title",
                                        "plugin_description", "plugin_notice"};
    int prev_bottom = pane[1];
    for (int i = 0; i < 5; i++) {
        int r[4] = {0};
        CHECK(rect_of(order[i], r));
        CHECK(r[0] >= pane[0] && r[0] + r[2] <= pane[0] + pane[2]);
        CHECK(r[1] >= prev_bottom);
        CHECK(r[3] > 0);
        prev_bottom = r[1] + r[3];
    }
    CHECK(prev_bottom <= pane[1] + pane[3]);
}

/* Compact files (final review): the whole file, or the end of OPTIONS and TOPMENU, on one line. The
   result must keep everything before <ROOT> as it was, put the button inside OPTIONS and the panel
   after </OPTIONS>, before </TOPMENU>. */
static char g_in[8192];

static const char *one_line(const char *in)
{
    size_t k = 0;
    for (const char *c = in; *c && k + 1 < sizeof g_in; c++)
        if (*c != '\r' && *c != '\n')
            g_in[k++] = *c;
    g_in[k] = 0;
    return g_in;
}

static int structure_ok(const char *in)
{
    const char *root = strstr(g_out, "<ROOT>"), *opt = strstr(g_out, "<OPTIONS"), *optc = strstr(g_out, "</OPTIONS>");
    const char *topc = strstr(g_out, "</TOPMENU>"), *btn = strstr(g_out, PLUGINS_BUTTON);
    const char *pan = strstr(g_out, "<PLUGINS"), *panc = strstr(g_out, "</PLUGINS>");
    const char *in_root = strstr(in, "<ROOT>");
    if (!root || !opt || !optc || !topc || !btn || !pan || !panc || !in_root)
        return 0;
    if (root - g_out != in_root - in || memcmp(g_out, in, (size_t)(in_root - in)) != 0)
        return 0;
    return root < opt && opt < btn && btn < optc && optc < pan && pan < panc && panc < topc;
}

TEST(mf_one_line_without_workshop)
{
    MfWhere w;
    const char *in = one_line(HEAD TAIL TRIAL);
    CHECK(run(in, &w) > strlen(in));
    CHECK_EQ(w, MF_ABOVE_BACK);
    CHECK(structure_ok(in));
}

TEST(mf_one_line_with_workshop)
{
    MfWhere w;
    const char *in = one_line(HEAD WORKSHOP TAIL TRIAL);
    CHECK(run(in, &w) > strlen(in));
    CHECK_EQ(w, MF_AFTER_WORKSHOP);
    CHECK(structure_ok(in));
}

TEST(mf_close_tags_on_one_line)
{
    static const char in[] = HEAD WORKSHOP
        "          <BUTTON name=\"back\" title=\"Back to Main Menu\" width=\"454\" height=\"35\"/>\r\n"
        "        </POSITIONING>\r\n      </POSITIONING>\r\n    </OPTIONS></TOPMENU>\r\n" TRIAL;
    MfWhere w;
    CHECK(run(in, &w) > strlen(in));
    CHECK(structure_ok(in));
}

/* Menu styling from mods (RON Revived's menu: a 364-wide window, 270 x 56 buttons 56 apart, its own
   textures). The Plugins button copies the size of the button it follows; the PLUGINS panel copies
   OPTIONS: its window attributes, its non-button children (STYLE, textures), its button size and
   spacing, with the list areas inside the button column and the buttons inside the window. */
#define RR_MENU "<?xml version=\"1.0\"?>\r\n<ROOT>\r\n<TOPMENU>\r\n" \
    "    <OPTIONS name=\"Tools &amp; Extras\" title=\"Tools &amp; Extras\" left=\"30\" top=\"40\" width=\"364\" height=\"768\">\r\n" \
    "      <STYLE CHILD=\"0\" TOP=\"1\"/>\r\n" \
    "      <IFACETEXLOAD id=\"TEX_MOD_9\" file=\".\\art\\mod.tga\"/>\r\n" \
    "      <IFACETEX name=\"back\" texture=\"TEX_MOD_9\" left=\"0\" top=\"0\" width=\"1024\" height=\"768\"/>\r\n" \
    "      <POSITIONING x=\"41\" y=\"114\">\r\n        <POSITIONING inc_y=\"56\">\r\n" \
    "          <BUTTON name=\"editor\" title=\"Editor\" width=\"270\" height=\"56\"/>\r\n" \
    "\t\t  <BUTTON name=\"workshop\" title=\"Workshop\" width=\"270\" height=\"56\"/>\r\n" \
    "          <BUTTON name=\"back\" title=\"Main Menu\" width=\"270\" height=\"56\"/>\r\n" \
    "        </POSITIONING>\r\n      </POSITIONING>\r\n    </OPTIONS>\r\n</TOPMENU>\r\n</ROOT>\r\n"

static const char *panel_text(void)
{
    static char panel[8192];
    const char *a = strstr(g_out, "<PLUGINS"), *b = a ? strstr(a, "</PLUGINS>") : NULL;
    panel[0] = 0;
    if (a && b && (size_t)(b - a) < sizeof panel) {
        memcpy(panel, a, (size_t)(b - a));
        panel[b - a] = 0;
    }
    return panel;
}

TEST(mf_panel_copies_options_style)
{
    MfWhere w;
    CHECK(run(RR_MENU, &w) > strlen(RR_MENU));
    CHECK_EQ(w, MF_AFTER_WORKSHOP);
    CHECK(strstr(g_out, "<BUTTON name=\"plugins\" title=\"Plugins\" width=\"270\" height=\"56\"/>") != NULL);
    CHECK(strstr(g_out, "<PLUGINS name=\"Plugins\" title=\"Plugins\" left=\"30\" top=\"40\" width=\"364\" height=\"768\">") != NULL);
    const char *p = panel_text();
    CHECK(strstr(p, "<STYLE CHILD=\"0\" TOP=\"1\"/>") != NULL);
    CHECK(strstr(p, "<IFACETEXLOAD id=\"TEX_MOD_9\" file=\".\\art\\mod.tga\"/>") != NULL);
    CHECK(strstr(p, "texture=\"TEX_MOD_9\"") != NULL);
    CHECK(strstr(p, "name=\"workshop\"") == NULL && strstr(p, "name=\"editor\"") == NULL);
    CHECK(strstr(p, "<BUTTON name=\"plugins_up\" title=\"Move Up\" width=\"270\" height=\"56\"/>") != NULL);
    CHECK(strstr(p, "<BUTTON name=\"back\" title=\"Back\" width=\"270\" height=\"56\"/>") != NULL);
    CHECK(strstr(p, "inc_y=\"56\"") != NULL);
    int pane[4] = {0}, list[4] = {0}, y = 0;
    CHECK(rect_of("plugin_pane", pane));
    CHECK(rect_of("plugin_list", list));
    CHECK_EQ(pane[0], 41);
    CHECK_EQ(pane[2], 270);
    CHECK(list[0] >= 41 + 8 && list[0] + list[2] <= 41 + 270 - 8);
    const char *pos = strstr(p, "<POSITIONING x=\"41\" y=\"");
    CHECK(pos != NULL);
    if (pos)
        CHECK_EQ(sscanf(pos, "<POSITIONING x=\"41\" y=\"%d\"", &y), 1);
    CHECK(y >= pane[1] + pane[3]);
    CHECK(y + 3 * 56 + 56 <= 768);
}

TEST(mf_panel_stock_geometry)
{
    MfWhere w;
    run(HEAD WORKSHOP TAIL TRIAL, &w);
    static const struct { const char *name; int r[4]; } k[] = {
        {"plugin_pane", {41, 114, 454, 372}},        {"plugin_list_title", {51, 118, 434, 22}},
        {"plugin_list", {51, 140, 434, 156}},        {"plugin_description_title", {51, 300, 434, 22}},
        {"plugin_description", {51, 322, 434, 108}}, {"plugin_notice", {51, 436, 434, 48}},
    };
    for (int i = 0; i < 6; i++) {
        int r[4] = {0};
        CHECK(rect_of(k[i].name, r));
        CHECK(memcmp(r, k[i].r, sizeof r) == 0);
    }
    const char *p = panel_text();
    CHECK(strstr(p, "<POSITIONING x=\"41\" y=\"492\">") != NULL);
    CHECK(strstr(p, "inc_y=\"36\"") != NULL);
    CHECK(strstr(p, "<BUTTON name=\"plugins_apply\" title=\"Apply and Restart\" width=\"454\" height=\"35\"/>") != NULL);
}

/* A narrow menu wraps the notice into more lines (the safe-start notice is about 140 characters):
   the notice area grows to 5 lines and the list gives up the room; every area stays inside the
   pane, in order. */
TEST(mf_narrow_panel_room_for_notice)
{
    MfWhere w;
    run(RR_MENU, &w);
    static const char *const order[] = {"plugin_list_title", "plugin_list", "plugin_description_title",
                                        "plugin_description", "plugin_notice"};
    int pane[4] = {0}, r[4] = {0};
    CHECK(rect_of("plugin_pane", pane));
    int prev = pane[1];
    for (int i = 0; i < 5; i++) {
        CHECK(rect_of(order[i], r));
        CHECK(r[1] >= prev && r[3] > 0);
        prev = r[1] + r[3];
    }
    CHECK(prev <= pane[1] + pane[3]);
    CHECK(r[3] >= 5 * 13);
    CHECK(rect_of("plugin_list", r));
    CHECK(r[3] >= 36 + 3 * 24);
}
