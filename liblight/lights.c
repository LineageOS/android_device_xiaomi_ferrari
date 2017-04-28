/*
 * Copyright (C) 2014-2015 The CyanogenMod Project
 * Copyright (c) 2017 thewisenerd <thewisenerd@protonmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "lights"

#include <cutils/log.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <hardware/lights.h>

/******************************************************************************/

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct light_state_t g_notification;
static struct light_state_t g_attention;
static struct light_state_t g_battery;

const char* const BACKLIGHT_FILE
	= "/sys/class/leds/lcd-backlight/brightness";

const char* const BUTTONS_FILE
	= "/sys/class/leds/button-backlight/brightness";

const char* const LED_FILE[] = {
	"/sys/class/leds/red/brightness",
	"/sys/class/leds/green/brightness",
	"/sys/class/leds/blue/brightness",
};

const char* const LED_RISE[] = {
	"/sys/class/leds/red/risetime",
	"/sys/class/leds/green/risetime",
	"/sys/class/leds/blue/risetime",
};

const char* const LED_HIGH[] = {
	"/sys/class/leds/red/delay_on",
	"/sys/class/leds/green/delay_on",
	"/sys/class/leds/blue/delay_on",
};

const char* const LED_FALL[] = {
	"/sys/class/leds/red/falltime",
	"/sys/class/leds/green/falltime",
	"/sys/class/leds/blue/falltime",
};

const char* const LED_LOW[] = {
	"/sys/class/leds/red/delay_off",
	"/sys/class/leds/green/delay_off",
	"/sys/class/leds/blue/delay_off",
};

const size_t LED_SIZE
	= sizeof(LED_FILE) / sizeof(LED_FILE[0]);

const int rf_times_ms[] = {
	   2,  262,  524,  1049,
	2097, 4194, 8389, 16780
};

const size_t rf_times_size
	= sizeof(rf_times_ms) / sizeof(rf_times_ms[0]);

inline size_t get_closest_rf(int ms) {
	int curr = 0;
	size_t i = 0;

	for (i = 0; i < rf_times_size; i++) {
		if (abs(ms - rf_times_ms[i]) < abs(ms - rf_times_ms[curr]))
			curr = i;
	}

	return curr;
}

/**
 * device methods
 */

void init_globals(void)
{
	// init the mutex
	pthread_mutex_init(&g_lock, NULL);
}

static int
write_int(char const* path, int value)
{
	int fd;
	static int already_warned = 0;

	fd = open(path, O_RDWR);
	if (fd >= 0) {
		char buffer[20];
		int bytes = snprintf(buffer, sizeof(buffer), "%d\n", value);
		ssize_t amt = write(fd, buffer, (size_t)bytes);
		close(fd);
		return amt == -1 ? -errno : 0;
	} else {
		if (already_warned == 0) {
			ALOGE("write_int failed[%d] to open %s\n", fd, path);
			already_warned = 1;
		}
		return -errno;
	}
}

static int
rgb_to_brightness(const struct light_state_t *state)
{
	int color = state->color & 0x00ffffff;
	return ((77*((color>>16)&0x00ff))
			+ (150*((color>>8)&0x00ff))
			+ (29*(color&0x00ff))) >> 8;
}

static int
is_lit(struct light_state_t const* state)
{
	return state->color & 0x00ffffff;
}

inline void reset_leds(void) {
	size_t i;

	for (i = 0; i < LED_SIZE; i++) {
		write_int(LED_FILE[i], 0);	// reset state
		write_int(LED_RISE[i], 0);	// reset rise
		write_int(LED_FALL[i], 0);	// reset fall
	}
}

void write_delay(
	const char* const rf_path[],
	const char* const delay_path[],
	int ms
) {
	size_t i;
	int tMS;

	tMS = get_closest_rf(ms);		// compute r/f
	for (i = 0; i < LED_SIZE; i++) {
		ALOGD("%s: writing r/f=%d to %s\n", __func__, tMS, rf_path[i]);
		write_int(rf_path[i], tMS);	// write r/f to sysfs
	}

	// balance correction
	tMS = ms - rf_times_ms[tMS];

	ALOGD("%s: balance correction = %d\n", __func__, tMS);

	if (tMS < 0) {
		for (i = 0; i < LED_SIZE; i++) {
			ALOGD("%s: writing r/f=0 to %s\n", __func__, delay_path[i]);
			write_int(delay_path[i], 0);
		}
	} else {
		for (i = 0; i < LED_SIZE; i++) {
			ALOGD("%s: writing r/f=%d to %s\n", __func__, tMS, delay_path[i]);
			write_int(delay_path[i], tMS);
		}
	}
}

static int
set_speaker_light_locked(struct light_device_t *dev,
				struct light_state_t const *state)
{
	unsigned int colorRGB;
	int alpha;
	int rgb[3];
	int onMS, offMS;
	size_t i;

	colorRGB = state->color;

	rgb[0] = (colorRGB >> 16) & 0xFF;	// red
	rgb[1] = (colorRGB >> 8) & 0xFF;	// green
	rgb[2] = colorRGB & 0xFF;		// blue

	switch (state->flashMode) {
	case LIGHT_FLASH_TIMED:
	case LIGHT_FLASH_HARDWARE:
		onMS = state->flashOnMS;
		offMS = state->flashOffMS;
		break;
	case LIGHT_FLASH_NONE:
	default:
		onMS = 0;
		offMS = 0;
		break;
	}

	ALOGD("%s: onMS = %d; offMS = %d;\n", __func__, onMS, offMS);

	// reset LED state
	reset_leds();

	// write RGB
	for (i = 0; i < LED_SIZE; i++) {
		write_int(LED_FILE[i], rgb[i]);
	}

	/*
	 * The ROM implements the following speeds.
	 *  on: 250, 500, 1000, 2500, 5000
	 * off: 250, 500, 1000, 2500, 5000
	 *
	 * rise/fall times aren't mentioned anywhere in android.
	 * so let us assume the decision is for us to take.
	 *
	 * kernel supports the following rise/fall times:
	 * 2048us, 262ms, 524ms, 1.049s, 2.097s, 4.194s, 8.389s, 16.78s
	 *    0  ;   1  ;   2  ;   3   ;    4  ;    5  ;   6   ;   7
	 *
	 * tryin to ease-in-out, let try to use the following values;
	 * 		rise		thigh		fall		tlow
	 *  250ms :	262ms		0ms		262ms		0ms
	 *  500ms :	524ms		0ms		524ms		0ms
	 * 1000ms :	1049ms		0ms		1049ms		0ms
	 * 2500ms :	2097ms		~500ms		2097ms		~500ms
	 * 5000ms :	4194ms		~800ms		4194ms		~800ms
	 *
	 * _assuming_ onMS = offMS for now, let's use rise, and thigh for now.
	 *
	 * onUS = onMS * 1000;
	 * risetimes = [ 2048, 262000, 524000, 1049000, 2097000, 4194000, 8389000, 16780000 ]
	 * delta = abs(onUS - risetimes[0])
	 * deltaI = 0
	 * for I, t in risetimes:
	 * 	if abs(onUS - t) < delta:
	 * 		delta = t
	 * 		deltaI = I
	 * rise = deltai
	 * write_rise(rise)
	 * thigh = (onUS - risetimes[deltaI])
	 * if (thigh < 0):
	 * 	; # nothing to do
	 * else:
	 * 	write_thigh(thigh)
	 *
	 * optimizations to come.
	 *
	 * quirks:
	 *   - thigh can only go till 9781.248ms
	 *   - tlow can go till 76.890112ms
	 *   - nothing in sysfs (that i knowof) to set thigh/tlow
	 */

	if (onMS == 0 || onMS == 1) { // LED always ON
		;
	} else { // LED blink
		write_delay(LED_RISE, LED_HIGH, onMS);
		write_delay(LED_FALL, LED_LOW, offMS);
	}

	return 0;
}

static void
handle_speaker_light_locked(struct light_device_t *dev)
{
	ALOGD("%s: g_attention->color: %x\n", __func__, g_attention.color);
	ALOGD("%s: g_notification->color: %x\n", __func__, g_notification.color);
	ALOGD("%s: g_battery->color: %x\n", __func__, g_battery.color);

	if (is_lit(&g_attention)) {
		set_speaker_light_locked(dev, &g_attention);
	} else if (is_lit(&g_notification)) {
		set_speaker_light_locked(dev, &g_notification);
	} else {
		set_speaker_light_locked(dev, &g_battery);
	}
}

static int
set_light_backlight(struct light_device_t *dev,
			const struct light_state_t *state)
{
	int err = 0;
	int brightness = rgb_to_brightness(state);

	ALOGD("%s: color: %x\n", __func__, state->color);
	ALOGD("%s: brightness: %d\n", __func__, brightness);

	pthread_mutex_lock(&g_lock);

	err = write_int(BACKLIGHT_FILE, brightness);

	pthread_mutex_unlock(&g_lock);

	return err;
}

static int
set_light_buttons(struct light_device_t *dev,
			const struct light_state_t *state)
{
	int err = 0;
	int brightness = rgb_to_brightness(state);

	ALOGD("%s: color: %x\n", __func__, state->color);
	ALOGD("%s: brightness: %d\n", __func__, brightness);

	pthread_mutex_lock(&g_lock);

	err = write_int(BUTTONS_FILE, brightness);

	pthread_mutex_unlock(&g_lock);

	return err;
}

static int
set_light_battery(struct light_device_t *dev,
			const struct light_state_t *state)
{
	pthread_mutex_lock(&g_lock);

	if (state == NULL) {
		ALOGE("%s: state == NULL!\n", __func__);
		return 0;
	}

	g_battery = *state;

	ALOGD("%s: color: %x\n", __func__, state->color);

	handle_speaker_light_locked(dev);

	pthread_mutex_unlock(&g_lock);

	return 0;
}

static int
set_light_notifications(struct light_device_t *dev,
			const struct light_state_t *state)
{
	pthread_mutex_lock(&g_lock);

	if (state == NULL) {
		ALOGE("%s: state == NULL!\n", __func__);
		return 0;
	}

	g_notification = *state;

	ALOGD("%s: color: %x\n", __func__, state->color);

	handle_speaker_light_locked(dev);

	pthread_mutex_unlock(&g_lock);

	return 0;
}

static int
set_light_attention(struct light_device_t *dev,
			const struct light_state_t *state)
{
	pthread_mutex_lock(&g_lock);

	if (state == NULL) {
		ALOGE("%s: state == NULL!\n", __func__);
		return 0;
	}

	g_attention = *state;

	ALOGD("%s: color: %x\n", __func__, state->color);

	handle_speaker_light_locked(dev);

	pthread_mutex_unlock(&g_lock);

	return 0;
}

/** Close the lights device */
static int
close_lights(struct light_device_t *dev)
{
	if (dev) {
		free(dev);
	}
	return 0;
}

/******************************************************************************/

/**
 * module methods
 */

/** Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t *module, const char *name,
			struct hw_device_t **device)
{
	int (*set_light)(struct light_device_t *dev,
		const struct light_state_t *state);

	if (0 == strcmp(LIGHT_ID_BACKLIGHT, name))
		set_light = set_light_backlight;
	else if (0 == strcmp(LIGHT_ID_BUTTONS, name))
		set_light = set_light_buttons;
	else if (0 == strcmp(LIGHT_ID_BATTERY, name))
		set_light = set_light_battery;
	else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name))
		set_light = set_light_notifications;
	else if (0 == strcmp(LIGHT_ID_ATTENTION, name))
		set_light = set_light_attention;
	else
		return -EINVAL;

	pthread_once(&g_init, init_globals);

	struct light_device_t *dev = malloc(sizeof(struct light_device_t));
	memset(dev, 0, sizeof(*dev));

	dev->common.tag = HARDWARE_DEVICE_TAG;
	dev->common.version = 0;
	dev->common.module = (struct hw_module_t*)module;
	dev->common.close = (int (*)(struct hw_device_t*))close_lights;
	dev->set_light = set_light;

	*device = (struct hw_device_t*)dev;
	return 0;
}

static struct hw_module_methods_t lights_module_methods = {
	.open =  open_lights,
};

/*
 * The lights Module
 */
struct hw_module_t HAL_MODULE_INFO_SYM = {
	.tag = HARDWARE_MODULE_TAG,
	.version_major = 1,
	.version_minor = 0,
	.id = LIGHTS_HARDWARE_MODULE_ID,
	.name = "Xiaomi Lights Module",
	.author = "The CyanogenMod Project",
	.methods = &lights_module_methods,
};
