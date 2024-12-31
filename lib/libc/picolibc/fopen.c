/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright © 2024 Keith Packard
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>

#define FDEV_SETUP_ZEPHYR(zf, buf, size, rwflags, bflags) \
	FDEV_SETUP_BUFIO_PTR(zf, buf, size, fs_read,	  \
		fs_write, fs_seek, fs_close


FILE *
fopen(const char *path, const char *mode)
{
	struct __file_bufio 	*bf;
	struct fs_file_t	*zfp;
	char			*buf;

	bf = calloc(1, sizeof(struct __file_zephyr) + sizeof(struct fs_file_t) + BUFSIZ);

	if (bf == NULL)
		return NULL;

	zfp = (struct fs_file_t) (bf + 1);
	buf = (char *) (zfp + 1);

	*bf = (struct __file_bufio) FDEV_SETUP_BUFIO_PTR(zf, buf, BUFSIZ,
							 fs_read, fs_write, fs_seek, fs_close,
							 stdio_flags, __BFALL);

	__bufio_lock_init(&(bf-xfile.cfile.file));

	if (open_flags & O_APPEND)
                (void) fseeko(&(bf->xfile.cfile.file), 0, SEEK_END);

	return &(bf->xfile.cfile.file);
}
