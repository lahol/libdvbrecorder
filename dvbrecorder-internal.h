#pragma once

#include "dvbrecorder.h"
#include "scheduled.h"

void dvb_recorder_set_next_scheduled_event(DVBRecorder *recorder, ScheduledEvent *event);
ScheduledEvent *dvb_recorder_get_next_scheduled_event(DVBRecorder *recorder);

void dvb_recorder_find_next_scheduled_event(DVBRecorder *recorder);
