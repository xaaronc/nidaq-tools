#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <signal.h>
#include <assert.h>
#include <math.h>

#include <unistd.h>
#include <sys/time.h>

#include "NIDAQmxBase.h"
#include "devices.h"

/* Set sample dynamic range.  Smaller usually yields better resolution. */
static float64 vd_min = -50e-3;
static float64 vd_max = 50e-3;

/* aggregate samples per second */
static float64 sample_rate = (float64)MAX_SAMPLE_RATE;

/* how many samples to average per output data point */
static unsigned long samples_avg = MAX_SAMPLE_RATE / 4;

/* which channel to sample */
static unsigned ai_chan = 0;

/* add offset to each sample */
static double offset = 0.0;

/* Multiply each sample by mult.  Happens after offset is applied. */
static double mult = 1.0;

/* if non-zero, give us current through sense resistor of size rsense ohms */
static double rsense = 0.0;

/* if non-zero, give power (assuming rsense is set) */
static double voltage = 0.0;

static int32 terminal_cfg = DAQmx_Val_Cfg_Default;
static float64 timeout = 10.0;
static unsigned dev_id = 1;

static unsigned verbose;
static unsigned use_timestamp;
static struct timeval tstart;
static volatile int sig;
static char *unit_str;

static TaskHandle task_vd = NULL;

static void
cleanup(void)
{
	if (task_vd) {
		DAQmxBaseStopTask(task_vd);
		DAQmxBaseClearTask(task_vd);
		task_vd = NULL;
	}
}

static void
fatal(int ret)
{
	char buff[2048] = "";
	if (DAQmxFailed(ret)) {
		DAQmxBaseGetExtendedErrorInfo(buff, 2048);
		fprintf(stderr, "DAQmxBase Error: %s\n", buff);
	}
	cleanup();
	exit(1);
}

static int
vd_sample(double *data_out)
{
	int ret;
	int j;
	int32 samples_taken;
	float64 data_buf[samples_avg];
	float64 *dptr = data_buf;

	double sum = 0;

	ret = DAQmxBaseReadAnalogF64(task_vd, samples_avg, timeout,
			DAQmx_Val_GroupByChannel, data_buf, samples_avg, &samples_taken,
			NULL);
	if (DAQmxFailed(ret))
		fatal(ret);

	if (samples_taken != samples_avg) {
		fprintf(stderr, "sample underrun (received %li, expected %lu)\n",
				(long)samples_taken, samples_avg);
		fprintf(stderr, "Try reducing the sample rate or "
		                "increasing the average window.\n");
		fatal(1);
	}

	for (j = 0; j < samples_avg; j++) {
		double data = (double)(*dptr);

		if ((data >= vd_max) || (data <= vd_min)) {
			fprintf(stderr, "data overflow: read %f (min=%lf, max=%lf)\n",
					data, vd_min, vd_max);
			fprintf(stderr, "Check for floating input, or "
			                "change --min and --max\n");
			fatal(1);
		}

		sum += data;
		dptr++;
	}

	if (rsense) {
		/* sum is now current */
		sum /= rsense;
	}

	if (voltage) {
		/* sum is now power */
		sum *= voltage;
	}

	/* everything to milli-bla */
	*data_out = 1000.0 * (sum / (double)samples_avg);

	/* apply the user-specified offset and multiplier */
	*data_out += offset;
	*data_out *= mult;

	return 0;
}

#define CHANSPEC_SZ 128

static int
task_init()
{
	int ret;
	char chanspec[CHANSPEC_SZ];

	ret = DAQmxBaseCreateTask("Vd", &task_vd);
	if (DAQmxFailed(ret))
		fatal(ret);

	snprintf(chanspec, CHANSPEC_SZ, "Dev%u/ai%u", dev_id, ai_chan);

	if (verbose)
		fprintf(stderr, "Vd chanspec: %s\n", chanspec);

	ret = DAQmxBaseCreateAIVoltageChan(task_vd, chanspec, NULL, terminal_cfg,
			vd_min, vd_max, DAQmx_Val_Volts, NULL);
	if (DAQmxFailed(ret))
		fatal(ret);

	ret = DAQmxBaseCfgSampClkTiming(task_vd, "OnboardClock", sample_rate,
			DAQmx_Val_Rising, DAQmx_Val_ContSamps, 0);
	if (DAQmxFailed(ret))
		fatal(ret);

	ret = DAQmxBaseStartTask(task_vd);
	if (DAQmxFailed(ret))
		fatal(ret);

	return ret;
}

void
sighandler(int signum)
{
	sig = signum;
}

static void
display(double data)
{
	if (use_timestamp) {
		struct timeval now;
		struct timeval diff;
		unsigned long long time_us;

		gettimeofday(&now, NULL);
		timersub(&now, &tstart, &diff);
		time_us = diff.tv_sec * 1000000ULL;
		time_us += diff.tv_usec;
		printf("%llu ", (unsigned long long)time_us);
	}

	printf("%lf %s\n", data, unit_str);
}

int
main(int argc, char *argv[])
{
	int i;
	struct sigaction sa = {.sa_handler = &sighandler };
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);

	// go unbuffered since our output is time sensitive
	setbuf(stdout, NULL);

	for (i = 1; i < argc; i++) {
		if (!strcmp ("-D", argv[i])) {
			verbose++;
		} else if (!strcmp("--ts", argv[i])) {
			use_timestamp = 1;
		} else if (!strcmp("--avg", argv[i])) {
			samples_avg = strtoul(argv[++i], NULL, 0);
		} else if (!strcmp("--rate", argv[i])) {
			sample_rate = strtod(argv[++i], NULL);
		} else if (!strcmp("--mult", argv[i])) {
			mult = strtod(argv[++i], NULL);
		} else if (!strcmp("--offset", argv[i])) {
			offset = strtod(argv[++i], NULL);
		} else if (!strcmp("--chan", argv[i])) {
			ai_chan = strtoul(argv[++i], NULL, 0);
		} else if (!strcmp("--rsense", argv[i])) {
			rsense = strtod(argv[++i], NULL) / 1000.0;
		} else if (!strcmp("--voltage", argv[i])) {
			voltage = strtod(argv[++i], NULL) / 1000.0;
		} else if (!strcmp("--min", argv[i])) {
			vd_min = strtod(argv[++i], NULL);
		} else if (!strcmp("--max", argv[i])) {
			vd_max = strtod(argv[++i], NULL);
		} else if (!strcmp("--dev", argv[i])) {
			dev_id = strtoul(argv[++i], NULL, 0);
		} else if (!strcmp("--mode", argv[i])) {
			char *mode = argv[++i];

			if (!strcmp("diff", mode)) {
				terminal_cfg = DAQmx_Val_Diff;
			} else if (!strcmp("nrse", mode)) {
				terminal_cfg = DAQmx_Val_NRSE;
			} else if (!strcmp("rse", mode)) {
				terminal_cfg = DAQmx_Val_RSE;
			}
		}
	}

	/*
	 * Work out what units we are displaying
	 */
	if (voltage && rsense) {
		unit_str = "mW";
	} else if (rsense) {
		unit_str = "mA";
	} else if (!rsense && !voltage){
		unit_str = "mV";
	} else {
		unit_str = "??";
	}

	task_init();

	gettimeofday(&tstart, NULL);

	for (;;) {
		double data;

		vd_sample(&data);

		if (sig) {
			cleanup();
			exit(-sig);
		}

		display(data);
	}

	cleanup();

	return 0;
}

