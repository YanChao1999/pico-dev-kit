/**
 * @file monitor.h
 * @brief Interface monitor – periodic health checks and statistics
 *
 * Polls the active interface(s) and reports anomalies (bus errors,
 * timeouts, unexpected NAKs, etc.) to the console.
 */

#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/** Initialise the monitor module. */
void monitor_init(void);

/** Enable periodic monitoring. */
void monitor_start(void);

/** Disable monitoring. */
void monitor_stop(void);

/** Return true while the monitor is running. */
bool monitor_is_active(void);

/**
 * Service the monitor; must be called from the main loop.
 * Runs health checks on the configured interval and logs results.
 */
void monitor_task(void);

/**
 * Record an error event from an interface driver.
 * May be called from interrupt context.
 */
void monitor_record_error(const char *description);

/** Return accumulated error count since last monitor_start(). */
uint32_t monitor_error_count(void);

#endif /* MONITOR_H */
