#include "dvb-tuner.h"
#include "logging-internal.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/time.h>

#include <unistd.h>
#include <fcntl.h>

#include <sys/ioctl.h>

#include <sys/poll.h>

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

/* http://blog.man7.org/2012/10/how-much-do-builtinexpect-likely-and.html */
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

int _asprintf(char **strp, const char *fmt, ...)
{
    char buffer[64];
    int bytes_needed;
    va_list args;

    va_start(args, fmt);
    bytes_needed = vsnprintf(buffer, 64, fmt, args);
    va_end(args);

    *strp = malloc(bytes_needed + 1);
    if (*strp == NULL)
        return -1;

    if (bytes_needed < 64) {
        strcpy(*strp, buffer);
    }
    else {
        va_start(args, fmt);
        vsnprintf(*strp, bytes_needed + 1, fmt, args);
        va_end(args);
    }

    return bytes_needed;
}

/* TODO: multithreading support (lock filedescriptors, etc.) */

#ifndef DVB_TUNER_DUMMY
struct PIDFilter {
    uint16_t pid;
    int fd;
};

struct PIDFilterList {
    struct PIDFilter filter;
    struct PIDFilterList *next;
};

struct _DVBTuner {
    uint8_t adapter_num;
    /* ?? enum fe_sec_tone_mode (frontend.h @108) */
    DVBTunerConfiguration config;
    uint8_t tone;
    uint8_t inversion;
    uint8_t fec_inner;
    /* ?? service id*/

    int dvr_fd;

    struct dvb_frontend_info frontend_info;
/*    struct dvb_frontend_parameters frontend_parameters;*/
    int frontend_fd;

    struct PIDFilterList *pid_filters;

    DVBRecorderLogger *logger;
};

int dvb_tuner_setup_frontend(DVBTuner *tuner)
{
    char *frontend_dev = NULL;
    if (_asprintf(&frontend_dev, "/dev/dvb/adapter%u/frontend0", tuner->adapter_num) < 0) {
        LOG(tuner->logger, "Failed to get frontend dev string.\n");
        return -1;
    }
    if ((tuner->frontend_fd = open(frontend_dev, O_CLOEXEC | O_RDWR)) < 0) {
        LOG(tuner->logger, "Failed to open frontend device %s.\n", frontend_dev);
        free(frontend_dev);
        return -1;
    }
    free(frontend_dev);

    LOG(tuner->logger, "dvb_tuner_new: frontend_fd: %d\n", tuner->frontend_fd);

    if ((ioctl(tuner->frontend_fd, FE_GET_INFO, &tuner->frontend_info)) < 0) {
        LOG(tuner->logger, "Failed to get frontend info.\n");
        return -1;
    }

    if (tuner->frontend_info.type != FE_QPSK) {
        LOG(tuner->logger, "Adapter %u does not support DVB-S(2).\n", tuner->adapter_num);
        return -1;
    }

    fcntl(tuner->frontend_fd, F_SETFL, O_NONBLOCK);

    return 0;
}

DVBTuner *dvb_tuner_new(uint8_t adapter_num)
{
    DVBTuner *tuner = malloc(sizeof(DVBTuner));
    if (tuner == NULL) {
        LOG(tuner->logger, "Failed to allocate memory.\n");
        return NULL;
    }
    memset(tuner, 0, sizeof(DVBTuner));
    tuner->frontend_fd = -1;
    tuner->dvr_fd = -1;

    tuner->adapter_num = adapter_num;

    return tuner;
}

void dvb_tuner_clean(DVBTuner *tuner)
{
    if (!tuner)
        return;
#define CLOSE_FD(fd) do {\
    if (tuner->fd >= 0) {\
        close(tuner->fd);\
        tuner->fd = -1;\
    }\
} while (0)

    LOG(tuner->logger, "dvb_tuner_clean\n");

    struct PIDFilterList *tmp;
    while (tuner->pid_filters) {
        tmp = tuner->pid_filters->next;

        ioctl(tuner->pid_filters->filter.fd, DMX_STOP);
        close(tuner->pid_filters->filter.fd);

        free(tuner->pid_filters);

        tuner->pid_filters = tmp;
    }

    CLOSE_FD(dvr_fd);
    CLOSE_FD(frontend_fd);

#undef CLOSE_FD
}

void dvb_tuner_free(DVBTuner *tuner)
{
    if (tuner) {
        dvb_tuner_clean(tuner);
        free(tuner);
    }
}

void dvb_tuner_set_logger(DVBTuner *tuner, DVBRecorderLogger *logger)
{
    if (tuner) {
        tuner->logger = logger;
    }
}

static int dvb_tuner_set_disecq(DVBTuner *tuner)
{
    LOG(tuner->logger, "dvb_tuner_set_disecq\n");
    /* http://www.eutelsat.com/files/live/sites/eutelsatv2/files/contributed/satellites/pdf/Diseqc/Reference%20docs/bus_spec.pdf */
    struct dvb_diseqc_master_cmd cmd =
    {
        {
            0xe0,          /* Framing byte: Run-in, Command from master, no reply required, first in this transmission */
            0x10,          /* Address byte: any LNB, Switcher, or SMATV (Master to all …) */
            0x38,          /* Write to port group 0 (sat_no|sat_no|pol|tone) */
            0xf0,          /* clear all four bits */
            0x00,
            0x00
        },
        4                  /* length of data */
    };

    const struct timespec slp = {
        .tv_sec = 0,
        .tv_nsec = 15e6
    };

    cmd.msg[3] = 0xf0 | ((tuner->config.sat_no << 2) & 0x0f)
                      | ((tuner->config.polarization ? 1 : 0) << 1)
                      | (tuner->tone ? 1 : 0);

    LOG(tuner->logger, "dvb_tuner_set_disecq frontend_fd: %d\n", tuner->frontend_fd);
    LOG(tuner->logger, "FE_SET_TONE\n");
    if (ioctl(tuner->frontend_fd, FE_SET_TONE, SEC_TONE_OFF) < 0) {
        LOG(tuner->logger, "FE_SET_TONE failed: (%d) %s\n", errno, strerror(errno));
        return -1;
    }

    LOG(tuner->logger, "FE_SET_VOLTAGE\n");
    if (ioctl(tuner->frontend_fd, FE_SET_VOLTAGE,
                tuner->config.polarization ? SEC_VOLTAGE_18 : SEC_VOLTAGE_13) < 0) {
        LOG(tuner->logger, "FE_SET_VOLTAGE failed: (%d) %s\n", errno, strerror(errno));
        return -1;
    }

/*    usleep(15000);*/
    nanosleep(&slp, NULL);

    LOG(tuner->logger, "FE_DISEQC_SEND_MASTER_CMD\n");
    if (ioctl(tuner->frontend_fd, FE_DISEQC_SEND_MASTER_CMD, &cmd) < 0) {
        LOG(tuner->logger, "FE_DISEQC_SEND_MASTER_CMD failed: (%d) %s\n", errno, strerror(errno));
        return -1;
    }

/*    usleep(15000);*/
    nanosleep(&slp, NULL);

    LOG(tuner->logger, "FE_DISEQC_SEND_BURST\n");
    if (ioctl(tuner->frontend_fd, FE_DISEQC_SEND_BURST,
                (tuner->config.sat_no >> 2) & 0x01 ? SEC_MINI_B : SEC_MINI_A) < 0) {
        LOG(tuner->logger, "FE_DISEQC_SEND_BURST failed: (%d) %s\n", errno, strerror(errno));
        return -1;
    }

/*    usleep(15000);*/
    nanosleep(&slp, NULL);

    LOG(tuner->logger, "FE_SET_TONE\n");
    if (ioctl(tuner->frontend_fd, FE_SET_TONE,
                tuner->tone ? SEC_TONE_ON : SEC_TONE_OFF) < 0) {
        LOG(tuner->logger, "FE_SET_TONE failed: (%d) %s\n", errno, strerror(errno));
        return -1;
    }

    LOG(tuner->logger, "set_disecq successful\n");
    return 0;
}

static int dvb_tuner_do_tune(DVBTuner *tuner)
{
    LOG(tuner->logger, "dvb_tuner_do_tune\n");
    struct dvb_frontend_event event;
    struct pollfd pfd[1];
    int rc;

    /* discard stale events */
    while (ioctl(tuner->frontend_fd, FE_GET_EVENT, &event) != -1);

/*    LOG(tuner->logger, "FE_SET_FRONTEND\n");
    if (ioctl(tuner->frontend_fd, FE_SET_FRONTEND, &tuner->frontend_parameters) < 0) {
        LOG(tuner->logger, "FE_SET_FRONTEND failed: (%d) %s\n", errno, strerror(errno));
        return -1;
    }

    LOG(tuner->logger, "FE_SET_FRONTEND successful\n");*/
    struct dtv_property p[] = {
        { .cmd = DTV_DELIVERY_SYSTEM, .u.data = tuner->config.delivery_system ? SYS_DVBS2 : SYS_DVBS },
        { .cmd = DTV_FREQUENCY,       .u.data = tuner->config.frequency },
        { .cmd = DTV_MODULATION,      .u.data = tuner->config.modulation },
        { .cmd = DTV_SYMBOL_RATE,     .u.data = tuner->config.symbolrate },
        { .cmd = DTV_INNER_FEC,       .u.data = tuner->fec_inner },
        { .cmd = DTV_INVERSION,       .u.data = tuner->inversion },
        { .cmd = DTV_ROLLOFF,         .u.data = tuner->config.roll_off },
        { .cmd = DTV_PILOT,           .u.data = PILOT_AUTO },
        { .cmd = DTV_TUNE },
    };
    struct dtv_properties cmdseq = {
        .num = 9,
        .props = p
    };

    if ((ioctl(tuner->frontend_fd, FE_SET_PROPERTY, &cmdseq)) == -1) {
        LOG(tuner->logger, "FE_SET_PROPERTY failed: (%d) %s\n", errno, strerror(errno));
        return -1;
    }

    pfd[0].fd = tuner->frontend_fd;
    pfd[0].events = POLLIN;

    if (poll(pfd, 1, 3000)) {
        if (pfd[0].revents & POLLIN) {
            rc = ioctl(tuner->frontend_fd, FE_GET_EVENT, &event);
#ifdef EOVERFLOW
            if (rc == -EOVERFLOW)
                LOG(tuner->logger, "EOVERFLOW\n");
#else
            if (rc == -EINVAL)
                LOG(tuner->logger, "EINVAL\n");
#endif
            if (event.parameters.frequency <= 0)
                return -1;
        }
    }

    /* xine: timeout */
    struct timeval time_now;
    struct timeval time_timeout;

    const struct timespec slp = {
        .tv_sec = 0,
        .tv_nsec = 1e7
    };

    gettimeofday(&time_timeout, NULL);
    time_timeout.tv_sec += 5; /* five seconds timeout */

    fe_status_t status;
    do {
        status = 0;
        if (ioctl(tuner->frontend_fd, FE_READ_STATUS, &status) < 0) {
            LOG(tuner->logger, "FE_READ_STATUS failed: (%d) %s\n", errno, strerror(errno));
            return -1;
        }

        if (status & FE_HAS_LOCK) {
            LOG(tuner->logger, "FE_HAS_LOCK\n");
            break;
        }

        /* cannot get lock if there is no signal -> warn user */
        gettimeofday(&time_now, NULL);
        if (time_now.tv_sec > time_timeout.tv_sec) {
            LOG(tuner->logger, "FE_TIMEDOUT\n");
            return -1;
        }

        /*usleep(10000);*/
        LOG(tuner->logger, "no lock or timeout\n");
        nanosleep(&slp, NULL);
    } while (!(status & FE_TIMEDOUT));

    /* xine: read tuner status (log only) */

    if (status & FE_HAS_LOCK && !(status & FE_TIMEDOUT)) {
        LOG(tuner->logger, "do_tune successful\n");
        return 0;
    }
    else {
        return -1;
    }
}

int dvb_tuner_tune(DVBTuner *tuner,
                   DVBTunerConfiguration *config,
                   uint16_t *pids,
                   size_t npids) /* + service id -> just for filter/epg/eit ? */
{
    if (tuner == NULL || config == NULL)
        return -1;

    /* close open file descriptors */
    dvb_tuner_clean(tuner);

    if (dvb_tuner_setup_frontend(tuner) < 0)
        return -1;

    tuner->config = *config;

    LOG(tuner->logger,
        "dvb_tuner_tune: frequency/symbolrate/pol: %" PRIu32 "/%" PRIu32 ", %u\n",
        tuner->config.frequency, tuner->config.symbolrate, tuner->config.polarization);

    while (tuner->config.frequency < 1000000) {
        tuner->config.frequency *= 1000;
    }
    while (tuner->config.symbolrate < 1000000) {
        tuner->config.symbolrate *= 1000;
    }

    LOG(tuner->logger,
        "dvb_tuner_tune: frequency/symbolrate: %" PRIu32 "/%" PRIu32 "\n",
        tuner->config.frequency, tuner->config.symbolrate);

    /* lnb switch frequency (hi band/lo band)*/
    if (tuner->config.frequency > 11700000) {
        tuner->config.frequency = tuner->config.frequency - 10600000; /* lnb frequency hi */
        tuner->tone = 1;
    }
    else {
        tuner->config.frequency = tuner->config.frequency - 9750000;  /* lnb frequency lo */
        tuner->tone = 0;
    }

    tuner->inversion = INVERSION_AUTO;
    tuner->fec_inner = FEC_AUTO;
    switch (tuner->config.modulation) {
        case 5: tuner->config.modulation = PSK_8; break;
        case 6: tuner->config.modulation = APSK_16; break;
        case 7: tuner->config.modulation = APSK_32; break;
        case 2:
        default:
                tuner->config.modulation = QPSK; break;
    }
    switch (tuner->config.roll_off) {
        case 20: tuner->config.roll_off = ROLLOFF_20; break;
        case 25: tuner->config.roll_off = ROLLOFF_25; break;
        case 0: tuner->config.roll_off = ROLLOFF_AUTO; break;
        default:
                tuner->config.roll_off = ROLLOFF_35;
    }

    /* actually tune, setup dvr_fd */
    /* == set_channel
     * + set diseqc
     * + tune_it */
    if (!(tuner->frontend_info.caps & FE_CAN_INVERSION_AUTO))
        tuner->inversion = INVERSION_OFF;

    if (dvb_tuner_set_disecq(tuner) < 0)
        return -1;

    if (dvb_tuner_do_tune(tuner) < 0)
        return -1;

    /* see: http://www.linuxtv.org/docs/dvbapi/DVB_Demux_Device.html */
    size_t j;
    for (j = 0; j < npids; ++j) {
        dvb_tuner_add_pid(tuner, pids[j]);
    }

    char *dvr_device = NULL;
    if (_asprintf(&dvr_device, "/dev/dvb/adapter%u/dvr0", tuner->adapter_num) < 0) {
        return -1;
    }
    tuner->dvr_fd = open(dvr_device, O_CLOEXEC | O_RDONLY | O_NONBLOCK);
    free(dvr_device);

    if (tuner->dvr_fd < 0) {
        LOG(tuner->logger, "failed to open dvr_device: (%d) %s\n", errno, strerror(errno));
        return -1;
    }

    LOG(tuner->logger, "tune successful\n");
    return 0;
}

void dvb_tuner_add_pid(DVBTuner *tuner, uint16_t pid)
{
    if (tuner == NULL)
        return;

    struct PIDFilterList *filter = malloc(sizeof(struct PIDFilterList));
    if (filter == NULL)
        return;

    filter->next = tuner->pid_filters;
    filter->filter.pid = pid;

    char *demux_device = NULL;
    if (_asprintf(&demux_device, "/dev/dvb/adapter%u/demux0", tuner->adapter_num) < 0) {
        goto err;
    }

    filter->filter.fd = open(demux_device, O_CLOEXEC | O_RDWR | O_NONBLOCK);
    free(demux_device);
    demux_device = NULL;

    struct dmx_pes_filter_params params;
    params.pid = pid;
    params.input = DMX_IN_FRONTEND;
    params.output = DMX_OUT_TS_TAP;
    params.pes_type = DMX_PES_OTHER; /* AUDIO/VIDIO/SUBTITLE/TELETEXT/PCR */
    params.flags = DMX_IMMEDIATE_START;
    if (ioctl(filter->filter.fd, DMX_SET_PES_FILTER, &params) < 0) {
        LOG(tuner->logger, "Error setting up filter for pid %u.\n", pid);
        goto err;
    }

/*    if (ioctl(filter->filter.fd, DMX_SET_BUFFER_SIZE, 8 * 4096) < 0) {
        LOG(tuner->logger, "Error setting buffer size for pid %u.\n", pid);
    }*/

    tuner->pid_filters = filter;

    LOG(tuner->logger, "Added pid %u\n", pid);
    return;

err:
    if (demux_device)
        free(demux_device);
    free(filter);
}

int dvb_tuner_get_fd(DVBTuner *tuner)
{
    if (tuner)
        return tuner->dvr_fd;
    return -1;
}

float dvb_tuner_get_signal_strength(DVBTuner *tuner)
{
    if (!tuner || tuner->frontend_fd < 0)
        return -1.0f;
    uint16_t strength = -1;;
    if (ioctl(tuner->frontend_fd, FE_READ_SIGNAL_STRENGTH, &strength) < 0) {
        return -1.0f;
    }
    LOG(tuner->logger, "signal strength: %u\n", strength);
    return (float)(strength/65535.0f);
}

#else /* DVB_TUNER_DUMMY */
struct _DVBTuner {
    int fd;

    DVBRecorderLogger *logger;
};

DVBTuner *dvb_tuner_new(uint8_t adapter_num)
{
    DVBTuner *tuner = malloc(sizeof(DVBTuner));
    if (unlikely(tuner == NULL)) {
        LOG(tuner->logger, "Failed to allocate memory.\n");
        return NULL;
    }
    memset(tuner, 0, sizeof(DVBTuner));
    tuner->fd = -1;

    return tuner;
}

void dvb_tuner_clean(DVBTuner *tuner)
{
    if (tuner) {
        LOG(tuner->logger, "dvb_tuner_clean tuner->fd: %d\n", tuner->fd);
        if (tuner->fd >= 0) {
            close(tuner->fd);
            tuner->fd = -1;
        }
    }
}

void dvb_tuner_free(DVBTuner *tuner)
{
    if (tuner) {
        dvb_tuner_clean(tuner);
        free(tuner);
    }
}

void dvb_tuner_set_logger(DVBTuner *tuner, DVBRecorderLogger *logger)
{
    if (tuner) {
        tuner->logger = logger;
    }
}

int dvb_tuner_tune(DVBTuner *tuner,
                   DVBTunerConfiguration *config,
                   uint16_t *pids,
                   size_t npids)
{
    if (tuner == NULL)
        return -1;
    LOG(tuner->logger, "dvb_tuner_tune tuner->fd: %d\n", tuner->fd);
    if (tuner->fd != -1)
        close(tuner->fd);
    tuner->fd = open("/tmp/ts-dummy.ts", O_CLOEXEC | O_RDONLY | O_NONBLOCK);

    LOG(tuner->logger, "[tuner] tuner->fd: %d\n", tuner->fd);

    if (tuner->fd == -1)
        return -1;
    return 0;
}

void dvb_tuner_add_pid(DVBTuner *tuner, uint16_t pid)
{
    LOG(tuner->logger, "[Tuner dummy] Add pid %u\n", pid);
}

int dvb_tuner_get_fd(DVBTuner *tuner)
{
    if (tuner == NULL)
        return -1;
    return tuner->fd;
}

float dvb_tuner_get_signal_strength(DVBTuner *tuner)
{
    return -1.0f;
}

#endif
