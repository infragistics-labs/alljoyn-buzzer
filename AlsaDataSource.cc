/**
 * Handle feeding audio input from a capture device thru ALSA.
 * (c) Infragistics, 2015.
 */

#include <iostream>
#include <pthread.h>
#include "AlsaDataSource.h"
#include <alsa/asoundlib.h>
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

using namespace std;

namespace ajn {
namespace services {

AlsaDataSource::AlsaDataSource() : DataSource(), capture_handle(NULL), mBufferSize(0), firstTime(true) {
}

AlsaDataSource::~AlsaDataSource() {
    Close();
}

bool AlsaDataSource::IsDataReady() {
    int err;
    snd_pcm_state_t state = snd_pcm_state(capture_handle);
    if (state == SND_PCM_STATE_SETUP) {
       err = snd_pcm_prepare(capture_handle);
       if (err) cout << "cannot prepare (%s)" << snd_strerror (err) << endl;
       return IsDataReady();
    } else if (state == SND_PCM_STATE_PREPARED) {
       err = snd_pcm_start (capture_handle); 
       if (err) cout << "cannot start (%s)" << snd_strerror (err) << endl;
       return IsDataReady();       
    } else if (state == SND_PCM_STATE_DRAINING) {
       cout << "draining";
       return true;
    } else if (state == SND_PCM_STATE_RUNNING) {
       snd_pcm_sframes_t avail;
       while(1) {
    	       pthread_mutex_lock(&mutex);
	       avail = snd_pcm_avail_update(capture_handle);
	       if (avail >= 0) {
		    size_t buflen = preBufferWritePos - preBufferReadPos;
                    bool buffered = buflen > ((mBufferSize * mBytesPerFrame) * 2);
                    bool isReady;
		    if (avail == 0) { 
			isReady = buffered;
		    } else if ((preBufferWritePos + avail * mBytesPerFrame) < preBufferSize) {
                        ssize_t result;
			if ((result = snd_pcm_readi(capture_handle, preBuffer + preBufferWritePos, avail)) < 0) {
			    cout << "read: failed (%s)" << snd_strerror (result) << endl;
			    isReady = false;
			} else if (result == 0) {
			    cout << "read: no data" << endl;
			    isReady = false;
			} else {
			    preBufferWritePos += result * mBytesPerFrame; 
			}
		    } else {
			cout << "data available" << buflen << endl;
			isReady = true;
		    }      
    	            pthread_mutex_unlock(&mutex);
                    return isReady;
	       } else if (avail == -EPIPE) {
		    err = snd_pcm_recover(capture_handle, avail, 0);
		    if (err) cout << "cannot recover (%s)" << snd_strerror (err) << endl;
		    /*
		    err = snd_pcm_drain(capture_handle);
		    if (err) cout << "cannot drain (%s)" << snd_strerror (err) << endl;*/
    	            pthread_mutex_unlock(&mutex);
		    return IsDataReady();
	       } else {
		    cout << "avail failed" << snd_strerror(avail) << endl;
    	            pthread_mutex_unlock(&mutex);
		    return IsDataReady();
	       }
    	       pthread_mutex_unlock(&mutex);
       }
    } else if (state == SND_PCM_STATE_XRUN) {
       err = snd_pcm_drop(capture_handle);
       if (err) cout << "cannot drop (%s)" << snd_strerror (err) << endl;
       return IsDataReady();
    } else {
       cout << "not running" << endl;
       return false;
    }
}

bool AlsaDataSource::Open(const char* filePath) {
    mChannelsPerFrame = 1;
    mSampleRate = 48000;
    mBitsPerChannel = 16;
    mBytesPerFrame = (mBitsPerChannel/8)*mChannelsPerFrame;
    mInputSize = 0xFFFFFFFF;
    mInputDataStart = 0;

    int err;
    printf("Filepath %s\n", filePath);
    if ((err = snd_pcm_open (&capture_handle, filePath, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    	fprintf (stderr, "cannot open audio device %s (%s)\n", 
    		 filePath,
    		 snd_strerror (err));
    	return false;
    }
       
    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
    	fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
    		 snd_strerror (err));
    	return false;
    }
    		 
    if ((err = snd_pcm_hw_params_any (capture_handle, hw_params)) < 0) {
    	fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
    		 snd_strerror (err));
    	return false;
    }
    
    if ((err = snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    	fprintf (stderr, "cannot set access type (%s)\n",
    		 snd_strerror (err));
    	return false;
    }
    
    if ((err = snd_pcm_hw_params_set_format (capture_handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
    	fprintf (stderr, "cannot set sample format (%s)\n",
    		 snd_strerror (err));
    	return false;
    }
    
    unsigned int exact_uvalue = mSampleRate;
    int dir = 0;
    if ((err = snd_pcm_hw_params_set_rate_near (capture_handle, hw_params, &exact_uvalue, &dir)) < 0) {
    	fprintf (stderr, "cannot set sample rate (%s)\n",
    		 snd_strerror (err));
    	return false;
    }
    if (dir != 0) {
	cout << "Alsa error: " << mSampleRate <<
		" rate is not supported by your hardware. Using " << exact_uvalue << endl;
    }
    mSampleRate = exact_uvalue;
    
    if ((err = snd_pcm_hw_params_set_channels (capture_handle, hw_params, mChannelsPerFrame)) < 0) {
    	fprintf (stderr, "cannot set channel count (%s)\n",
    		 snd_strerror (err));
    	return false;
    }
 
    snd_pcm_uframes_t periodsize = 1024;
    unsigned long exact_ulvalue = periodsize;
    if ((err = snd_pcm_hw_params_set_period_size_near(capture_handle, hw_params, &exact_ulvalue, &dir)) < 0) {
    	fprintf (stderr, "cannot set period size (%s)\n",
    		 snd_strerror (err));
    	return false;
    }
    if (dir != 0) {
	cout << "Alsa error: " << periodsize <<
		" periods size is not supported by your hardware. Using " << exact_ulvalue << endl;
    }
    periodSize = exact_ulvalue;

    unsigned int periods = 2;
    if ((err = snd_pcm_hw_params_set_periods_near(capture_handle, hw_params, &exact_uvalue, &dir)) < 0) {
    	fprintf (stderr, "cannot set periods (%s)\n",
    		 snd_strerror (err));
    	return false;
    } 
    if (dir != 0) {
	cout << "Alsa error: " << periods <<
		" periods is not supported by your hardware. Using " << exact_uvalue << endl;
    }

    snd_pcm_uframes_t buffer_size = 65535;
    snd_pcm_uframes_t exact_buffer_size = buffer_size;
    if ((err = snd_pcm_hw_params_set_buffer_size_near(capture_handle, hw_params, &exact_buffer_size)) < 0) {
    	fprintf (stderr, "cannot set buffer size (%s)\n",
    		 snd_strerror (err));
    	return false;
    } 
    if (buffer_size != exact_buffer_size) {
	cout << "Alsa error: " << buffer_size <<
		" buffer_size is not supported by your hardware. Using " << exact_buffer_size << endl;
    }

    if ((err = snd_pcm_hw_params (capture_handle, hw_params)) < 0) {
    	fprintf (stderr, "cannot set parameters (%s)\n",
    		 snd_strerror (err));
    	return false;
    }

    if ((err = snd_pcm_hw_params_get_buffer_size(hw_params, &buffer_size) < 0)) {
    	fprintf (stderr, "cannot get buffer size (%s)\n",
    		 snd_strerror (err));
    	return false;
    }

    mBufferSize = (int)buffer_size;

    preBufferSize = sizeof(uint8_t) * 1024 * 1024 *10;
    preBuffer = (uint8_t*)malloc(preBufferSize);
    memset(preBuffer, 0, preBufferSize); 
    preBufferWritePos = mBufferSize * mBytesPerFrame;
    preBufferReadPos = 0;
 
    snd_pcm_hw_params_free (hw_params);
    
    if ((err = snd_pcm_prepare (capture_handle)) < 0) {
    	fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
    		 snd_strerror (err));
    	return false;
    }

    snd_output_t *log;
    snd_output_stdio_attach(&log, stderr, 0);
    snd_pcm_dump(capture_handle, log);

    /* Pre buffer */
    //while(!IsDataReady()) usleep(10*1000);
    printf ("ALSA INITIALIZED: %u\n", mBufferSize);

    return true;
}

void AlsaDataSource::Close() {
    pthread_mutex_lock(&mutex);
    if (capture_handle) {
        snd_pcm_close(capture_handle);
        capture_handle = NULL;
    }
    pthread_mutex_unlock(&mutex);
}

size_t AlsaDataSource::ReadData(uint8_t* ptr, size_t offset, size_t count) {   
    size_t size = 0;
    pthread_mutex_lock(&mutex);
    if (capture_handle) {
        size_t len = preBufferWritePos - preBufferReadPos;
        size = MIN(count, len);
    	memcpy(ptr, preBuffer + preBufferReadPos, size);
    	memmove(preBuffer, preBuffer + preBufferReadPos + size, len - size);
	preBufferReadPos = 0;
    	preBufferWritePos = len - size;
    }

    pthread_mutex_unlock(&mutex);
    return size; 

}

}
}
