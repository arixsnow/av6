/*
 * Copyright (c) 2026, Arka Mondal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _AV6_KIO_H_
#define _AV6_KIO_H_

#include <arch/stdarg.h>

/*
 * Log levels - lower number = more urgent. Carried per message
 * so a future kernel-log ring buffer / dmesg can filter.
 * WARNING and above also render a short tag after the timestamp.
 */
#define KERN_EMERG   0
#define KERN_ALERT   1
#define KERN_CRIT    2
#define KERN_ERR     3
#define KERN_WARNING 4
#define KERN_NOTICE  5
#define KERN_INFO    6
#define KERN_DEBUG   7
#define KERN_CONT    8

void printk_init(void);
void printk_level(int level, const char *format, ...);
void printk(const char *format, ...); /* shorthand for KERN_INFO */

#define pr_emerg(...)  printk_level(KERN_EMERG, __VA_ARGS__)
#define pr_alert(...)  printk_level(KERN_ALERT, __VA_ARGS__)
#define pr_crit(...)   printk_level(KERN_CRIT, __VA_ARGS__)
#define pr_err(...)    printk_level(KERN_ERR, __VA_ARGS__)
#define pr_warn(...)   printk_level(KERN_WARNING, __VA_ARGS__)
#define pr_notice(...) printk_level(KERN_NOTICE, __VA_ARGS__)
#define pr_info(...)   printk_level(KERN_INFO, __VA_ARGS__)
#define pr_debug(...)  printk_level(KERN_DEBUG, __VA_ARGS__)

void vprintk(int level, const char *format, va_list arglist);

void panic(const char *format, ...) __attribute__((noreturn));
void panic_stop_self(void) __attribute__((noreturn));
int panic_in_progress(void);

/*
 * Set only after the other CPUs have parked. From then on a lock whose holder
 * is parked will never be released, so the lock primitives stop waiting for one
 * and carry on unlocked instead of hanging with the log half written.
 */
extern volatile int panicking;

#endif /* _AV6_KIO_H_ */
