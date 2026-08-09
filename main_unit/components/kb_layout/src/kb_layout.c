/*
 * kb_layout: active-layout selector and registry.
 */

#include "kb_layout.h"

#include <string.h>

#include "sdkconfig.h"

/* Kconfig bool symbols are #defined to 1 when set and undefined
 * otherwise; normalise them to 0/1 compile-time constants. */
#ifdef CONFIG_SK_LANG_ENABLE_US
#define SK_EN_US 1
#else
#define SK_EN_US 0
#endif
#ifdef CONFIG_SK_LANG_ENABLE_DE
#define SK_EN_DE 1
#else
#define SK_EN_DE 0
#endif
#ifdef CONFIG_SK_LANG_ENABLE_FR
#define SK_EN_FR 1
#else
#define SK_EN_FR 0
#endif
#ifdef CONFIG_SK_LANG_ENABLE_UA
#define SK_EN_UA 1
#else
#define SK_EN_UA 0
#endif

static const kb_layout_t *const s_all[] = {
    &kb_layout_us,
    &kb_layout_de,
    &kb_layout_fr,
    &kb_layout_ua,
};

static const int s_count = sizeof(s_all) / sizeof(s_all[0]);

static const kb_layout_t *s_active = &kb_layout_us;

/* Compile-time availability of a layout, driven by the per-language
 * SK_LANG_ENABLE_* Kconfig switches. Only "available" layouts are
 * offered in the settings menu and participate in the rotation. */
static bool lang_available_name(const char *name)
{
    if (strcmp(name, "US") == 0) return SK_EN_US;
    if (strcmp(name, "DE") == 0) return SK_EN_DE;
    if (strcmp(name, "FR") == 0) return SK_EN_FR;
    if (strcmp(name, "UA") == 0) return SK_EN_UA;
    return false;
}

bool kb_layout_is_available(int i)
{
    if (i < 0 || i >= s_count) return false;
    return lang_available_name(s_all[i]->name);
}

static void resolve_kconfig_default_once(void)
{
    static bool s_done;
    if (s_done) return;
    s_done = true;
    /* Boot with the first available (Kconfig-activated) layout. */
    for (int i = 0; i < s_count; ++i) {
        if (kb_layout_is_available(i)) { s_active = s_all[i]; return; }
    }
    /* No layout activated in Kconfig: fall back to the first. */
    s_active = s_all[0];
}

const kb_layout_t *kb_layout_active(void)
{
    resolve_kconfig_default_once();
    return s_active;
}

bool kb_layout_set_active_by_name(const char *name)
{
    const kb_layout_t *l = kb_layout_by_name(name);
    if (!l) return false;
    s_active = l;
    return true;
}

int kb_layout_count(void) { return s_count; }

const kb_layout_t *kb_layout_by_index(int i)
{
    if (i < 0 || i >= s_count) return NULL;
    return s_all[i];
}

const kb_layout_t *kb_layout_by_name(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < s_count; ++i) {
        if (strcmp(s_all[i]->name, name) == 0) return s_all[i];
    }
    return NULL;
}

int kb_layout_index_of(const kb_layout_t *l)
{
    for (int i = 0; i < s_count; ++i) {
        if (s_all[i] == l) return i;
    }
    return -1;
}

/* ----- Enabled-language set ----- */

/* Default: enable the two fully-implemented layouts (US + UA).
 * The compile-time default layout (resolved above) is force-added
 * so the active layout is always part of the enabled set. */
static uint32_t s_enabled_mask;
static bool     s_enabled_done;

static uint32_t all_mask(void)
{
    return (s_count >= 32) ? 0xFFFFFFFFu : ((1u << s_count) - 1u);
}

/* Kconfig bool symbols are #defined to 1 when set and undefined
 * otherwise; normalised to 0/1 constants above (SK_EN_*). */

static void resolve_enabled_default_once(void)
{
    if (s_enabled_done) return;
    s_enabled_done = true;
    /* Seed the enabled set from the Kconfig per-language switches
     * (SK_LANG_ENABLE_*). Defaults enable US + UA; DE / FR are
     * 1x1 stubs and off by default. */
    uint32_t m = 0;
    const struct { const char *name; bool on; } cfg[] = {
        { "US", SK_EN_US },
        { "DE", SK_EN_DE },
        { "FR", SK_EN_FR },
        { "UA", SK_EN_UA },
    };
    for (size_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); ++i) {
        if (!cfg[i].on) continue;
        const kb_layout_t *l = kb_layout_by_name(cfg[i].name);
        if (l) m |= 1u << kb_layout_index_of(l);
    }
    /* Always include the active (compile-time default) layout. */
    int ai = kb_layout_index_of(kb_layout_active());
    if (ai >= 0) m |= 1u << ai;
    if (m == 0) m = 1u;  /* never empty */
    s_enabled_mask = m & all_mask();
}

uint32_t kb_layout_enabled_mask(void)
{
    resolve_enabled_default_once();
    return s_enabled_mask;
}

void kb_layout_set_enabled_mask(uint32_t mask)
{
    resolve_enabled_default_once();
    mask &= all_mask();
    if (mask == 0) return;  /* refuse to disable every language */
    s_enabled_mask = mask;
    /* Keep the active layout inside the enabled set. */
    int ai = kb_layout_index_of(s_active);
    if (ai < 0 || !(s_enabled_mask & (1u << ai))) {
        for (int i = 0; i < s_count; ++i) {
            if (s_enabled_mask & (1u << i)) { s_active = s_all[i]; break; }
        }
    }
}

bool kb_layout_is_enabled(int i)
{
    if (i < 0 || i >= s_count) return false;
    return (kb_layout_enabled_mask() & (1u << i)) != 0;
}

void kb_layout_set_enabled(int i, bool on)
{
    if (i < 0 || i >= s_count) return;
    uint32_t m = kb_layout_enabled_mask();
    if (on) m |= 1u << i;
    else    m &= ~(1u << i);
    kb_layout_set_enabled_mask(m);
}

const kb_layout_t *kb_layout_next_enabled(const kb_layout_t *cur)
{
    resolve_enabled_default_once();
    int start = kb_layout_index_of(cur);
    if (start < 0) start = 0;
    for (int step = 1; step <= s_count; ++step) {
        int idx = (start + step) % s_count;
        if (s_enabled_mask & (1u << idx)) return s_all[idx];
    }
    return cur;
}
