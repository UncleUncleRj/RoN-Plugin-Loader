/* Plugin discovery (plugins\<id>.dll with plugins\<id>.ini) and plugins\load-order.txt. */
#ifndef RON_ORDER_H
#define RON_ORDER_H

#include <stddef.h>
#include <wchar.h>

#define ORDER_MAX 128

typedef struct OrderItem {
    wchar_t name[64];   /* plugin id: the DLL's file name without .dll */
    int enabled;
    int is_new;         /* found this start, not listed before */
} OrderItem;

/* Plugin ids in plugins_dir, sorted case-insensitively. A DLL without a same-name .ini is passed to
   on_skip (its file name; on_skip may be NULL). Subfolders are not searched. */
int order_discover(const wchar_t *plugins_dir, wchar_t (*names)[64], int max, void (*on_skip)(const wchar_t *dll));

/* load-order.txt text: one id per line, leading '-' = disabled, '#' comments, blank lines skipped,
   BOM tolerated, later case-insensitive duplicates dropped. */
int order_parse(const char *utf8, size_t len, OrderItem *out, int max);

/* Listed ids that were found keep their order and state; listed ids not found are dropped and
   counted in *dropped; found ids not listed are appended (enabled, new) in discovery order. */
int order_merge(const OrderItem *listed, int n_listed, const wchar_t (*found)[64], int n_found, OrderItem *out,
                int max, int *dropped);

/* UTF-8, CRLF, a header comment line, "-Name" for a disabled plugin. Returns the length written
   (NUL-terminated), 0 when buf is too small. */
size_t order_serialize(const OrderItem *items, int n, char *buf, size_t cap);

#endif
