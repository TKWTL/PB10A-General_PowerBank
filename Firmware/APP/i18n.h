#ifndef PB10A_I18N_H
#define PB10A_I18N_H
#include <stdint.h>
/* Shared UI/functions translation API. English-only until catalogs exist. */
typedef enum {I18N_LANG_EN=0, I18N_LANG_ZH_CN=1} i18n_language_t;
void I18N_SetLanguage(i18n_language_t language);
i18n_language_t I18N_GetLanguage(void);
/* Resolve English keys at presentation time. Keep menu prefixes and
 * format placeholders in the catalog; numeric data and technical units
 * need no translation. Missing keys fall back to the original text. */
const char *I18N_Text(const char *english_key);
#endif
