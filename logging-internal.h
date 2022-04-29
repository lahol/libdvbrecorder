#pragma once

#include "logging.h"

#ifdef DEBUG
#define DLOG(logger, fmt, ...) dvb_recorder_log((logger), "[libdvbrecorder] *DEBUG* " fmt, ##__VA_ARGS__)
#else
#define DLOG(logger, fmt, ...)
#endif

#ifdef FDEBUG
#define FLOG(fmt, ...) fprintf(stderr, "%s " fmt, __func__, ##__VA_ARGS__)
#else
#define FLOG(fmt, ...)
#endif

#define LOG(logger, fmt, ...) dvb_recorder_log((logger), "[libdvbrecorder] " __FILE__ ":%d " fmt, __LINE__,  ##__VA_ARGS__)
