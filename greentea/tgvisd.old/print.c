// SPDX-License-Identifier: GPL-2.0
/*
 *  Printing functions
 *
 *  Copyright (C) 2021  Ammar Faizi
 */

#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif

#if defined(__linux__)
	#include <unistd.h>
	#include <pthread.h>
	#include <sys/types.h>
	static pthread_mutex_t get_time_lock = PTHREAD_MUTEX_INITIALIZER;
	static pthread_mutex_t print_lock    = PTHREAD_MUTEX_INITIALIZER;
#else
	#define pthread_mutex_lock(MUTEX)
	#define pthread_mutex_unlock(MUTEX)
	#define pthread_mutex_trylock(MUTEX)
#endif

#include <string.h>
#include <stdarg.h>
#include <tgvisd/print.h>


uint8_t __notice_level = DEFAULT_NOTICE_LEVEL;


static __always_inline char *get_time(char *buf)
{
	size_t len;
	char *time_chr;
	time_t rawtime;
	struct tm *timeinfo;

	pthread_mutex_lock(&get_time_lock);
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	time_chr = asctime(timeinfo);
	len = strnlen(time_chr, 32) - 1;
	memcpy(buf, time_chr, len);
	buf[len] = '\0';
	pthread_mutex_unlock(&get_time_lock);
	return buf;
}


void __attribute__((format(printf, 1, 2))) __pr_notice(const char *fmt, ...)
{
	va_list vl;
	char buf[32];

	va_start(vl, fmt);
	pthread_mutex_lock(&print_lock);
	printf("[%s][T%d] ", get_time(buf), gettid());
	vprintf(fmt, vl);
	putchar('\n');
	pthread_mutex_unlock(&print_lock);
	va_end(vl);
}


void __attribute__((format(printf, 1, 2))) __pr_error(const char *fmt, ...)
{
	va_list vl;
	char buf[32];

	va_start(vl, fmt);
	pthread_mutex_lock(&print_lock);
	printf("[%s][T%d] Error: ", get_time(buf), gettid());
	vprintf(fmt, vl);
	putchar('\n');
	pthread_mutex_unlock(&print_lock);
	va_end(vl);
}


void __attribute__((format(printf, 1, 2)))__pr_emerg(const char *fmt, ...)
{
	va_list vl;
	char buf[32];

	va_start(vl, fmt);
	pthread_mutex_lock(&print_lock);
	printf("[%s][T%d] Emergency: ", get_time(buf), gettid());
	vprintf(fmt, vl);
	putchar('\n');
	pthread_mutex_unlock(&print_lock);
	va_end(vl);
}


void __attribute__((format(printf, 1, 2))) __pr_debug(const char *fmt, ...)
{
	va_list vl;
	char buf[32];

	va_start(vl, fmt);
	pthread_mutex_lock(&print_lock);
	printf("[%s][T%d] Debug: ", get_time(buf), gettid());
	vprintf(fmt, vl);
	putchar('\n');
	pthread_mutex_unlock(&print_lock);
	va_end(vl);
}


void __attribute__((format(printf, 3, 4)))
__panic(const char *file, int lineno, const char *fmt, ...)
{
	va_list vl;
	pthread_mutex_trylock(&print_lock);
	pthread_mutex_trylock(&get_time_lock);
	puts("=======================================================");
	printf("Emergency: Panic - Not syncing: ");
	va_start(vl, fmt);
	vprintf(fmt, vl);
	va_end(vl);
	putchar('\n');
	printf("[T%d][P%d] Panic at %s:%d\n", gettid(), getpid(), file, lineno);
	#define dump_stack()
	/* TODO: Write real dump_stack() */
	dump_stack();
	#undef dump_stack
	puts("=======================================================");
	fflush(stdout);
	abort();
}
