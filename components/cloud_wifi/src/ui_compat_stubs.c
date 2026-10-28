#include "ui.h"

/*
 * SquareLine object hooks used by wifi_management.c.
 * Must live in this component (not main): without --start-group, main.a is
 * scanned before wifi_management.o is extracted, so stubs in libmain.a are never pulled.
 * Hub themes leave these NULL — UI callbacks become no-ops.
 */
lv_obj_t *ui_Label1 = NULL;
lv_obj_t *ui_Label3 = NULL;
lv_obj_t *ui_Label24 = NULL;
lv_obj_t *ui_Label42 = NULL;
lv_obj_t *ui_Label43 = NULL;
lv_obj_t *ui_Screen7 = NULL;
lv_obj_t *ui_Dropdown1 = NULL;
