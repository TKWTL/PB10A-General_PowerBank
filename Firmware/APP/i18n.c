#include "i18n.h"
static i18n_language_t s_language=I18N_LANG_EN;
void I18N_SetLanguage(i18n_language_t language)
{ s_language=language; }
i18n_language_t I18N_GetLanguage(void){return s_language;}
const char *I18N_Text(const char *english_key)
{ return english_key ? english_key : ""; } /* Placeholder: no catalog yet. */
