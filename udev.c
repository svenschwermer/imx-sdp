#include "udev.h"
#include <errno.h>
#include <libudev.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sdp_udev_
{
    struct udev *udev;
    struct udev_monitor *mon;
};

sdp_udev *sdp_udev_init()
{
    sdp_udev *result = calloc(1, sizeof(sdp_udev));
    if (!result)
        goto out;

    result->udev = udev_new();
    if (!result->udev)
        goto cleanup;

    result->mon = udev_monitor_new_from_netlink(result->udev, "udev");
    if (!result->mon)
        goto cleanup;

    int ret = udev_monitor_filter_add_match_subsystem_devtype(result->mon, "hidraw", NULL);
    if (ret)
        goto cleanup;

    ret = udev_monitor_enable_receiving(result->mon);
    if (ret)
        goto cleanup;

    return result;

cleanup:
    sdp_udev_free(result);
out:
    return NULL;
}

void sdp_udev_free(sdp_udev *udev)
{
    if (udev->mon)
        udev_monitor_unref(udev->mon);
    if (udev->udev)
        udev_unref(udev->udev);
    free(udev);
}

char *sdp_udev_wait(sdp_udev *udev, uint16_t vid, uint16_t pid, const char *usb_path, int timeout)
{
    char vid_str[5], pid_str[5];
    sprintf(vid_str, "%04x", vid);
    sprintf(pid_str, "%04x", pid);

    char *result = NULL;
    int ret;
    struct pollfd pollfd = {
        .fd = udev_monitor_get_fd(udev->mon),
        .events = POLLIN,
    };
    while (!result && (ret = poll(&pollfd, 1, timeout)))
    {
        if ((pollfd.revents & POLLIN) == 0)
        {
            printf("poll failed: revents=0x%x\n", pollfd.revents);
            break;
        }
        struct udev_device *dev = udev_monitor_receive_device(udev->mon);
        if (!dev)
            continue;
        struct udev_device *parent = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
        if (!parent)
            goto unref_dev;
        // Use VID/PID from the environment properties instead of sysattr
        // because the latter is not available yet.
        const char *vid_prop = udev_device_get_property_value(parent, "ID_VENDOR_ID");
        if (!vid_prop || strcmp(vid_str, vid_prop))
            goto unref_dev;
        const char *pid_prop = udev_device_get_property_value(parent, "ID_MODEL_ID");
        if (!pid_prop || strcasecmp(pid_str, pid_prop))
            goto unref_dev;
        if (usb_path && strcmp(udev_device_get_sysname(parent), usb_path))
            goto unref_dev;
        const char *devnode = udev_device_get_devnode(dev);
        if (!devnode)
            goto unref_dev;

        // got the device path to our device, return a copy
        result = strdup(devnode);

    unref_dev:
        udev_device_unref(dev);
    }
    return result;
}

bool sdp_udev_matching_usb_path(sdp_udev *udev, const char *device_path, const char *usb_path)
{
    bool result = false;

    const char *sysname = strstr(device_path, "hidraw");
    if (!sysname)
    {
        fprintf(stderr, "ERROR: Device has unexpected path (no hidraw device?): %s\n", device_path);
        goto out;
    }

    struct udev_device *dev = udev_device_new_from_subsystem_sysname(udev->udev, "hidraw", sysname);
    if (!dev)
    {
        fprintf(stderr, "ERROR: Cannot open udev device for %s: %s\n", device_path, strerror(errno));
        goto out;
    }

    struct udev_device *parent = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
    if (!parent)
    {
        fprintf(stderr, "ERROR: Failed to find USB device parent for %s: %s\n", device_path, strerror(errno));
        goto unref_device;
    }

    result = !strcmp(usb_path, udev_device_get_sysname(parent));

unref_device:
    udev_device_unref(dev);
out:
    return result;
}
