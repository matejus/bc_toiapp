#include <application.h>

//#define BATTERY_MINI      // mini module is used

#define SERVICE_INTERVAL_INTERVAL (60 * 60 * 1000)
#define BATTERY_UPDATE_INTERVAL (60 * 60 * 1000)
#define SENSOR_PUB_NO_CHANGE_INTEVAL (15 * 60 * 1000)
#define SENSOR_UPDATE_SERVICE_INTERVAL (5 * 1000)
#define TEMPERATURE_PUB_VALUE_CHANGE 1.0f
#define HUMIDITY_PUB_VALUE_CHANGE 5.0f
#define LUX_PUB_VALUE_CHANGE 5.0f
#define ACC_UPDATE_INTERVAL (60*1000)        // resend position of node
#define ACC_BUMP_INTERVAL 1000               // resend door bump after some time
#define DOOR_UPDATE_INTERVAL (500)           // check door status 
#define TOI_STATE_UPDATE_INTERVAL (2*60*1000)  // resend toi status every 30 sec
#define TOI_GAMEPLAY_INTERVAL (120*1000)     // door still closed but no repeated move event inside 
#define TOI_MIN_MOVE_TIME (7*1000)           // someone is on toilet when door opened
#define PIR_NOMOVE_TIME 700                  // have to be closed before move event is triggered
#define LED_MESSAGE_FLASH 100                // short flash when message sent to computer
#define TOI_MESSAGE_SUBTOPIC "toi/-/state"   // part of message sent over radio
#define ACC_MESSAGE_SUBTOPIC "accelerometer/1:19/%s"
#define GPIO_DOOR BC_GPIO_P10                // door sensor 

// LED instance
bc_led_t _led;

// Button instance
bc_button_t _button;
static uint16_t _button_count = 0;

// Thermometer instance
bc_tmp112_t _tmp112;
event_param_t _temperature_event_param = { .next_pub = 0 };

// PIR motion sensor
bc_module_pir_t _pir;
bc_tick_t _last_movement_time = 0;
bool _pir_status = false;

bool _occupied = false;

bc_tick_t _sent_time = 0;

// accelerometer
bc_lis2dh12_t _acc;
bc_lis2dh12_alarm_t _acc_alarm;  // alarm settings
bc_tick_t _bump_time;

// humidity tag
bc_tag_humidity_t _humidity;
event_param_t _humidity_event_param = { .next_pub = 0 };

// lux meter tag
bc_opt3001_t _lux;
event_param_t _lux_event_param = { .next_pub = 0 };


// for radio
//static uint64_t _my_id;

static bool _door_status = false;
static bc_tick_t _door_changed_time = 0;

void application_init(void)
{
    // Initialize LED
    bc_led_init(&_led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&_led, BC_LED_MODE_ON);

    bc_radio_init(BC_RADIO_MODE_NODE_SLEEPING);


    // Initialize button
    bc_button_init(&_button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&_button, button_event_handler, NULL);

    // Initialize battery - rem depends on battery module
    #ifdef BATTERY_MINI
    bc_module_battery_init(BC_MODULE_BATTERY_FORMAT_MINI);
    #else
    bc_module_battery_init(BC_MODULE_BATTERY_FORMAT_STANDARD);
    #endif
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize thermometer sensor on core module
    _temperature_event_param.channel = BC_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_ALTERNATE;
    bc_tmp112_init(&_tmp112, BC_I2C_I2C0, 0x49);
    bc_tmp112_set_event_handler(&_tmp112, tmp112_event_handler, &_temperature_event_param);
    bc_tmp112_set_update_interval(&_tmp112, SENSOR_UPDATE_SERVICE_INTERVAL);

    // humidity
    _humidity_event_param.channel = BC_TAG_HUMIDITY_I2C_ADDRESS_DEFAULT;
    bc_tag_humidity_init(&_humidity, BC_TAG_HUMIDITY_REVISION_R2, BC_I2C_I2C0, BC_TAG_HUMIDITY_I2C_ADDRESS_DEFAULT);
    bc_tag_humidity_set_event_handler(&_humidity, humidity_tag_event_handler, &_humidity_event_param);
    bc_tag_humidity_set_update_interval(&_humidity, SENSOR_UPDATE_SERVICE_INTERVAL);

    // initialize door sensor
    bc_gpio_init(GPIO_DOOR);
    bc_gpio_set_mode(GPIO_DOOR, BC_GPIO_MODE_INPUT);
    bc_gpio_set_pull(GPIO_DOOR, BC_GPIO_PULL_DOWN);

    bc_module_pir_init(&_pir);
    bc_module_pir_set_event_handler(&_pir, pir_event_handler, NULL);

    // accelerometer
    bc_lis2dh12_init(&_acc, BC_I2C_I2C0, 0x19);
    //bc_lis2dh12_set_update_interval(&_acc, ACC_UPDATE_INTERVAL);
    _acc_alarm.x_high = true;
    _acc_alarm.threshold = 1;
    bc_lis2dh12_set_alarm(&_acc, &_acc_alarm);
    bc_lis2dh12_set_event_handler(&_acc, lis2_event_handler, NULL);

    // luxmeter
    bc_opt3001_init(&_lux, BC_I2C_I2C0, 0x44);
    bc_opt3001_set_update_interval(&_lux, SENSOR_UPDATE_SERVICE_INTERVAL);
    // set evend handler (what to do when tag update is triggered)
    bc_opt3001_set_event_handler(&_lux, lux_module_event_handler, &_lux_event_param);

    // to enable backward communication with node
    //bc_radio_listen();
    //bc_radio_set_event_handler(radio_event_handler, NULL);
    bc_radio_pairing_request(TOI_APPNAME, TOI_VERSION);

    bc_led_set_mode(&_led, BC_LED_MODE_OFF);
}

void send_status()
{
    int value = _occupied?1:0;
    value += _door_status?2:0;

    bc_tick_t t = bc_tick_get();
    if (_door_status)
    {
        // door closed, check gamblers
        if ((_door_changed_time<_last_movement_time) && (t-_last_movement_time)<TOI_GAMEPLAY_INTERVAL)
            value+=4;
    }
    else
    {
        if ((t-_last_movement_time)<TOI_MIN_MOVE_TIME)
            value+=4;
    }
    
    _sent_time = t;
    bc_radio_pub_int(TOI_MESSAGE_SUBTOPIC, &value);
}

void set_occupied(bool status)
{
    // resend status if required
    if (status != _occupied)
    {
        _occupied = status;
        send_status();
    }
}

bool get_door_status()
{
    uint8_t door = bc_gpio_get_input(GPIO_DOOR);
    return (door==1);
}

void application_task()
{
    bc_tick_t tick = bc_tick_get();
    bool status = get_door_status();
    //bool motion = (tick-_last_movement_time < PIR_PUB_MIN_INTERVAL);
      
    if (status != _door_status)
    {
        _door_changed_time = bc_tick_get();
        
        if (!status)
            set_occupied(false);
        
        _door_status = status;
        
        bc_led_pulse(&_led, 4*LED_MESSAGE_FLASH);
        send_status();
    }
    else if (tick-_last_movement_time > TOI_MIN_MOVE_TIME && _pir_status == true)
    {
        _pir_status = false;
        send_status();
    }
    else if (tick-_sent_time > TOI_STATE_UPDATE_INTERVAL)
    {
        send_status();
    }

    // Repeat this task again
    bc_scheduler_plan_current_relative(DOOR_UPDATE_INTERVAL);
}


void pir_event_handler(bc_module_pir_t *self, bc_module_pir_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    if (event == BC_MODULE_PIR_EVENT_MOTION)
    {
        bc_tick_t t = bc_tick_get();
        bool send = (t-_last_movement_time)>TOI_MIN_MOVE_TIME;
        _last_movement_time = t;
        _pir_status = true;
        
        if (send)
            send_status();

        bool door = get_door_status();
        if (door && (t-_door_changed_time)>PIR_NOMOVE_TIME)
            set_occupied(true);

        bc_led_pulse(&_led, LED_MESSAGE_FLASH);
    }
    else if (event == BC_MODULE_PIR_EVENT_ERROR)
    {
        bc_radio_pub_string(TOI_MESSAGE_SUBTOPIC, "PIR error");
    }
}



void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    if (event == BC_BUTTON_EVENT_PRESS)
    {
        _button_count++;
        bc_led_pulse(&_led, 100);
        bc_radio_pub_push_button(&_button_count);
    }
    else if (event == BC_BUTTON_EVENT_HOLD)
    {
        bc_radio_pairing_request(TOI_APPNAME, TOI_VERSION);
        bc_led_pulse(&_led, 50*LED_MESSAGE_FLASH);
    }
}

void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    float voltage;

    if (bc_module_battery_get_voltage(&voltage))
    {
        bc_radio_pub_battery(&voltage);
    }
}

void tmp112_event_handler(bc_tmp112_t *self, bc_tmp112_event_t event, void *event_param)
{
    float value;
    event_param_t *param = (event_param_t *)event_param;

    if (event == BC_TMP112_EVENT_UPDATE)
    {
        if (bc_tmp112_get_temperature_celsius(self, &value))
        {
            if ((fabsf(value - param->value) >= TEMPERATURE_PUB_VALUE_CHANGE) || (param->next_pub < bc_scheduler_get_spin_tick()))
            {
                bc_radio_pub_temperature(param->channel, &value);

                param->value = value;
                param->next_pub = bc_scheduler_get_spin_tick() + SENSOR_PUB_NO_CHANGE_INTEVAL;
            }
        }
    }
}

void humidity_tag_event_handler(bc_tag_humidity_t *self, bc_tag_humidity_event_t event, void *event_param)
{
    float value;
    event_param_t *param = (event_param_t *)event_param;

    if (event != BC_TAG_HUMIDITY_EVENT_UPDATE)
    {
        return;
    }

    if (bc_tag_humidity_get_humidity_percentage(self, &value))
    {
        if ((fabs(value - param->value) >= HUMIDITY_PUB_VALUE_CHANGE) || (param->next_pub < bc_scheduler_get_spin_tick()))
        {
            bc_radio_pub_humidity(param->channel, &value);
            param->value = value;
            param->next_pub = bc_scheduler_get_spin_tick() + SENSOR_PUB_NO_CHANGE_INTEVAL;
        }
    }
}

void send_acc_message(const char *event, const char *value)
{
    char msg[100];
    sprintf(msg, ACC_MESSAGE_SUBTOPIC, event);
    bc_radio_pub_string(msg, value);
}

void lis2_event_handler(bc_lis2dh12_t *self, bc_lis2dh12_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    bc_lis2dh12_result_g_t result;

    if (event == BC_LIS2DH12_EVENT_UPDATE) 
    {
        bc_lis2dh12_get_result_g(&_acc, &result);
        char message[100];
        sprintf(message, "X:%f\tY:%f\tZ:%f", result.x_axis, result.y_axis, result.z_axis);
        // send message
        send_acc_message("position", message);
    } 
    else if (event == BC_LIS2DH12_EVENT_ALARM) 
    {
        bc_tick_t t = bc_tick_get();
        if (t-_bump_time>ACC_BUMP_INTERVAL)
        {
            _bump_time=t;
            bc_led_pulse(&_led, LED_MESSAGE_FLASH);
            // send message
            send_acc_message("alarm", "1");
        }
    }
    else  if (event == BC_LIS2DH12_EVENT_ERROR)
    {
        send_acc_message("error", "");
    }
}


void lux_module_event_handler(bc_opt3001_t *self, bc_opt3001_event_t event, void *event_param) {
    float illumination = 0.0;
    event_param_t *param = (event_param_t *)event_param;

    if (event == BC_OPT3001_EVENT_UPDATE) {
        bc_opt3001_get_illuminance_lux(self, &illumination);

        if ((fabs(illumination - param->value) >= LUX_PUB_VALUE_CHANGE) || (param->next_pub < bc_scheduler_get_spin_tick()))
        {
            bc_radio_pub_luminosity(self->_i2c_channel, &illumination);
            param->value = illumination;
            param->next_pub = bc_scheduler_get_spin_tick() + SENSOR_PUB_NO_CHANGE_INTEVAL;
        }

    }
}


/*
static void radio_event_handler(bc_radio_event_t event, void *event_param)
{
    (void) event_param;

    bc_led_set_mode(&_led, BC_LED_MODE_OFF);

    if (event == BC_RADIO_EVENT_ATTACH)
    {
        bc_led_pulse(&_led, 1000);
    }
    else if (event == BC_RADIO_EVENT_DETACH)
    {
        bc_led_pulse(&_led, 1000);
    }
    else if (event == BC_RADIO_EVENT_INIT_DONE)
    {
        _my_id = bc_radio_get_my_id();
    }
}
*/
