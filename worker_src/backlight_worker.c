#include <pebble_worker.h>

#define DURATION	6               /* copied from backlight.c */
#define SAMPLES		7               /* copied from backlight.c */
#define CHARGING	8
#define PLUGGED		9
#define AMBIENT		10

void light_enable_interaction(void);
void light_enable(bool val);

bool auto_backlight = false;
bool light_on = false;
time_t watch_level_start = 0;
uint32_t time_duration=15;               /* default */
uint32_t samples=1;               /* default */
bool charging=false;
bool light_charging = false;            /* current have light on while charging */
bool plugged=false;
bool light_plugged = false;            /* current have light on while powered */
bool ambient=false;


/*
 * Trying out turning on the backlight via the function which understands
 * ambient light.
 */
void
backlight_enable (bool val) 
{

    if (val)
        light_enable_interaction();
}



void
light_callback (void *data) 
{ 

    light_on = false;
    light_enable(false);
}


#define X_RANGE_LOW -250
#define X_RANGE_HIGH 250
#define Y_RANGE_LOW -1000
#define Y_RANGE_HIGH -300

void
handle_accel(AccelData *data, uint32_t num_samples)
{
    static bool watch_outside_range = true;

    uint i;

    if (light_charging == true || light_plugged == true) {
        /* Don't bother, but leave the light on while charging or powered */
        return;
    }

    for (i = 0 ; i < num_samples ; i++) {
        if (data[i].x < X_RANGE_HIGH && data[i].x > X_RANGE_LOW &&
            data[i].y > Y_RANGE_LOW && data[i].y < Y_RANGE_HIGH) {
            if (light_on == false &&
                watch_level_start == 0 &&
                watch_outside_range) {
                watch_level_start = time(0L); /* record when level started */
                watch_outside_range = false;
                break;
            }
        } else if ((data[i].x > (X_RANGE_HIGH + 50) || data[i].x < (X_RANGE_LOW - 50)) ||
                   (data[i].y < (Y_RANGE_LOW - 100) || data[i].y > (Y_RANGE_HIGH + 100))) {
            watch_level_start = 0;
            watch_outside_range = true;
            break;
        }
    }
    
    if (watch_level_start != 0 && (time(0L) - watch_level_start) > 0) {
        if (ambient) {
            backlight_enable(true);
        } else {
            light_enable(true);
        }
	light_on = true;
	app_timer_register(time_duration * 1000, light_callback, NULL);
	watch_level_start = 0;
    }
}



void
battery_handler (BatteryChargeState charge) 
{

    if (charge.is_charging && charging) {
        light_enable(true);
        light_charging = true;
        light_on = true;
    } else if (charge.is_plugged && plugged) {
        light_enable(true);
        light_plugged = true;
        light_on = true;
    } else {
        light_charging = false;
        light_plugged = false;
        if (light_on == true) {
            light_enable(false);
            light_on = false;
        }
    }
}



int main(void) {
    uint32_t	val;

    val = persist_read_int(DURATION);
    if (val) {
	time_duration = (int)val;
    } else {
        time_duration = 5;              /* default */
    }
    APP_LOG(APP_LOG_LEVEL_WARNING, "time_duration=%u", (uint)val);

    val = persist_read_int(SAMPLES);
    if (val) {
	samples = (int)val;
    } else {
        samples = 1;              /* default */
    }
    APP_LOG(APP_LOG_LEVEL_WARNING, "samples=%u", (uint)val);

    accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
    accel_data_service_subscribe(samples, handle_accel);
    auto_backlight = true;

    val = persist_read_bool(CHARGING);
    if (val) {
	charging = (bool)val;
    } else {
        charging = false;              /* default */
    }
    APP_LOG(APP_LOG_LEVEL_WARNING, "light when charging = %u",
            (uint)val);

    val = persist_read_bool(PLUGGED);
    if (val) {
	plugged = (bool)val;
    } else {
        plugged = false;              /* default */
    }
    APP_LOG(APP_LOG_LEVEL_WARNING, "light when plugged = %u",
            (uint)val);

    val = persist_read_bool(AMBIENT);
    if (val) {
	ambient = (bool)val;
    } else {
        ambient = false;              /* default */
    }
    APP_LOG(APP_LOG_LEVEL_WARNING, "ambient=%u", (uint)val);

    if (charging || plugged) {
        battery_state_service_subscribe(battery_handler);
    }

    worker_event_loop();
}
