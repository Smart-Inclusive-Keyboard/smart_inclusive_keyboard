/* Narrator no-op stub for boards without PSRAM + speaker, or when
 * CONFIG_NARRATOR_ENABLE is unset. */
#include "narrator.h"
void narrator_init(void)                          { }
void narrator_speak_selection(void)               { }
void narrator_speak_hid(unsigned hid_usage)       { (void)hid_usage; }
void narrator_speak_key(const kb_key_t *key)      { (void)key; }
void narrator_speak_key_ex(const kb_key_t *key, bool shifted)
{
    (void)key; (void)shifted;
}
void narrator_speak_token(const char *token)      { (void)token; }
