#include <string.h>
#include "check.h"
#include "loader/manifest.h"

static int parse(const char *text, Manifest *m) { return manifest_parse(text, strlen(text), m); }

TEST(manifest_full)
{
    Manifest m;
    CHECK_EQ(parse("[Plugin]\nName=RON Revived\nVersion=2026.11.24\nAuthor=Rj\n"
                   "Description=Camera and interface.\nAffectsSim=0\nFolder=RON Revived\n", &m), 1);
    CHECK_STR(m.name, "RON Revived");
    CHECK_STR(m.version, "2026.11.24");
    CHECK_STR(m.author, "Rj");
    CHECK_STR(m.description, "Camera and interface.");
    CHECK_EQ(m.affects_sim, 0);
    CHECK_STR(m.folder, "RON Revived");
    CHECK_EQ(m.folder_bad, 0);
}

TEST(manifest_defaults)
{
    Manifest m;
    CHECK_EQ(parse("[Plugin]\n", &m), 1);
    CHECK_STR(m.name, "");
    CHECK_STR(m.version, "");
    CHECK_STR(m.author, "");
    CHECK_STR(m.description, "");
    CHECK_STR(m.folder, "");
    CHECK_EQ(m.affects_sim, 1);
    CHECK_EQ(m.folder_bad, 0);
}

TEST(manifest_case_bom_crlf)
{
    Manifest m;
    CHECK_EQ(parse("\xEF\xBB\xBF[plugin]\r\nname = X \r\n", &m), 1);
    CHECK_STR(m.name, "X");
}

TEST(manifest_no_section)
{
    Manifest m;
    CHECK_EQ(parse("Name=X\n", &m), 0);
}

TEST(manifest_comments_other_sections)
{
    Manifest m;
    CHECK_EQ(parse("; comment\n[Other]\nName=Wrong\n[Plugin]\n# Name=Wrong\nVersion=1\n[More]\nAuthor=Wrong\n", &m), 1);
    CHECK_STR(m.name, "");
    CHECK_STR(m.version, "1");
    CHECK_STR(m.author, "");
}

TEST(manifest_truncate_utf8)
{
    char text[400] = "[Plugin]\nDescription=";
    size_t n = strlen(text);
    for (int i = 0; i < 150; i++) {  /* 300 bytes of "é" (C3 A9) */
        text[n++] = (char)0xC3;
        text[n++] = (char)0xA9;
    }
    text[n++] = '\n';
    text[n] = 0;
    Manifest m;
    CHECK_EQ(parse(text, &m), 1);
    size_t len = strlen(m.description);
    CHECK(len <= 255);
    CHECK(len > 250);
    CHECK_EQ(len % 2, 0);  /* whole two-byte characters only */
    CHECK_EQ((unsigned char)m.description[len - 1], 0xA9);
}

TEST(manifest_folder_rejects_paths)
{
    const char *bad[] = {"[Plugin]\nFolder=..\\x\n", "[Plugin]\nFolder=C:\\x\n", "[Plugin]\nFolder=a/b\n",
                         "[Plugin]\nFolder=..\n", "[Plugin]\nFolder=.\n"};
    for (int i = 0; i < 5; i++) {
        Manifest m;
        CHECK_EQ(parse(bad[i], &m), 1);
        CHECK_STR(m.folder, "");
        CHECK_EQ(m.folder_bad, 1);
    }
}
