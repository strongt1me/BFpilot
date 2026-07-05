/*
 * BFpilot - archive extraction entry points.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int bfpilot_archive_run_prepared_job(void);
int bfpilot_archive_start_daemon(void);
void bfpilot_archive_stop_daemon(void);

#ifdef __cplusplus
}
#endif
