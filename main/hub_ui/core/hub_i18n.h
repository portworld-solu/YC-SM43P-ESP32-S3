#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True when UI language is Simplified Chinese. */
bool hub_lang_zh(void);

/** Pick zh or en string based on settings.lang_zh. */
const char *hub_tr(const char *zh, const char *en);

#ifdef __cplusplus
}
#endif
