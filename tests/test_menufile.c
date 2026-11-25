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
             "    <OPTIONS name=\"Tools &amp; Extras\" title=\"Tools &amp; Extras\" left=\"30\" top=\"40\">\r\n" \
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
