#pragma once

#include <stdint.h>
#include <stdlib.h>
#include "logging.h"

typedef struct _DVBTuner DVBTuner;

DVBTuner *dvb_tuner_new(uint8_t adapter_num);
void dvb_tuner_clean(DVBTuner *tuner);
void dvb_tuner_free(DVBTuner *tuner);

void dvb_tuner_set_logger(DVBTuner *tuner, DVBRecorderLogger *logger);

typedef struct _DVBTunerConfiguration {
    uint32_t frequency;
    uint32_t symbolrate;
    uint8_t polarization;
    uint8_t sat_no;
    uint8_t delivery_system;
    uint8_t modulation;
    uint8_t roll_off;
} DVBTunerConfiguration;

int dvb_tuner_tune(DVBTuner *tuner,
                   DVBTunerConfiguration *config,
                   uint16_t *pids,
                   size_t npids);
/* Stop the tuner and close all file descriptors, including frontend. */
void dvb_tuner_stop(DVBTuner *tuner);
void dvb_tuner_add_pid(DVBTuner *tuner, uint16_t pid);

int dvb_tuner_get_fd(DVBTuner *tuner);

float dvb_tuner_get_signal_strength(DVBTuner *tuner);
