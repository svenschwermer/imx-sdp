#include "stages.h"
#include "config.h"
#include "sdp.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WITH_UDEV
#include "udev.h"
#else
#include <unistd.h>
#endif

struct sdp_stage_
{
    uint16_t usb_vid;
    uint16_t usb_pid;
    sdp_step *steps;
    struct sdp_stage_ *next;
};

static int parse_stage(char *const s, struct sdp_stage_ *stage)
{
    char *saveptr = NULL;
    char *tok = strtok_r(s, ",", &saveptr);
    if (!tok)
    {
        fprintf(stderr, "ERROR: Stage \"%s\" invalid\n", s);
        return 1;
    }

    unsigned int vid, pid;
    int conversions = sscanf(tok, "%04x:%04x", &vid, &pid);
    if (conversions != 2)
    {
        fprintf(stderr, "ERROR: Stage didn't contain USB VID/PID");
        if (errno != 0)
            fprintf(stderr, ": %s\n", strerror(errno));
        else
            fputc('\n', stderr);
        return 1;
    }

    stage->usb_vid = vid;
    stage->usb_pid = pid;

    sdp_step *last_step;
    while ((tok = strtok_r(NULL, ",", &saveptr)))
    {
        sdp_step *step = sdp_parse_step(tok);
        if (!step)
        {
            fprintf(stderr, "ERROR: Failed to parse step\n");
            return 1;
        }

        if (!stage->steps)
            stage->steps = step;
        else
            sdp_set_next_step(last_step, step);
        last_step = step;
    }

    return 0;
}

// Parse stages from command line arguments
sdp_stages *sdp_parse_stages(int count, char *s[])
{
    sdp_stages *stages = NULL;
    sdp_stages *last = NULL;

    for (int i = 0; i < count; ++i)
    {
        sdp_stages *stage = malloc(sizeof(struct sdp_stage_));
        if (!stage)
        {
            fprintf(stderr, "ERROR: Failed to allocate stage %d\n", i + 1);
            goto free_stages;
        }

        stage->steps = NULL;
        stage->next = NULL;

        if (last)
        {
            last->next = stage;
            last = stage;
        }
        else
            stages = last = stage;

        if (parse_stage(s[i], stage))
        {
            fprintf(stderr, "ERROR: Failed to parse stage %d\n", i + 1);
            goto free_stages;
        }
    }

    return stages;

free_stages:
    sdp_free_stages(stages);
    return NULL;
}

static int parse_uint16(const char *s, uint16_t *value)
{
	char *end;
	unsigned long ul = strtoul(s, &end, 16);
	if (s == end || ul > UINT16_MAX)
		return -1;
	*value = (uint16_t)ul;
	return 0;
}

// Upon success, takes ownership of steps
sdp_stages *sdp_new_stage(const char *vid, const char *pid, sdp_step *steps)
{
	if (!vid || !pid)
	{
		fprintf(stderr, "ERROR: Stage VIP/PID unset\n");
		return NULL;
	}
	if (!steps)
	{
		fprintf(stderr, "ERROR: Steps unset\n");
		return NULL;
	}

    sdp_stages *stage = malloc(sizeof(struct sdp_stage_));
    if (!stage)
    {
        fprintf(stderr, "ERROR: Failed to allocate stage\n");
        return NULL;
    }
    stage->steps = steps;
    stage->next = NULL;

    if (parse_uint16(vid, &stage->usb_vid))
    {
        fprintf(stderr, "ERROR: Invalid VID value\n");
        goto free_stage;
    }
    if (parse_uint16(pid, &stage->usb_pid))
    {
        fprintf(stderr, "ERROR: Invalid PID value\n");
        goto free_stage;
    }

    return stage;

free_stage:
    free(stage);
    return NULL;
}

sdp_stages *sdp_append_stage(sdp_stages *list, sdp_stages *stage)
{
    if (!list)
		return stage;

	sdp_stages *it = list;
	while (it->next)
		it = it->next;
	it->next = stage;
	return list;
}

#ifdef WITH_UDEV
static hid_device *_open_device(sdp_udev *udev, uint16_t vid, uint16_t pid, const char *usb_path, bool quiet)
{
    hid_device *result = NULL;

    struct hid_device_info * const enumerator = hid_enumerate(vid, pid);
    if (!enumerator)
    {
        if (!quiet)
            fprintf(stderr, "ERROR: Failed to enumerate HID devices: %ls\n", hid_error(NULL));
        return NULL;
    }

    const char *device_path = NULL;
    for (struct hid_device_info *i = enumerator; !device_path && i; i = i->next)
    {
        if (!usb_path || sdp_udev_matching_usb_path(udev, i->path, usb_path))
            device_path = i->path;
    }

    if (device_path)
    {
        result = hid_open_path(device_path);
        if (!result && !quiet)
            fprintf(stderr, "ERROR: Failed to open device: %ls\n", hid_error(result));
    }
    else if (!quiet)
        fprintf(stderr, "ERROR: No matching device found\n");

    hid_free_enumeration(enumerator);

    return result;
}
#else
static hid_device *_open_device(sdp_udev *udev, uint16_t vid, uint16_t pid, const char *path, bool quiet)
{
    struct hid_device *result = hid_open(vid, pid, NULL);
    if (!result && !quiet)
        fprintf(stderr, "ERROR: Failed to open device: %ls\n", hid_error(result));
    return result;
}
#endif

static hid_device *open_device(uint16_t vid, uint16_t pid, const char *usb_path, bool wait)
{
    hid_device *result = NULL;

#ifdef WITH_UDEV
    sdp_udev *udev = sdp_udev_init();
    if (!udev)
    {
        fprintf(stderr, "ERROR: Failed to initialize udev\n");
        goto out;
    }

#else
    if (usb_path)
    {
        fprintf(stderr, "ERROR: Filtering by path is only supported with udev support\n");
        goto out;
    }
#endif

    result = _open_device(udev, vid, pid, usb_path, wait);
    if (!result)
    {
        if (!wait)
            goto free_udev;

        printf("Waiting for device...\n");

#ifdef WITH_UDEV
        const char *devpath = sdp_udev_wait(udev, vid, pid, usb_path, 5000);
        if (!devpath)
        {
            fprintf(stderr, "ERROR: Timeout!\n");
            goto free_udev;
        }
        result = hid_open_path(devpath);
        free((void *)devpath);
        if (!result)
            fprintf(stderr, "ERROR: Failed to open device: %ls\n", hid_error(result));
#else
        do
        {
            usleep(500000ul); // 500ms
            result = hid_open(vid, pid, NULL);
        } while (!result);
#endif
    }

free_udev:
#ifdef WITH_UDEV
    sdp_udev_free(udev);
out:
#endif

    return result;
}

int sdp_execute_stages(sdp_stages *stages, bool initial_wait, const char *usb_path)
{
    int res = hid_init();
    if (res)
        fprintf(stderr, "ERROR: hidapi init failed\n");

    int i = 0;
    for (struct sdp_stage_ *stage = stages; !res && stage; stage = stage->next, i++)
    {
        printf("[Stage %d] VID=0x%04x PID=0x%04x\n", i + 1, stage->usb_vid, stage->usb_pid);

        bool wait = initial_wait || (i > 0);
        hid_device *handle = open_device(stage->usb_vid, stage->usb_pid, usb_path, wait);
        if (!handle)
        {
            res = 1;
            break;
        }

        uint32_t hab_status, status;
        res = sdp_error_status(handle, &hab_status, &status);
        if (res)
            break;

        if (sdp_execute_steps(handle, stage->steps))
        {
            fprintf(stderr, "ERROR: Failed to execute stage %d\n", i + 1);
            res = 1;
        }

        hid_close(handle);
    }

    if (hid_exit())
        fprintf(stderr, "ERROR: hidapi exit failed\n");

    if (!res)
        printf("All stages done\n");

    return res;
}

void sdp_free_stages(sdp_stages *stages)
{
    while (stages)
	{
        sdp_free_steps(stages->steps);		
        void *const to_be_freed = stages;
		stages = stages->next;
		free(to_be_freed);
	}
}
