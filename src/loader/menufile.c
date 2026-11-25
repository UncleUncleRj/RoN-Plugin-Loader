#include <ctype.h>
#include <string.h>
#include "loader/menufile.h"

static const char k_button[] = "<BUTTON name=\"plugins\" title=\"Plugins\" width=\"454\" height=\"35\"/>";

/* The PLUGINS panel; "\n" becomes the file's line ending. */
static const char k_panel[] =
    "    <PLUGINS name=\"Plugins\" title=\"Plugins\" left=\"30\" top=\"40\" width=\"548\" height=\"768\">\n"
    "      <STYLE CHILD=\"0\" TOP=\"1\"/>\n"
    "      <IFACETEXLOAD id=\"TEX_TOPMENU_0\" sort=\"0\" file=\".\\art\\iface_wrapper1.tga\" pixformat=\"PF_32\"/>\n"
    "      <IFACETEX name=\"back\" texture=\"TEX_TOPMENU_0\" left=\"0\" top=\"0\" width=\"1024\" height=\"768\"/>\n"
    "      <IFACETEXLOAD id=\"TEX_PLUGINS_1\" file=\".\\art\\wrapper_pieces_02.tga\"/>\n"
    "      <IFACETEX name=\"check_off\" texture=\"TEX_PLUGINS_1\" left=\"1\" top=\"1\" width=\"25\" height=\"22\"/>\n"
    "      <IFACETEX name=\"check_on\" texture=\"TEX_PLUGINS_1\" left=\"1\" top=\"70\" width=\"25\" height=\"22\"/>\n"
    "      <IFACERECT name=\"plugin_pane\" left=\"41\" top=\"114\" width=\"454\" height=\"372\"/>\n"
    "      <IFACERECT name=\"plugin_list_title\" left=\"51\" top=\"118\" width=\"434\" height=\"22\"/>\n"
    "      <IFACERECT name=\"plugin_list\" left=\"51\" top=\"140\" width=\"434\" height=\"156\"/>\n"
    "      <IFACERECT name=\"plugin_description_title\" left=\"51\" top=\"300\" width=\"434\" height=\"22\"/>\n"
    "      <IFACERECT name=\"plugin_description\" left=\"51\" top=\"322\" width=\"434\" height=\"108\"/>\n"
    "      <IFACERECT name=\"plugin_notice\" left=\"51\" top=\"436\" width=\"434\" height=\"48\"/>\n"
    "      <POSITIONING x=\"41\" y=\"492\">\n"
    "        <POSITIONING inc_y=\"36\">\n"
    "          <BUTTON name=\"plugins_up\" title=\"Move Up\" width=\"454\" height=\"35\"/>\n"
    "          <BUTTON name=\"plugins_down\" title=\"Move Down\" width=\"454\" height=\"35\"/>\n"
    "          <BUTTON name=\"plugins_apply\" title=\"Apply and Restart\" width=\"454\" height=\"35\"/>\n"
    "          <BUTTON name=\"back\" title=\"Back\" width=\"454\" height=\"35\"/>\n"
    "        </POSITIONING>\n"
    "      </POSITIONING>\n"
    "    </PLUGINS>\n";

typedef struct Text {
    const char *s;
    size_t n;
} Text;

static int ieq(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    return 1;
}

static int name_char(char c) { return isalnum((unsigned char)c) || c == '_' || c == '-' || c == ':'; }

/* The next tag ('<' followed by a name or '/') at or after i, skipping comments; t->n when none. */
static size_t next_tag(const Text *t, size_t i)
{
    while (i < t->n) {
        const char *p = memchr(t->s + i, '<', t->n - i);
        if (!p)
            return t->n;
        i = (size_t)(p - t->s);
        if (t->n - i >= 4 && memcmp(p, "<!--", 4) == 0) {
            const char *e = NULL;
            for (size_t j = i + 4; j + 3 <= t->n; j++)
                if (memcmp(t->s + j, "-->", 3) == 0) {
                    e = t->s + j;
                    break;
                }
            if (!e)
                return t->n;
            i = (size_t)(e - t->s) + 3;
            continue;
        }
        if (i + 1 < t->n && (name_char(p[1]) || p[1] == '/'))
            return i;
        i++;
    }
    return t->n;
}

/* Whether the tag at i is <name ...> (closing = 0) or </name> (closing = 1). */
static int tag_is(const Text *t, size_t i, const char *name, int closing)
{
    size_t k = i + 1;
    if (closing) {
        if (k >= t->n || t->s[k] != '/')
            return 0;
        k++;
    } else if (k < t->n && t->s[k] == '/') {
        return 0;
    }
    size_t len = strlen(name);
    return k + len <= t->n && ieq(t->s + k, name, len) && (k + len == t->n || !name_char(t->s[k + len]));
}

/* The index just past the '>' of the tag at i (quoted values may hold '>'); t->n when unclosed. */
static size_t tag_end(const Text *t, size_t i)
{
    char q = 0;
    for (size_t k = i + 1; k < t->n; k++) {
        char c = t->s[k];
        if (q) {
            if (c == q)
                q = 0;
        } else if (c == '"' || c == '\'') {
            q = c;
        } else if (c == '>') {
            return k + 1;
        }
    }
    return t->n;
}

/* The first <name> start tag in [from, to). */
static size_t find_start(const Text *t, size_t from, size_t to, const char *name)
{
    for (size_t i = next_tag(t, from); i < to; i = next_tag(t, i + 1))
        if (tag_is(t, i, name, 0))
            return i;
    return to;
}

/* The first </name> in [from, to). */
static size_t find_close(const Text *t, size_t from, size_t to, const char *name)
{
    for (size_t i = next_tag(t, from); i < to; i = next_tag(t, i + 1))
        if (tag_is(t, i, name, 1))
            return i;
    return to;
}

/* Whether the start tag at i has the attribute name="value" (case-insensitive, either quote). */
static int has_name(const Text *t, size_t i, const char *value)
{
    size_t end = tag_end(t, i), vlen = strlen(value);
    for (size_t k = i + 1; k + 4 < end; k++) {
        if (!isspace((unsigned char)t->s[k]) || !ieq(t->s + k + 1, "name", 4))
            continue;
        size_t j = k + 5;
        while (j < end && isspace((unsigned char)t->s[j]))
            j++;
        if (j >= end || t->s[j] != '=')
            continue;
        j++;
        while (j < end && isspace((unsigned char)t->s[j]))
            j++;
        if (j >= end || (t->s[j] != '"' && t->s[j] != '\''))
            continue;
        char q = t->s[j++];
        const char *close = memchr(t->s + j, q, end - j);
        return close && (size_t)(close - (t->s + j)) == vlen && ieq(t->s + j, value, vlen);
    }
    return 0;
}

/* The index just past the element that starts at i: "/>" or its own </BUTTON>. */
static size_t element_end(const Text *t, size_t i, size_t limit)
{
    size_t e = tag_end(t, i);
    if (e >= 2 && t->s[e - 2] == '/')
        return e;
    size_t c = find_close(t, e, limit, "BUTTON");
    return c < limit ? tag_end(t, c) : e;
}

static size_t line_start(const Text *t, size_t i)
{
    while (i > 0 && t->s[i - 1] != '\n')
        i--;
    return i;
}

/* Whether only spaces and tabs stand between the start of i's line and i. */
static int alone_on_line(const Text *t, size_t i)
{
    for (size_t k = line_start(t, i); k < i; k++)
        if (t->s[k] != ' ' && t->s[k] != '\t')
            return 0;
    return 1;
}

/* The result as the game will read it: the text before TOPMENU unchanged, a plugins button inside
   the full game's OPTIONS, and (when one was added) the PLUGINS panel after OPTIONS, closed before
   </TOPMENU>. Anything else and the game keeps its own file. */
static int placed_ok(const char *xml, size_t top, const char *out, size_t n, int panel_added)
{
    if (n < top || memcmp(out, xml, top) != 0)
        return 0;
    Text o = {out, n};
    size_t ot = find_start(&o, 0, n, "TOPMENU");
    if (ot != top)
        return 0;
    size_t otc = find_close(&o, tag_end(&o, ot), n, "TOPMENU");
    size_t oo = find_start(&o, tag_end(&o, ot), otc, "OPTIONS");
    if (otc >= n || oo >= otc)
        return 0;
    size_t ooc = find_close(&o, tag_end(&o, oo), otc, "OPTIONS");
    if (ooc >= otc)
        return 0;
    int button = 0;
    for (size_t i = next_tag(&o, tag_end(&o, oo)); i < ooc; i = next_tag(&o, i + 1))
        if (tag_is(&o, i, "BUTTON", 0) && has_name(&o, i, "plugins"))
            button = 1;
    if (!button)
        return 0;
    if (!panel_added)
        return 1;
    size_t pp = find_start(&o, ooc, otc, "PLUGINS");
    return pp < otc && find_close(&o, tag_end(&o, pp), otc, "PLUGINS") < otc &&
           find_start(&o, tag_end(&o, ot), ooc, "PLUGINS") >= ooc;
}

/* Appends a text, turning "\n" into eol when convert is set; fails (returns 0) past cap. */
static int put(char *out, size_t cap, size_t *at, const char *s, size_t n, const char *eol, int convert)
{
    for (size_t i = 0; i < n; i++) {
        const char *piece = &s[i];
        size_t len = 1;
        if (convert && s[i] == '\n') {
            piece = eol;
            len = strlen(eol);
        }
        if (*at + len >= cap)
            return 0;
        memcpy(out + *at, piece, len);
        *at += len;
    }
    return 1;
}

size_t menufile_add_plugins(const char *xml, size_t len, char *out, size_t cap, MfWhere *where)
{
    *where = MF_FAILED;
    Text t = {xml, len};
    size_t top = find_start(&t, 0, len, "TOPMENU");
    if (top >= len)
        return 0;
    size_t top_close = find_close(&t, tag_end(&t, top), len, "TOPMENU");
    size_t opt = find_start(&t, tag_end(&t, top), top_close, "OPTIONS");
    if (top_close >= len || opt >= top_close)
        return 0;
    size_t opt_close = find_close(&t, tag_end(&t, opt), top_close, "OPTIONS");

    size_t workshop = opt_close, back = opt_close, plugins = opt_close;
    for (size_t i = next_tag(&t, tag_end(&t, opt)); i < opt_close; i = next_tag(&t, i + 1)) {
        if (!tag_is(&t, i, "BUTTON", 0))
            continue;
        if (workshop == opt_close && has_name(&t, i, "workshop"))
            workshop = i;
        else if (back == opt_close && has_name(&t, i, "back"))
            back = i;
        else if (plugins == opt_close && has_name(&t, i, "plugins"))
            plugins = i;
    }
    int has_panel = find_start(&t, tag_end(&t, top), top_close, "PLUGINS") < top_close;

    const char *eol = "\n";
    for (size_t i = 0; i + 1 < len; i++)
        if (xml[i] == '\r' && xml[i + 1] == '\n') {
            eol = "\r\n";
            break;
        }
    size_t button_at = len;  /* where the button text goes; len = nowhere */
    size_t indent_from = 0;
    int after = 0;           /* 1: insert eol + indent + button; 0: insert indent + button + eol */
    int inline_button = 0;   /* the back button shares its line: insert the button text alone, before it */
    MfWhere w;
    if (plugins < opt_close) {
        w = MF_ALREADY;
    } else if (workshop < opt_close) {
        w = MF_AFTER_WORKSHOP;
        button_at = element_end(&t, workshop, opt_close);
        indent_from = line_start(&t, workshop);
        after = 1;
    } else if (back < opt_close) {
        w = MF_ABOVE_BACK;
        inline_button = !alone_on_line(&t, back);
        button_at = inline_button ? back : line_start(&t, back);
        indent_from = button_at;
    } else {
        return 0;
    }
    size_t indent_len = 0;
    while (button_at < len && indent_from + indent_len < len &&
           (xml[indent_from + indent_len] == ' ' || xml[indent_from + indent_len] == '\t'))
        indent_len++;
    size_t panel_at = has_panel ? len : alone_on_line(&t, top_close) ? line_start(&t, top_close) : top_close;
    if (button_at < len && panel_at < len && panel_at < button_at)
        return 0;

    size_t at = 0;
    size_t pos = 0;
    if (button_at < len) {
        if (!put(out, cap, &at, xml, button_at, eol, 0))
            return 0;
        if (inline_button) {
            if (!put(out, cap, &at, k_button, sizeof k_button - 1, eol, 0))
                return 0;
        } else if ((after && !put(out, cap, &at, eol, strlen(eol), eol, 0)) ||
                   !put(out, cap, &at, xml + indent_from, indent_len, eol, 0) ||
                   !put(out, cap, &at, k_button, sizeof k_button - 1, eol, 0) ||
                   (!after && !put(out, cap, &at, eol, strlen(eol), eol, 0))) {
            return 0;
        }
        pos = button_at;
    }
    if (panel_at < len) {
        if (!put(out, cap, &at, xml + pos, panel_at - pos, eol, 0) ||
            !put(out, cap, &at, k_panel, sizeof k_panel - 1, eol, 1))
            return 0;
        pos = panel_at;
    }
    if (!put(out, cap, &at, xml + pos, len - pos, eol, 0))
        return 0;
    out[at] = 0;
    if (!placed_ok(xml, top, out, at, panel_at < len))
        return 0;
    *where = w;
    return at;
}
