#pragma once

#include <stdint.h>
#include <stdlib.h>

typedef struct _DVBTuner DVBTuner;

DVBTuner *dvb_tuner_new(uint8_t adapter_num);
void dvb_tuner_clean(DVBTuner *tuner);
void dvb_tuner_free(DVBTuner *tuner);

int dvb_tuner_tune(DVBTuner *tuner,
                   uint32_t frequency,
                   uint8_t polarization,
                   uint8_t sat_no,
                   uint32_t symbolrate,
                   uint32_t delivery_system,
                   uint32_t modulation,
                   uint32_t roll_off,
                   uint16_t *pids,
                   size_t npids);
void dvb_tuner_add_pid(DVBTuner *tuner, uint16_t pid);

int dvb_tuner_get_fd(DVBTuner *tuner);

float dvb_tuner_get_signal_strength(DVBTuner *tuner);
