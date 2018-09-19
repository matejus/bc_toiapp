#ifndef _APPLICATION_H
#define _APPLICATION_H

#define TOI_APPNAME "IoT/TOI app"
#define TOI_VERSION "v0.1"

#include <bcl.h>

typedef struct
{
    uint8_t channel;
    float value;
    bc_tick_t next_pub;

} event_param_t;

void pir_event_handler(bc_module_pir_t *self, bc_module_pir_event_t event, void *event_param);
void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param);
void battery_event_handler(bc_module_battery_event_t event, void *event_param);
void tmp112_event_handler(bc_tmp112_t *self, bc_tmp112_event_t event, void *event_param);
void humidity_tag_event_handler(bc_tag_humidity_t *self, bc_tag_humidity_event_t event, void *event_param);
void lis2_event_handler(bc_lis2dh12_t *self, bc_lis2dh12_event_t event, void *event_param);
void lux_module_event_handler(bc_opt3001_t *self, bc_opt3001_event_t event, void *event_param);

//static void radio_event_handler(bc_radio_event_t event, void *event_param);

#endif // _APPLICATION_H
