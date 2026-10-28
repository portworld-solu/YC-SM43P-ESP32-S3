#include "hub_i18n.h"
#include "hub_model.h"

bool hub_lang_zh(void)
{
    return hub_model()->settings.lang_zh;
}

const char *hub_tr(const char *zh, const char *en)
{
    if (hub_lang_zh()) {
        return zh ? zh : "";
    }
    return en ? en : "";
}
