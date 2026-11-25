#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loader/menufile.h"

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

/* The value of attribute attr in the start tag at i, as [*v, *v + *n); 0 when it has none. */
static int attr_span(const Text *t, size_t i, const char *attr, size_t *v, size_t *n)
{
    size_t end = tag_end(t, i), alen = strlen(attr);
    for (size_t k = i + 1; k + alen + 1 < end; k++) {
        if (!isspace((unsigned char)t->s[k]) || !ieq(t->s + k + 1, attr, alen) || name_char(t->s[k + 1 + alen]))
            continue;
        size_t j = k + 1 + alen;
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
        if (!close)
            return 0;
        *v = j;
        *n = (size_t)(close - (t->s + j));
        return 1;
    }
    return 0;
}

/* attr as a whole number 0..10000, or def. */
static int attr_int(const Text *t, size_t i, const char *attr, int def)
{
    size_t v, n;
    char buf[16], *e;
    if (!attr_span(t, i, attr, &v, &n) || n == 0 || n >= sizeof buf)
        return def;
    memcpy(buf, t->s + v, n);
    buf[n] = 0;
    long x = strtol(buf, &e, 10);
    return *e || x < 0 || x > 10000 ? def : (int)x;
}

/* The index just past the element that starts at i: its "/>" or its matching close tag (same-name
   elements nest); just past the start tag when it is never closed before limit. */
static size_t elem_end(const Text *t, size_t i, size_t limit)
{
    size_t e = tag_end(t, i);
    if (e >= 2 && t->s[e - 2] == '/')
        return e;
    char name[40];
    size_t k = i + 1, n = 0;
    while (k < t->n && name_char(t->s[k]) && n + 1 < sizeof name)
        name[n++] = t->s[k++];
    name[n] = 0;
    int depth = 1;
    for (size_t j = next_tag(t, e); j < limit; j = next_tag(t, j + 1)) {
        if (tag_is(t, j, name, 1)) {
            if (--depth == 0)
                return tag_end(t, j);
        } else if (tag_is(t, j, name, 0)) {
            size_t te = tag_end(t, j);
            if (!(te >= 2 && t->s[te - 2] == '/'))
                depth++;
        }
    }
    return e;
}

/* How the Tools & Extras panel is laid out, which the Plugins button and panel copy so a mod's menu
   style carries over: the first POSITIONING's x and y and the first inc_y, the size of the button
   the Plugins button joins, the panel's height. The stock values are the defaults. */
typedef struct Style {
    int x, y, inc, bw, bh, height;
} Style;

static Style options_style(const Text *t, size_t opt, size_t opt_close, size_t ref_button)
{
    Style st = {41, 114, 36, 454, 35, 768};
    size_t v, n;
    int got_x = 0, got_y = 0, got_inc = 0;
    st.height = attr_int(t, opt, "height", 768);
    for (size_t i = next_tag(t, tag_end(t, opt)); i < opt_close; i = next_tag(t, i + 1)) {
        if (!tag_is(t, i, "POSITIONING", 0))
            continue;
        if (!got_x && attr_span(t, i, "x", &v, &n)) {
            st.x = attr_int(t, i, "x", st.x);
            got_x = 1;
        }
        if (!got_y && attr_span(t, i, "y", &v, &n)) {
            st.y = attr_int(t, i, "y", st.y);
            got_y = 1;
        }
        if (!got_inc && attr_span(t, i, "inc_y", &v, &n)) {
            st.inc = attr_int(t, i, "inc_y", st.inc);
            got_inc = 1;
        }
    }
    if (ref_button < opt_close) {
        st.bw = attr_int(t, ref_button, "width", st.bw);
        st.bh = attr_int(t, ref_button, "height", st.bh);
    }
    if (st.bw < 120)
        st.bw = 120;
    if (st.inc < st.bh)
        st.inc = st.bh;
    return st;
}

typedef struct Buf {
    char *p;
    size_t n, cap;
    int ok;
} Buf;

static void add(Buf *b, const char *s, size_t n)
{
    if (!b->ok || b->n + n >= b->cap) {
        b->ok = 0;
        return;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
}

static void addf(Buf *b, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (k < 0 || k >= (int)sizeof tmp)
        b->ok = 0;
    else
        add(b, tmp, (size_t)k);
}

/* The PLUGINS panel, made from the OPTIONS panel so it looks like it: the same start tag (name and
   title "Plugins"), the same children except its buttons and their POSITIONING (STYLE, textures and
   anything else a mod puts there), then the check boxes, the areas the loader draws in (inside the
   button column) and the panel's own four buttons in the same size and spacing. */
static void make_panel(const Text *t, size_t opt, size_t opt_close, const Style *st, const char *eol, Buf *b)
{
    size_t eol_n = strlen(eol), end = tag_end(t, opt), k = opt + 1;
    addf(b, "%s", "    <PLUGINS name=\"Plugins\" title=\"Plugins\"");
    while (k < end && name_char(t->s[k]))
        k++;
    for (;;) {
        while (k < end && isspace((unsigned char)t->s[k]))
            k++;
        if (k >= end || t->s[k] == '>' || t->s[k] == '/')
            break;
        size_t a0 = k;
        while (k < end && name_char(t->s[k]))
            k++;
        size_t an = k - a0, j = k;
        if (an == 0) {
            k++;
            continue;
        }
        while (j < end && isspace((unsigned char)t->s[j]))
            j++;
        if (j >= end || t->s[j] != '=') {
            k = j;
            continue;
        }
        j++;
        while (j < end && isspace((unsigned char)t->s[j]))
            j++;
        if (j >= end || (t->s[j] != '"' && t->s[j] != '\'')) {
            k = j;
            continue;
        }
        const char *close = memchr(t->s + j + 1, t->s[j], end - j - 1);
        if (!close)
            break;
        size_t vend = (size_t)(close - t->s) + 1;
        if (!(an == 4 && ieq(t->s + a0, "name", 4)) && !(an == 5 && ieq(t->s + a0, "title", 5))) {
            add(b, " ", 1);
            add(b, t->s + a0, vend - a0);
        }
        k = vend;
    }
    add(b, ">", 1);
    add(b, eol, eol_n);
    for (size_t i = next_tag(t, end); i < opt_close;) {
        if (t->s[i + 1] == '/')
            break;
        size_t e = elem_end(t, i, opt_close);
        if (e <= i)
            break;
        if (!tag_is(t, i, "POSITIONING", 0) && !tag_is(t, i, "BUTTON", 0)) {
            add(b, "      ", 6);
            add(b, t->s + i, e - i);
            add(b, eol, eol_n);
        }
        i = next_tag(t, e);
    }

    /* The areas: a pane over the button column from the first button's place, 372 px high when
       the window has room for it and the four buttons below (the stock size), never under 300. */
    int ch = st->height - st->y - 4 * st->inc - 6 - 40;
    if (ch > 372)
        ch = 372;
    if (ch < 300)
        ch = 300;
    /* A narrow column wraps the notice into more lines: 5 of 13 px below 400 px, taken from the list. */
    int nh = st->bw >= 400 ? 48 : 72;
    int x = st->x + 10, w = st->bw - 20, lt = st->y + 4, l = lt + 22, lh = ch - 216 - (nh - 48), dt = l + lh + 4,
        d = dt + 22, n = d + 108 + 6;
    addf(b, "      <IFACETEXLOAD id=\"TEX_PLUGINS_1\" file=\".\\art\\wrapper_pieces_02.tga\"/>%s", eol);
    addf(b, "      <IFACETEX name=\"check_off\" texture=\"TEX_PLUGINS_1\" left=\"1\" top=\"1\" width=\"25\" height=\"22\"/>%s", eol);
    addf(b, "      <IFACETEX name=\"check_on\" texture=\"TEX_PLUGINS_1\" left=\"1\" top=\"70\" width=\"25\" height=\"22\"/>%s", eol);
    static const char rect[] = "      <IFACERECT name=\"%s\" left=\"%d\" top=\"%d\" width=\"%d\" height=\"%d\"/>%s";
    addf(b, rect, "plugin_pane", st->x, st->y, st->bw, ch, eol);
    addf(b, rect, "plugin_list_title", x, lt, w, 22, eol);
    addf(b, rect, "plugin_list", x, l, w, lh, eol);
    addf(b, rect, "plugin_description_title", x, dt, w, 22, eol);
    addf(b, rect, "plugin_description", x, d, w, 108, eol);
    addf(b, rect, "plugin_notice", x, n, w, nh, eol);
    addf(b, "      <POSITIONING x=\"%d\" y=\"%d\">%s        <POSITIONING inc_y=\"%d\">%s", st->x, st->y + ch + 6, eol,
         st->inc, eol);
    static const char *const buttons[4][2] = {
        {"plugins_up", "Move Up"}, {"plugins_down", "Move Down"}, {"plugins_apply", "Apply and Restart"}, {"back", "Back"}};
    for (int i = 0; i < 4; i++)
        addf(b, "          <BUTTON name=\"%s\" title=\"%s\" width=\"%d\" height=\"%d\"/>%s", buttons[i][0], buttons[i][1],
             st->bw, st->bh, eol);
    addf(b, "        </POSITIONING>%s      </POSITIONING>%s    </PLUGINS>%s", eol, eol, eol);
}

static size_t mf_write(const char *xml, size_t len, char *out, size_t cap, const char *eol, const char *button,
                       size_t button_n, size_t button_at, size_t indent_from, size_t indent_len, int after,
                       int inline_button, size_t panel_at, const Buf *panel);

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

    size_t workshop = opt_close, back = opt_close, plugins = opt_close, first = opt_close;
    for (size_t i = next_tag(&t, tag_end(&t, opt)); i < opt_close; i = next_tag(&t, i + 1)) {
        if (!tag_is(&t, i, "BUTTON", 0))
            continue;
        if (first == opt_close)
            first = i;
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

    /* The mod's (or the game's) look: sizes from the button the Plugins button joins. */
    Style st = options_style(&t, opt, opt_close, workshop < opt_close ? workshop : back < opt_close ? back : first);
    char button[128];
    snprintf(button, sizeof button, "<BUTTON name=\"plugins\" title=\"Plugins\" width=\"%d\" height=\"%d\"/>", st.bw,
             st.bh);
    size_t button_n = strlen(button);
    Buf panel = {NULL, 0, 0, 1};
    if (panel_at < len) {
        panel.cap = (opt_close - opt) + 4096;
        panel.p = malloc(panel.cap);
        if (!panel.p)
            return 0;
        make_panel(&t, opt, opt_close, &st, eol, &panel);
    }
    size_t at = mf_write(xml, len, out, cap, eol, button, button_n, button_at, indent_from, indent_len, after,
                         inline_button, panel_at, &panel);
    free(panel.p);
    if (!at || !placed_ok(xml, top, out, at, panel_at < len))
        return 0;
    *where = w;
    return at;
}

/* The output: the text up to the button, the button, the text up to the panel, the panel, the rest. */
static size_t mf_write(const char *xml, size_t len, char *out, size_t cap, const char *eol, const char *button,
                       size_t button_n, size_t button_at, size_t indent_from, size_t indent_len, int after,
                       int inline_button, size_t panel_at, const Buf *panel)
{
    if (panel_at < len && !panel->ok)
        return 0;
    size_t at = 0;
    size_t pos = 0;
    if (button_at < len) {
        if (!put(out, cap, &at, xml, button_at, eol, 0))
            return 0;
        if (inline_button) {
            if (!put(out, cap, &at, button, button_n, eol, 0))
                return 0;
        } else if ((after && !put(out, cap, &at, eol, strlen(eol), eol, 0)) ||
                   !put(out, cap, &at, xml + indent_from, indent_len, eol, 0) ||
                   !put(out, cap, &at, button, button_n, eol, 0) ||
                   (!after && !put(out, cap, &at, eol, strlen(eol), eol, 0))) {
            return 0;
        }
        pos = button_at;
    }
    if (panel_at < len) {
        if (!put(out, cap, &at, xml + pos, panel_at - pos, eol, 0) ||
            !put(out, cap, &at, panel->p, panel->n, eol, 0))
            return 0;
        pos = panel_at;
    }
    if (!put(out, cap, &at, xml + pos, len - pos, eol, 0))
        return 0;
    out[at] = 0;
    return at;
}
