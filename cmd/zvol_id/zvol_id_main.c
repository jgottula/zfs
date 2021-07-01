/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2011, Fajar A. Nugraha.  All rights reserved.
 * Use is subject to license terms.
 */

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/ioctl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/zfs_znode.h>
#include <sys/fs/zfs.h>


/* set to 1 for ERESTARTSYS retry cope logic; set to 0 for the usual behavior */
#define RETRY_ENABLE 1


#if RETRY_ENABLE

/* not declared in any userspace-public header; swiped from include/linux/errno.h */
#define ERESTARTSYS 512

/* max number of retries for open() calls with ERESTARTSYS outcome before giving up */
#define RETRY_MAX_TRIES 2500

/* microseconds to sleep between each ERESTARTSYS retry */
#define RETRY_SLEEP_USEC 3000

static void
sleep_us(long usec)
{
	if (usec <= 0L)
		return;

	int saved_errno = errno;
	{
		struct timespec t1 = { usec / 1000000L, usec % 1000000L }, t2;
		while (errno = 0, nanosleep(&t1, &t2) != 0) {
			if (errno != EINTR)
				break;
			t1 = t2;
		}
	}
	errno = saved_errno;
}

#endif /* RETRY_ENABLE */

static int
ioctl_get_msg(char *var, int fd)
{
	int ret;
	char msg[ZFS_MAX_DATASET_NAME_LEN];

	ret = ioctl(fd, BLKZNAME, msg);
	if (ret < 0) {
		return (ret);
	}

	snprintf(var, ZFS_MAX_DATASET_NAME_LEN, "%s", msg);
	return (ret);
}

int
main(int argc, char **argv)
{
	int fd = -1, ret = 0, status = EXIT_FAILURE;
	char zvol_name[ZFS_MAX_DATASET_NAME_LEN];
	char *zvol_name_part = NULL;
	char *dev_name;
	struct stat64 statbuf;
	int dev_minor, dev_part;
	int i;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s /dev/zvol_device_node\n", argv[0]);
		goto fail;
	}

	dev_name = argv[1];
	ret = stat64(dev_name, &statbuf);
	if (ret != 0) {
		fprintf(stderr, "Unable to access device file: %s\n", dev_name);
		goto fail;
	}

	dev_minor = minor(statbuf.st_rdev);
	dev_part = dev_minor % ZVOL_MINORS;

#if RETRY_ENABLE
retry:
	int tries = 0;
	while ((fd = open(dev_name, O_RDONLY)) < 0 && errno == ERESTARTSYS) {
		if (++tries < RETRY_MAX_TRIES) {
			fprintf(stderr, "Got ERESTARTSYS on open(%s) try #%d; retrying...", dev_name, tries);
			sleep_us(RETRY_SLEEP_USEC);
		} else {
			fprintf(stderr, "Got ERESTARTSYS on open(%s) try #%d; giving up!", dev_name, tries);
			break;
		}
	}
#else /* RETRY_ENABLE */
	fd = open(dev_name, O_RDONLY);
#endif /* RETRY_ENABLE */
	if (fd < 0) {
		fprintf(stderr, "Unable to open device file: %s\n", dev_name);
		goto fail;
	}

	ret = ioctl_get_msg(zvol_name, fd);
	if (ret < 0) {
		fprintf(stderr, "ioctl_get_msg failed: %s\n", strerror(errno));
		goto fail;
	}
	if (dev_part > 0)
		ret = asprintf(&zvol_name_part, "%s-part%d", zvol_name,
		    dev_part);
	else
		ret = asprintf(&zvol_name_part, "%s", zvol_name);

	if (ret == -1 || zvol_name_part == NULL)
		goto fail;

	for (i = 0; i < strlen(zvol_name_part); i++) {
		if (isblank(zvol_name_part[i]))
			zvol_name_part[i] = '+';
	}

	printf("%s\n", zvol_name_part);
	status = EXIT_SUCCESS;

fail:
	if (zvol_name_part)
		free(zvol_name_part);
	if (fd >= 0)
		close(fd);

	return (status);
}
