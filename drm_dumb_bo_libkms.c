#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <error.h>
#include <errno.h>

#include <xf86drmMode.h>
#include <xf86drm.h>
#include <libkms.h>

#include "drm_utils.h"

/* */

static const char device_name[] = "/dev/dri/card0";

/* */

int main(char argc, char *argv[])
{
	struct kms_driver *drv;
	struct kms_bo *bo;

	struct kms_display kms_data;

	uint32_t fb, stride, handle;
	uint32_t *dst;
	int ret, fd;

    drmModeCrtcPtr saved_crtc;

	uint32_t attr[] = {
		KMS_WIDTH, 0,
		KMS_HEIGHT, 0,
		KMS_BO_TYPE, KMS_BO_TYPE_SCANOUT_X8R8G8B8,
		KMS_TERMINATE_PROP_LIST, 0
	};

	fd = open(device_name, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("cannot open drm device");
		exit(-1);
	}

	/* find current DRM configuration */

	if (!find_drm_configuration(fd, &kms_data)) {
		fprintf(stderr, "failed to setup KMS\n");
		ret = -EFAULT;
		goto err_close;
	}

	/* set display dimensions for chosen configuration */

	attr[1] = kms_data.mode.hdisplay;
	attr[3] = kms_data.mode.vdisplay;

	/* create kms driver */

	ret = kms_create(fd, &drv);
	if (ret) {
		fprintf(stderr, "failed to setup kms driver\n");
		goto err_close;
	}

	/* create dumb buffer object */

	ret = kms_bo_create(drv, attr, &bo);
	if (ret) {
		fprintf(stderr, "failed to create dumb bo\n");
		goto err_driver_destroy;
	}

	ret = kms_bo_get_prop(bo, KMS_PITCH, &stride);
	if (ret) {
		fprintf(stderr, "failed to get attrs of dumb bo\n");
		goto err_buffer_destroy;
	}

	ret = kms_bo_get_prop(bo, KMS_HANDLE, &handle);
	if (ret) {
		fprintf(stderr, "failed to get attrs of dumb bo\n");
		goto err_buffer_destroy;
	}

	/* map dumb buffer object */

	ret = kms_bo_map(bo, (void **) &dst);
	if (ret) {
		fprintf(stderr, "failed to map buffer object\n");
		goto err_buffer_destroy;
	}

	/* create drm framebuffer */

	ret = drmModeAddFB(fd, kms_data.mode.hdisplay, kms_data.mode.vdisplay, 24, 32, stride, handle, &fb);
	if (ret) {
		fprintf(stderr, "cannot add drm framebuffer for dumb buffer object\n");
		goto err_buffer_unmap;
	}

	/* store current crtc */

    saved_crtc = drmModeGetCrtc(fd, kms_data.encoder->crtc_id);
    if (saved_crtc == NULL) {
        fprintf(stderr, "failed to get current mode\n");
        goto err_buffer_unmap;
    }

	/* setup new crtc */

    ret = drmModeSetCrtc(fd, kms_data.encoder->crtc_id, fb, 0, 0,
            &kms_data.connector->connector_id, 1, &kms_data.mode);
	if (ret) {
		fprintf(stderr, "cannot set new drm crtc");
		goto err_buffer_unmap;
	}

	/* draw on the screen */

	do {

		uint32_t color32[] = {
			0x000000FF, 0x0000FF00, 0x00FF0000,
			0x00FF00FF, 0x00FFFF00, 0x0000FFFF,
		};

		uint32_t h = kms_data.mode.vdisplay;
		uint32_t w = kms_data.mode.hdisplay;
		uint32_t color;
		int i, j;

		for(i = 0; i < h; i++ ){

			color = color32[6*i/h];

			for(j = 0; j < w; j++) {
				*(dst + i*w + j) = color;
			}
		}

		getchar();

	} while(0);

    ret = drmModeSetCrtc(fd, saved_crtc->crtc_id, saved_crtc->buffer_id,
            saved_crtc->x, saved_crtc->y, &kms_data.connector->connector_id, 1, &saved_crtc->mode);

err_fb:
    drmModeRmFB(fd, fb);

err_buffer_unmap:
	ret = kms_bo_unmap(bo);
	if (ret) {
		fprintf(stderr, "cannot unmap dumb buffer");
	}

err_buffer_destroy:
	ret = kms_bo_destroy(&bo);
	if (ret) {
		fprintf(stderr, "cannot destroy dumb buffer");
	}

err_driver_destroy:
	ret = kms_destroy(&drv);
	if (ret) {
		fprintf(stderr, "cannot destroy kms driver");
	}

err_close:
	close(fd);

	return ret;
}
