#include <pebble.h>

#define START_HOUR	0
#define START_MINUTE	1
#define STOP_HOUR	2
#define STOP_MINUTE	3
#define START_ALARM	4
#define STOP_ALARM	5
#define DURATION	6
#define SAMPLES		7
#define CHARGING	8
#define PLUGGED		9

/* Screen size info */
#if defined(PBL_RECT)
#define SCREEN_WIDTH 144
#define SCREEN_HEIGHT 168
#elif defined(PBL_ROUND)
#define SCREEN_WIDTH 180
#define SCREEN_HEIGHT 180
#endif

#define SECONDS (1)
#define MINUTES (60 * SECONDS)
#define HOURS (60 * MINUTES)
#define DAYS (24 * HOURS)

static Window *window;
static TextLayer *text_layer=NULL;

GFont my_font;
#define FONT_HEIGHT 30
#define FONT_WIDTH 30

static Window *top_menu_window=NULL;
SimpleMenuLayer *top_menu_layer=NULL;

Window *time_window=NULL;
TextLayer *time_layer=NULL;
Layer *line_layer=NULL;

char initial_text[]="Backlight:\nUp - enable\nMiddle - menu\nDown - disable";

char time_select_text[30];		/* hours : minutes */
int time_select_pointer;
#define TIME_SELECT_HOURS 1
#define TIME_SELECT_MINUTES 2
int time_select_hours;
int time_select_minutes;
int time_setting;
#define TIME_START	0
#define TIME_STOP	1
char *which[2]={"start", "stop"};
int start_hour;
int start_min;
int stop_hour;
int stop_min;
int time_duration;
WakeupId start_alarm_id;
WakeupId stop_alarm_id;
bool charging_mode=false;                       /* on while charging mode */
bool plugged_mode=false;                       /* on while plugged in mode */

Window *sample_window=NULL;
TextLayer *sample_layer=NULL;
uint samples=1;
#define SAMPLE_TEXT "Response of backlight in 1/10th second increments:"
char sample_text[sizeof(SAMPLE_TEXT) + 10];

/*
 * Main window menu
 */
void top_menu_callback(int index, void *context);
const SimpleMenuItem top_menu_items[]={
    {"Toggle backlight", NULL, NULL, (SimpleMenuLayerSelectCallback)top_menu_callback},
    {"Enable time", NULL, NULL, (SimpleMenuLayerSelectCallback)top_menu_callback},
    {"Disable time", NULL, NULL, (SimpleMenuLayerSelectCallback)top_menu_callback},
    {"Set Timeout", NULL, NULL, (SimpleMenuLayerSelectCallback)top_menu_callback},
    {"Clear times", NULL, NULL, (SimpleMenuLayerSelectCallback)top_menu_callback},
    {"Responsiveness", NULL, NULL, (SimpleMenuLayerSelectCallback)top_menu_callback},
    {"Charging light", NULL, NULL, (SimpleMenuLayerSelectCallback)top_menu_callback},
    {"Powered light", NULL, NULL, (SimpleMenuLayerSelectCallback)top_menu_callback},
};
#define num_top_menu_items (sizeof(top_menu_items) / sizeof(*top_menu_items))

const SimpleMenuSection top_menu_sections={.title="Main menu", .items=top_menu_items,
					   .num_items=num_top_menu_items};
#define num_top_menu_sections 1


/****************************************************************************
 * Setting start and stop times
 ****************************************************************************/


void
schedule_wakeup (WakeupId *alarm_id,
		 int hour, int min,
		 int which, int which_mem) 
{
    time_t now;
    struct tm *now_tick;
    time_t alarm_time;
    int32_t time_inc;

    now = time(0L);
    now_tick = localtime(&now);

    wakeup_cancel(*alarm_id);
    time_inc = ((hour - now_tick->tm_hour) * HOURS) +
	((min - now_tick->tm_min) * MINUTES) -
	now_tick->tm_sec;

    if (time_inc < 0) {
        /* place it back into the future */
        time_inc += 1 * DAYS;
    }
    app_log(APP_LOG_LEVEL_WARNING,
	    __FILE__,
	    __LINE__,
	    "time increment is %d", (int)time_inc);

    alarm_time = now + time_inc;
    *alarm_id = wakeup_schedule(alarm_time, which, false);
    persist_write_int(which_mem, (uint32_t)(*alarm_id));
}


void
save_and_initiate_timer (int which) 
{


    if (which == TIME_START) {
	persist_write_int(START_HOUR, (uint32_t)start_hour);
	persist_write_int(START_MINUTE, (uint32_t)start_min);

	schedule_wakeup(&start_alarm_id, start_hour, start_min, TIME_START, START_ALARM);
    } else { /* TIME_STOP */
	persist_write_int(STOP_HOUR, (uint32_t)stop_hour);
	persist_write_int(STOP_MINUTE, (uint32_t)stop_min);

	schedule_wakeup(&stop_alarm_id, stop_hour, stop_min, TIME_STOP, STOP_ALARM);
    }
}


void
format_time (void) 
{

    snprintf(time_select_text, sizeof(time_select_text),
	     "Setting\n%s\n%02u:%02u",
	     which[time_setting],
	     time_select_hours,
	     time_select_minutes);
}



static void
select_time_handler(ClickRecognizerRef recognizer, void *context) {

    app_log(APP_LOG_LEVEL_WARNING,
	    __FILE__,
	    __LINE__,
	    "Time Click select");

    if (time_select_pointer == TIME_SELECT_HOURS) {
	time_select_pointer = TIME_SELECT_MINUTES;
        layer_mark_dirty(line_layer);
    } else {
	app_log(APP_LOG_LEVEL_WARNING,
		__FILE__,
		__LINE__,
		"Time selected is '%s'", time_select_text);

	if (time_setting == TIME_START) {
	    start_hour = time_select_hours;
	    start_min = time_select_minutes;

	    save_and_initiate_timer(TIME_START);
	} else {
	    stop_hour = time_select_hours;
	    stop_min = time_select_minutes;

	    save_and_initiate_timer(TIME_STOP);
	}
	
	window_stack_pop(true);
    }
}

static void
up_time_handler(ClickRecognizerRef recognizer, void *context) {

    if (time_select_pointer == TIME_SELECT_HOURS) {
	time_select_hours++;
	if (time_select_hours > 23)
	    time_select_hours = 0;
	else if (time_select_hours < 0)
	    time_select_hours = 23;
    } else {
	time_select_minutes++;
	if (time_select_minutes > 59)
	    time_select_minutes = 0;
	else if (time_select_minutes < 0)
	    time_select_minutes = 59;
    }

    format_time();
    text_layer_set_text(time_layer, time_select_text);
}

static void
down_time_handler(ClickRecognizerRef recognizer, void *context) {

    if (time_select_pointer == TIME_SELECT_HOURS) {
	time_select_hours--;
	if (time_select_hours > 23)
	    time_select_hours = 0;
	else if (time_select_hours < 0)
	    time_select_hours = 23;
    } else {
	time_select_minutes--;
	if (time_select_minutes > 59)
	    time_select_minutes = 0;
	else if (time_select_minutes < 0)
	    time_select_minutes = 59;
    }

    format_time();
    text_layer_set_text(time_layer, time_select_text);
}

static void
time_config_provider(void *context) {

  window_single_click_subscribe(BUTTON_ID_SELECT, select_time_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_time_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_time_handler);

  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_time_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_time_handler);
}

void
line_update_proc (struct Layer *layer, GContext* ctx) 
{

    if (time_select_pointer == TIME_SELECT_HOURS) {
        graphics_draw_line(ctx, GPoint((SCREEN_WIDTH/2)-22, 0),
                           GPoint((SCREEN_WIDTH/2)-2, 0));
        graphics_draw_line(ctx, GPoint((SCREEN_WIDTH/2)-22, 1),
                           GPoint((SCREEN_WIDTH/2)-2, 1));
        graphics_draw_line(ctx, GPoint((SCREEN_WIDTH/2)-22, 2),
                           GPoint((SCREEN_WIDTH/2)-2, 2));
    } else {
        graphics_draw_line(ctx, GPoint((SCREEN_WIDTH/2)+2, 0),
                           GPoint((SCREEN_WIDTH/2)+22, 0));
        graphics_draw_line(ctx, GPoint((SCREEN_WIDTH/2)+2, 1),
                           GPoint((SCREEN_WIDTH/2)+22, 1));
        graphics_draw_line(ctx, GPoint((SCREEN_WIDTH/2)+2, 2),
                           GPoint((SCREEN_WIDTH/2)+22, 2));
    }
}



void
set_time (int which) 
{

    /*
     * Create the base window
     */
    if (!time_window)
	time_window = window_create();

    /*
     * Create the hours and minutes layers
     */
    if (!time_layer) {
#if defined(PBL_RECT)
	time_layer = text_layer_create(GRect(0, 12, /* origin */
					     SCREEN_WIDTH, FONT_HEIGHT*3)); /* size */
	line_layer = layer_create(GRect(0, FONT_HEIGHT*3, /* origin */
					     SCREEN_WIDTH, 5)); /* size */
#elif defined(PBL_ROUND)
	time_layer = text_layer_create(GRect(0, 30, /* origin */
					     SCREEN_WIDTH, FONT_HEIGHT*3)); /* size */
	line_layer = layer_create(GRect(0, (FONT_HEIGHT*3)+15, /* origin */
					     SCREEN_WIDTH, 5)); /* size */
#endif
        layer_set_update_proc(line_layer, line_update_proc);
    }
    
    text_layer_set_font(time_layer, my_font);
    text_layer_set_text_alignment(time_layer, GTextAlignmentCenter);
    layer_add_child(window_get_root_layer(time_window), (Layer *)time_layer);
    layer_add_child(window_get_root_layer(time_window), line_layer);

    time_setting = which;		/* which one we're setting */
    time_select_pointer = TIME_SELECT_HOURS;
    time_select_hours = (which == TIME_START) ? start_hour : stop_hour;
    time_select_minutes = (which == TIME_START) ? start_min : stop_min;
    format_time();
    text_layer_set_text(time_layer, time_select_text);
    window_set_click_config_provider(time_window, time_config_provider);
    
    window_stack_push(time_window, true);
}


/***********************************************************/
/* Routines for setting backlight duration information     */
/***********************************************************/


NumberWindow *number_window=NULL;
    

void
restart_worker (void) 
{
    if (app_worker_is_running()) {
        app_log(APP_LOG_LEVEL_WARNING,
                __FILE__,
                __LINE__,
                "restarting worker");
        app_worker_kill();
        psleep(1000);                   /* wait a little while */
        app_worker_launch();
    } else {
        app_worker_launch();
    }
}


void
number_window_select (NumberWindow *nw, void *context) 
{
    uint32_t val;

    time_duration = number_window_get_value(nw);
    app_log(APP_LOG_LEVEL_WARNING,
            __FILE__,
            __LINE__,
            "Number Window select: duration = %d", time_duration);

    persist_write_int(DURATION, (uint32_t)time_duration);
    val = persist_read_int(DURATION);
    if (val) {
        time_duration = (int)val;
    }
    app_log(APP_LOG_LEVEL_WARNING,
            __FILE__,
            __LINE__,
            "duration re-read as %d", time_duration);

    /* Restart worker, so we get the new values */
    restart_worker();

    window_stack_pop(true);
    number_window_destroy(number_window);
    number_window = NULL;
}

NumberWindowCallbacks number_window_callbacks={
    NULL,
    NULL,
    number_window_select
};



void
set_timeout (void) 
{
    /*
     * Create a window for setting a number
     */
    if (number_window) {
        number_window_destroy(number_window);
    }
    number_window = number_window_create("Set Duration", number_window_callbacks, NULL);

    if (!number_window) {
        app_log(APP_LOG_LEVEL_WARNING,
                __FILE__,
                __LINE__,
                "Error creating number window");
        return;                         /* internal error */
    }
    
    number_window_set_max(number_window, 60);
    number_window_set_min(number_window, 1);
    number_window_set_value(number_window, time_duration);

    window_stack_push((Window *)number_window, true);
}


void
clear_times (void) 
{
    start_hour = 0;
    start_min = 0;
    stop_hour = 0;
    stop_min = 0;
    save_and_initiate_timer(TIME_START);
    save_and_initiate_timer(TIME_STOP);
}

void
destroy_samples_window(void) 
{
    
    window_destroy(sample_window);
    text_layer_destroy(sample_layer);
    sample_window = NULL;
    sample_layer = NULL;
}

static void
select_sample_handler(ClickRecognizerRef recognizer, void *context) {

    uint32_t val;

    app_log(APP_LOG_LEVEL_WARNING,
            __FILE__,
            __LINE__,
            "Samples Window select: samples = %d", samples);

    persist_write_int(SAMPLES, (uint32_t)samples);
    val = persist_read_int(SAMPLES);
    if (val) {
        samples = (int)val;
    }
    app_log(APP_LOG_LEVEL_WARNING,
            __FILE__,
            __LINE__,
            "samples re-read as %d", samples);

    /* Restart worker, so we get the new values */
    if (app_worker_is_running()) {
        app_log(APP_LOG_LEVEL_WARNING,
                __FILE__,
                __LINE__,
                "restarting worker");
        app_worker_kill();
        psleep(1000);                   /* wait a little while */
        app_worker_launch();
    }

    window_stack_pop(true);
    destroy_samples_window();
}

void
update_samples_window (void) {

    snprintf(sample_text, sizeof(sample_text),
             "%s\n%d", SAMPLE_TEXT, samples);

    text_layer_set_text_alignment(sample_layer, GTextAlignmentCenter);
    text_layer_set_text(sample_layer, sample_text);
}


static void
up_sample_handler (ClickRecognizerRef recognizer, void *context) {

    samples++;
    if (samples >= 100)
        samples = 100;
    update_samples_window();
}

static void
down_sample_handler (ClickRecognizerRef recognizer, void *context) {

    samples--;
    if (samples <= 1)
        samples = 1;
    update_samples_window();
}

static void
sample_config_provider (void *context) {

  window_single_click_subscribe(BUTTON_ID_SELECT, select_sample_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_sample_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_sample_handler);

  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, up_sample_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_sample_handler);
}

void
set_samples (void) 
{

    app_log(APP_LOG_LEVEL_WARNING,
            __FILE__,
            __LINE__,
            "set_samples");
    /*
     * Create a window for setting a number
     */
    if (sample_window) {
        destroy_samples_window();
    }

    app_log(APP_LOG_LEVEL_WARNING,
            __FILE__,
            __LINE__,
            "set_samples 2");
    /*
     * Create the base window
     */
    if (!sample_window)
	sample_window = window_create();

    /*
     * Create the hours and minutes layers
     */
    if (!sample_layer) {
#if defined(PBL_RECT)
	sample_layer = text_layer_create(GRect(0, 20, /* origin */
                                               SCREEN_WIDTH, SCREEN_HEIGHT-20)); /* size */
#elif defined(PBL_ROUND)
	sample_layer = text_layer_create(GRect(20, (SCREEN_HEIGHT/2)-(FONT_HEIGHT*2), /* origin */
                                               SCREEN_WIDTH-40, FONT_HEIGHT*4)); /* size */
#endif        
    }
    
    text_layer_set_font(sample_layer, my_font);
    text_layer_set_text_alignment(sample_layer, GTextAlignmentCenter);
    layer_add_child(window_get_root_layer(sample_window), (Layer *)sample_layer);

    snprintf(sample_text, sizeof(sample_text),
             "%s\n%d", SAMPLE_TEXT, samples);
    text_layer_set_text(sample_layer, sample_text);
    window_set_click_config_provider(sample_window, sample_config_provider);
    
    window_stack_push(sample_window, true);
}


/*
 * Toggle "on while charging" mode
 */
static void
on_while_charging (void) 
{
    static char buffer[40];

    if (charging_mode) {
        charging_mode = false;
    } else {
        charging_mode = true;
    }

    persist_write_bool(CHARGING, charging_mode);

    snprintf(buffer, sizeof(buffer), "Light will be %s during charging",
             charging_mode ? "on" : "off");
    text_layer_set_text(text_layer, buffer);

    restart_worker();
}

/*
 * Toggle "on while powered" mode
 */
static void
on_while_plugged (void) 
{
    static char buffer[40];

    if (plugged_mode) {
        plugged_mode = false;
    } else {
        plugged_mode = true;
    }

    persist_write_bool(PLUGGED, plugged_mode);

    snprintf(buffer, sizeof(buffer), "Light will be %s while plugged in",
             plugged_mode ? "on" : "off");
    text_layer_set_text(text_layer, buffer);

    restart_worker();
}



/*************************************
 * Main menu definitions
 */
void
top_menu_callback (int index, void *context) 
{
    
    app_log(APP_LOG_LEVEL_WARNING,
	    __FILE__,
	    __LINE__,
	    "Main Menu callback on row %d\n", index);

    switch ( index ) {
    case 0:				/* toggle auto-backlight */
	if (app_worker_is_running()) {
	    text_layer_set_text(text_layer, "Light off");
	    app_worker_kill();
	} else {
	    text_layer_set_text(text_layer, "Light on");
	    app_worker_launch();
	}
	break;

    case 1:				/* Set Start Time */
	set_time(TIME_START);
	return;

    case 2:				/* Set Start Time */
	set_time(TIME_STOP);
	return;

    case 3:				/* Set Timeout */
	set_timeout();
	return;

    case 4:				/* Clear times */
	clear_times();
	return;

    case 5:				/* Samples per callback */
	set_samples();
	return;

    case 6:
        on_while_charging();            /* keep light on while charging */
        break;

    case 7:
        on_while_plugged();            /* keep light on while powered */
        break;
    }

    window_stack_pop(true); /* menu window */
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {

    app_log(APP_LOG_LEVEL_WARNING,
	    __FILE__,
	    __LINE__,
	    "Top Click handler");

    window_stack_push(top_menu_window, true);
    text_layer_set_text_alignment(text_layer, GTextAlignmentCenter);
    text_layer_set_text(text_layer, initial_text);
    simple_menu_layer_set_selected_index(top_menu_layer, 0 /* toggle */, false);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {

	app_worker_launch();
	text_layer_set_text(text_layer, "Light on");
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {

	app_worker_kill();
	text_layer_set_text(text_layer, "Light off");
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);

#if defined(PBL_RECT)
  text_layer = text_layer_create(GRect(0, 20, /* origin */
                                               SCREEN_WIDTH, SCREEN_HEIGHT-20)); /* size */
#elif defined(PBL_ROUND)
  text_layer = text_layer_create(GRect(20, (SCREEN_HEIGHT/2)-(FONT_HEIGHT*2), /* origin */
                                               SCREEN_WIDTH-40, FONT_HEIGHT*4)); /* size */
#endif        
  text_layer_set_font(text_layer, my_font);
  text_layer_set_text_alignment(text_layer, GTextAlignmentCenter);
  text_layer_set_text(text_layer, initial_text);
  text_layer_set_overflow_mode(text_layer, GTextOverflowModeWordWrap);
  layer_add_child(window_layer, text_layer_get_layer(text_layer));
}

static void window_unload(Window *window) {
  text_layer_destroy(text_layer);
  text_layer = NULL;
}

static void init(void) {
  window = window_create();
  window_set_click_config_provider(window, click_config_provider);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  const bool animated = true;

  Layer *window_layer = window_get_root_layer(window);

//  text_layer = text_layer_create((GRect) { .origin = { 0, 0 }, .size = { bounds.size.w, bounds.size.h } });

  my_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

/*
 * Create the top level menu
 */
  top_menu_window = window_create();
  top_menu_layer = simple_menu_layer_create(layer_get_frame(window_layer), top_menu_window,
					    &top_menu_sections, num_top_menu_sections, NULL);
  layer_add_child(window_get_root_layer(top_menu_window), simple_menu_layer_get_layer(top_menu_layer));


/*
 * Start main window
 */
  window_stack_push(window, animated);
}

static void deinit(void) {
  if (time_window)
      window_destroy(time_window);
  if (number_window)
      number_window_destroy(number_window);
  if (time_layer)
      text_layer_destroy(time_layer);
  if (line_layer)
      layer_destroy(line_layer);
  if (sample_window)
      window_destroy(sample_window);
  if (sample_layer)
      text_layer_destroy(sample_layer);
  if (text_layer)
      text_layer_destroy(text_layer);
  if (top_menu_layer)
      simple_menu_layer_destroy(top_menu_layer);
  if (top_menu_window)
      window_destroy(top_menu_window);
  window_destroy(window);
}

void
read_alarm_data (void) 
{
    uint32_t val;
    
    val = persist_read_int(START_ALARM);
    if (val) {
	start_alarm_id = (WakeupId)val;
	APP_LOG(APP_LOG_LEVEL_DEBUG, "start_alarm_id=%u", (uint)val);
    }

    val = persist_read_int(STOP_ALARM);
    if (val) {
	stop_alarm_id = (WakeupId)val;
	APP_LOG(APP_LOG_LEVEL_DEBUG, "stop_alarm_id=%u", (uint)val);
    }

    val = persist_read_int(START_HOUR);
    if (val) {
	start_hour = (int)val;
	APP_LOG(APP_LOG_LEVEL_DEBUG, "start_hour=%u", (uint)val);
    }
    val = persist_read_int(START_MINUTE);
    if (val) {
	start_min = (int)val;
	APP_LOG(APP_LOG_LEVEL_DEBUG, "start_min=%u", (uint)val);
    }

    val = persist_read_int(STOP_HOUR);
    if (val) {
	stop_hour = (int)val;
	APP_LOG(APP_LOG_LEVEL_DEBUG, "stop_hour=%u", (uint)val);
    }
    val = persist_read_int(STOP_MINUTE);
    if (val) {
	stop_min = (int)val;
	APP_LOG(APP_LOG_LEVEL_DEBUG, "stop_min=%u", (uint)val);
    }
    if (persist_exists(DURATION)) {
        val = persist_read_int(DURATION);
        if (val) {
            time_duration = (int)val;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "time_duration=%u", (uint)val);
        }
    } else {
        time_duration = 5;              /* default */
        APP_LOG(APP_LOG_LEVEL_DEBUG, "init time_duration=5");
    }
    if (persist_exists(SAMPLES)) {
        val = persist_read_int(SAMPLES);
        if (val) {
            samples = (int)val;
            APP_LOG(APP_LOG_LEVEL_DEBUG, "samples=%u", (uint)val);
        }
    } else {
        samples = 1;              /* default */
        APP_LOG(APP_LOG_LEVEL_DEBUG, "init samples=1");
    }
    val = persist_read_bool(CHARGING);
    if (val) {
	charging_mode = (bool)val;
	APP_LOG(APP_LOG_LEVEL_DEBUG, "charging_mode=%u", (uint)charging_mode);
    }
    val = persist_read_bool(PLUGGED);
    if (val) {
	plugged_mode = (bool)val;
	APP_LOG(APP_LOG_LEVEL_DEBUG, "plugged_mode=%u", (uint)plugged_mode);
    }
}


void
handle_wakeup (WakeupId id, int32_t cookie) 
{

    if (cookie == TIME_START) {
        app_log(APP_LOG_LEVEL_WARNING,
                __FILE__,
                __LINE__,
                "Start backlight");
	app_worker_launch();
	wakeup_cancel(start_alarm_id);
	schedule_wakeup(&start_alarm_id, start_hour, start_min, TIME_START, START_ALARM);
    } else { /* TIME_STOP */
        app_log(APP_LOG_LEVEL_WARNING,
                __FILE__,
                __LINE__,
                "Stop backlight");

        light_enable(false);            /* make sure it's off */
	app_worker_kill();
	wakeup_cancel(stop_alarm_id);
	schedule_wakeup(&stop_alarm_id, stop_hour, stop_min, TIME_STOP, STOP_ALARM);
    }
}



int main(void) {
    AppLaunchReason reason;
    WakeupId id = 0;
    int32_t cookie;

    reason = launch_reason();

    app_log(APP_LOG_LEVEL_WARNING,
            __FILE__,
            __LINE__,
            "Backlight - launch_reason=%d", (int)reason);

    if (reason == APP_LAUNCH_WAKEUP) {
	wakeup_get_launch_event(&id, &cookie);
	read_alarm_data();
	handle_wakeup(id, cookie);
    } else {
        init();
	read_alarm_data();
        wakeup_cancel_all();            /* complete reset */
        save_and_initiate_timer(TIME_START);
        save_and_initiate_timer(TIME_STOP);
        restart_worker();
	
	APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);
	
	app_event_loop();
	deinit();
    }
}
