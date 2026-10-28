#include "ui_runtime.h"

#include "hub_climate_offset.h"
#include "hub_model.h"
#include "hub_ui.h"

#include <string.h>

/* Closed libbt_ctrl.a calls this from bt_management_start(); keep the TU linked. */
void bt_switch_control_linker_keep(void) {}

void ui_runtime_apply(void) {}
void ui_runtime_disable_btn_grow_everywhere(void) {}
void ui_runtime_disable_btn_grow_on(lv_obj_t *root) { (void)root; }
bool ui_runtime_screen_switch_busy(void) { return false; }
void ui_runtime_rgb_commit_active_screen(void) {}
void ui_runtime_rgb_commit_full_screen(void) {}
void ui_runtime_set_network_info(const char *mac, const char *ip)
{
    (void)mac;
    (void)ip;
}
void ui_runtime_step_screen3_temp(int delta) { (void)delta; }
void ui_runtime_step_screen5_temp(int delta) { (void)delta; }
void ui_runtime_fill_cloud_snapshot(ui_runtime_cloud_snapshot_t *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}
bool ui_runtime_allow_mqtt_temp_item(uint8_t item_id)
{
    (void)item_id;
    return false;
}
void ui_runtime_screen10_pick_and_home(uint8_t pick) { (void)pick; }
void ui_runtime_screen_change_by_obj(lv_obj_t *scr) { (void)scr; }

void ui_runtime_indoor_apply_climate(float temp_c, int rh_pct)
{
    hub_climate_offset_init();
    hub_climate_note_raw(temp_c, rh_pct);
    hub_climate_apply_to_model(true);
}

void ui_runtime_indoor_apply_temp(int temp_c)
{
    hub_model_t *m = hub_model();
    const int rh = m ? m->rh : 0;
    /* Treat as raw sample without known separate RH raw — use model RH as baseline. */
    float raw_c = (float)temp_c;
    int raw_rh = rh;
    if (hub_climate_have_raw()) {
        raw_rh = hub_climate_raw_rh();
    } else {
        raw_rh = rh - hub_climate_rh_offset();
    }
    ui_runtime_indoor_apply_climate(raw_c, raw_rh);
}
