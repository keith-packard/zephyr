/*
 * Copyright © 2021, Keith Packard <keithp@keithp.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/errno_private.h>
#include <zephyr/sys/libc-hooks.h>
#include <zephyr/syscall_handler.h>
#include <zephyr/app_memory/app_memdomain.h>
#include <zephyr/init.h>
#include <zephyr/sys/sem.h>
#include <zephyr/logging/log.h>

#define LIBC_BSS	K_APP_BMEM(z_libc_partition)
#define LIBC_DATA	K_APP_DMEM(z_libc_partition)

#ifdef CONFIG_MMU
#ifdef CONFIG_USERSPACE
struct kmem_partition z_malloc_partition;
#endif

LIBC_BSS static unsigned char *heap_base;
LIBC_BSS static size_t max_heap_size;

#define HEAP_BASE		heap_base
#define MAX_HEAP_SIZE		max_heap_size
#define USE_MALLOC_PREPARE	1

#elif CONFIG_PICOLIBC_ALIGNED_HEAP_SIZE
K_APPMEM_PARTITION_DEFINE(z_malloc_partition);
#define MALLOC_BSS	K_APP_BMEM(z_malloc_partition)

/* Compiler will throw an error if the provided value isn't a power of two */
MALLOC_BSS static unsigned char __aligned(CONFIG_PICOLIBC_ALIGNED_HEAP_SIZE)
	heap_base[CONFIG_PICOLIBC_ALIGNED_HEAP_SIZE];

#define HEAP_BASE	((uintptr_t) heap_base)
#define MAX_HEAP_SIZE	CONFIG_PICOLIBC_ALIGNED_HEAP_SIZE

#else /* Not MMU or CONFIG_PICOLIBC_ALIGNED_HEAP_SIZE */
/* Heap base and size are determined based on the available unused SRAM,
 * in the interval from a properly aligned address after the linker symbol
 * `_end`, to the end of SRAM
 */
#define USED_RAM_END_ADDR   POINTER_TO_UINT(&_end)

#ifdef Z_MALLOC_PARTITION_EXISTS
/* Need to be able to program a memory protection region from HEAP_BASE
 * to the end of RAM so that user threads can get at it.
 * Implies that the base address needs to be suitably aligned since the
 * bounds have to go in a k_mem_partition.
 */
struct k_mem_partition z_malloc_partition;
/* TODO: Need a generic Kconfig for the MPU region granularity */
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
#define HEAP_BASE	ROUND_UP(USED_RAM_END_ADDR, \
				 CONFIG_ARM_MPU_REGION_MIN_ALIGN_AND_SIZE)
#elif defined(CONFIG_ARC)
#define HEAP_BASE	ROUND_UP(USED_RAM_END_ADDR, Z_ARC_MPU_ALIGN)
#elif defined(CONFIG_RISCV)
#define HEAP_BASE	ROUND_UP(USED_RAM_END_ADDR, Z_RISCV_STACK_GUARD_SIZE)
#else
#error "Unsupported platform"
#endif /* CONFIG_<arch> */
#else /* !Z_MALLOC_PARTITION_EXISTS */
/* No partition, heap can just start wherever _end is */
#define HEAP_BASE	USED_RAM_END_ADDR
#endif /* Z_MALLOC_PARTITION_EXISTS */

#ifdef CONFIG_XTENSA
extern void *_heap_sentry;
#define MAX_HEAP_SIZE  (POINTER_TO_UINT(&_heap_sentry) - HEAP_BASE)
#else
#define MAX_HEAP_SIZE	(KB(CONFIG_SRAM_SIZE) - \
			 (HEAP_BASE - CONFIG_SRAM_BASE_ADDRESS))
#endif

#endif /* CONFIG_PICOLIBC_ALIGNED_HEAP_SIZE */


static int malloc_prepare(const struct device *unused)
{
	ARG_UNUSED(unused);

#ifdef CONFIG_MMU
	max_heap_size = MIN(CONFIG_PICOLIBC_LIBC_MAX_MAPPED_REGION_SIZE,
			    k_mem_free_get());

	if (max_heap_size != 0) {
		heap_base = k_mem_map(max_heap_size, K_MEM_PERM_RW);
		__ASSERT(heap_base != NULL,
			 "failed to allocate heap of size %zu", max_heap_size);

	}
#endif

#if Z_MALLOC_PARTITION_EXISTS
	z_malloc_partition.start = HEAP_BASE;
	z_malloc_partition.size = MAX_HEAP_SIZE;
	z_malloc_partition.attr = K_MEM_PARTITION_P_RW_U_RW;
#endif
	return 0;
}

SYS_INIT(malloc_prepare, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

LIBC_BSS static unsigned int heap_sz;

static int (*_stdout_hook)(int);

int z_impl_zephyr_fputc(int a, FILE *out)
{
	(*_stdout_hook)(a);
	return 0;
}

#ifdef CONFIG_USERSPACE
static inline int z_vrfy_zephyr_fputc(int c, FILE *stream)
{
	return z_impl_zephyr_fputc(c, stream);
}
#include <syscalls/zephyr_fputc_mrsh.c>
#endif

static int picolibc_put(char a, FILE *f)
{
	zephyr_fputc(a, f);
	return 0;
}

static FILE __stdout = FDEV_SETUP_STREAM(picolibc_put, NULL, NULL, 0);
static FILE __stdin = FDEV_SETUP_STREAM(NULL, NULL, NULL, 0);

#ifdef __strong_reference
#define STDIO_ALIAS(x) __strong_reference(stdout, x);
#else
#define STDIO_ALIAS(x) FILE *const x = &__stdout;
#endif

FILE *const stdin = &__stdin;
FILE *const stdout = &__stdout;
STDIO_ALIAS(stderr);

void __stdout_hook_install(int (*hook)(int))
{
	_stdout_hook = hook;
	__stdout.flags |= _FDEV_SETUP_WRITE;
}

void __stdin_hook_install(unsigned char (*hook)(void))
{
	__stdin.get = (int (*)(FILE *)) hook;
	__stdin.flags |= _FDEV_SETUP_READ;
}

int z_impl_zephyr_read_stdin(char *buf, int nbytes)
{
	int i = 0;

	for (i = 0; i < nbytes; i++) {
		*(buf + i) = getchar();
		if ((*(buf + i) == '\n') || (*(buf + i) == '\r')) {
			i++;
			break;
		}
	}
	return i;
}

int z_impl_zephyr_write_stdout(const void *buffer, int nbytes)
{
	const char *buf = buffer;
	int i;

	for (i = 0; i < nbytes; i++) {
		if (*(buf + i) == '\n') {
			putchar('\r');
		}
		putchar(*(buf + i));
	}
	return nbytes;
}

#ifdef CONFIG_PRINTK

#if defined(CONFIG_PRINTK_SYNC)
static struct k_spinlock lock;
#endif

static int
k_chr_out(char c, FILE *file)
{
	(void) file;
	k_str_out(&c, 1);
	return (int) (unsigned char) c;
}

__attribute__((weak)) int arch_printk_char_out(int c)
{
	(void) c;
	return 0;
}

static FILE __console = FDEV_SETUP_STREAM(
	(int(*)(char, FILE *)) arch_printk_char_out,
	NULL, NULL, _FDEV_SETUP_WRITE);

static FILE __k_out = FDEV_SETUP_STREAM(
	k_chr_out,
	NULL, NULL, _FDEV_SETUP_WRITE);

#ifdef CONFIG_USERSPACE
static inline void z_vrfy_k_str_out(char *c, size_t n)
{
	Z_OOPS(Z_SYSCALL_MEMORY_READ(c, n));
	z_impl_k_str_out((char *)c, n);
}
#include <syscalls/k_str_out_mrsh.c>
#endif /* CONFIG_USERSPACE */

void __printk_hook_install(int (*fn)(int))
{
	__console.put = (int(*)(char, FILE *)) fn;
	__console.flags |= _FDEV_SETUP_WRITE;
}

void *__printk_get_hook(void)
{
	return __console.put;
}

void vprintk(const char *fmt, va_list ap)
{
	if (IS_ENABLED(CONFIG_LOG_PRINTK)) {
		z_log_vprintk(fmt, ap);
		return;
	}

	if (k_is_user_context()) {
		vfprintf(&__k_out, fmt, ap);
	} else {
#ifdef CONFIG_PRINTK_SYNC
		k_spinlock_key_t key = k_spin_lock(&lock);
#endif

		vfprintf(&__console, fmt, ap);

#ifdef CONFIG_PRINTK_SYNC
		k_spin_unlock(&lock, key);
#endif
	}
}

void printk(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
}

void z_impl_k_str_out(char *c, size_t n)
{
#ifdef CONFIG_PRINTK_SYNC
	k_spinlock_key_t key = k_spin_lock(&lock);
#endif

	(void) fwrite(c, 1, n, &__console);

#ifdef CONFIG_PRINTK_SYNC
	k_spin_unlock(&lock, key);
#endif
}

#endif

#ifndef CONFIG_POSIX_API
int _read(int fd, char *buf, int nbytes)
{
	ARG_UNUSED(fd);

	return z_impl_zephyr_read_stdin(buf, nbytes);
}
__weak FUNC_ALIAS(_read, read, int);

int _write(int fd, const void *buf, int nbytes)
{
	ARG_UNUSED(fd);

	return z_impl_zephyr_write_stdout(buf, nbytes);
}
__weak FUNC_ALIAS(_write, write, int);

int _open(const char *name, int mode)
{
	return -1;
}
__weak FUNC_ALIAS(_open, open, int);

int _close(int file)
{
	return -1;
}
__weak FUNC_ALIAS(_close, close, int);

int _lseek(int file, int ptr, int dir)
{
	return 0;
}
__weak FUNC_ALIAS(_lseek, lseek, int);
#else
extern ssize_t write(int file, const char *buffer, size_t count);
#define _write	write
#endif

int _isatty(int file)
{
	return 1;
}
__weak FUNC_ALIAS(_isatty, isatty, int);

int _kill(int i, int j)
{
	return 0;
}
__weak FUNC_ALIAS(_kill, kill, int);

int _getpid(void)
{
	return 0;
}
__weak FUNC_ALIAS(_getpid, getpid, int);

int _fstat(int file, struct stat *st)
{
	st->st_mode = S_IFCHR;
	return 0;
}
__weak FUNC_ALIAS(_fstat, fstat, int);

__weak void _exit(int status)
{
	_write(1, "exit\n", 5);
	while (1) {
		;
	}
}

static LIBC_DATA SYS_SEM_DEFINE(heap_sem, 1, 1);

void *_sbrk(intptr_t count)
{
	void *ret, *ptr;

	sys_sem_take(&heap_sem, K_FOREVER);

#if CONFIG_PICOLIBC_ALIGNED_HEAP_SIZE
	ptr = heap_base + heap_sz;
#else
	ptr = ((char *)HEAP_BASE) + heap_sz;
#endif

	if ((heap_sz + count) < MAX_HEAP_SIZE) {
		heap_sz += count;
		ret = ptr;
	} else {
		ret = (void *)-1;
	}

	/* coverity[CHECKED_RETURN] */
	sys_sem_give(&heap_sem);

	return ret;
}
__weak FUNC_ALIAS(_sbrk, sbrk, void *);

/* This function gets called if static buffer overflow detection is enabled on
 * stdlib side (Picolibc here), in case such an overflow is detected. Picolibc
 * provides an implementation not suitable for us, so we override it here.
 */
__weak FUNC_NORETURN void __chk_fail(void)
{
	static const char chk_fail_msg[] = "* buffer overflow detected *\n";

	_write(2, chk_fail_msg, sizeof(chk_fail_msg) - 1);
	z_except_reason(K_ERR_STACK_CHK_FAIL);
	CODE_UNREACHABLE;
}

int _gettimeofday(struct timeval *__tp, void *__tzp)
{
	ARG_UNUSED(__tp);
	ARG_UNUSED(__tzp);

	return -1;
}

#include <stdlib.h>
#include <zephyr.h>

/* Replace picolibc abort with native Zephyr one */
void abort(void)
{
	printk("%s\n", __func__);
	k_panic();
	__builtin_unreachable();
}
