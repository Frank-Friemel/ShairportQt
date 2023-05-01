#pragma once

#define NUM_CHANNELS	        2
#define	SAMPLE_SIZE		        16
#define	SAMPLE_FACTOR	        (NUM_CHANNELS * (SAMPLE_SIZE >> 3))
#define	SAMPLE_FREQ		        (44100.0l)
#define BYTES_PER_SEC	        (SAMPLE_FREQ * SAMPLE_FACTOR)

#define START_FILL_MS	        500
#define	MIN_FILL_MS		        50
#define	MAX_FILL_MS		        2000

#define MAX_DB_VOLUME           0
#define MIN_DB_VOLUME           (-144)

#define LOW_LEVEL_RTP_QUEUE     64
#define MIN_RTP_LEVEL_OFFSET    64

#define	LOG_FILE_NAME			"ShairportQt.log"