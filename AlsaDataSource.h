/**
 * Handle feeding audio input from a capture device thru ALSA.
 * (c) Infragistics, 2015.
 */

#ifndef _ALSADATASOURCE_H_
#define _ALSADATASOURCE_H_

#ifndef __cplusplus
#error Only include AlsaDataSource.h in C++ code.
#endif

#include <alsa/asoundlib.h>
#include <alljoyn/audio/DataSource.h>
#include <stdio.h>

namespace ajn {
namespace services {

/**
 * A WAV file data input source.
 */
class AlsaDataSource : public DataSource {
  public:
    AlsaDataSource();
    virtual ~AlsaDataSource();

    /**
     * Opens the file used to read data from.
     *
     * @param[in] filePath the file path.
     *
     * @return true if open.
     */
    bool Open(const char* filePath);
    /**
     * Closes the file used to read data from.
     */
    void Close();

    double GetSampleRate() { return mSampleRate; }
    uint32_t GetBytesPerFrame() { return mBytesPerFrame; }
    uint32_t GetChannelsPerFrame() { return mChannelsPerFrame; }
    uint32_t GetBitsPerChannel() { return mBitsPerChannel; }
    uint32_t GetInputSize() { return mInputSize; }

    size_t ReadData(uint8_t* buffer, size_t offset, size_t length);
    bool IsDataReady();

    /**
     * Since we read ondemand from a file always return true that data is ready
     */

  private:
    snd_pcm_t *capture_handle;
    snd_pcm_hw_params_t *hw_params;
    double mSampleRate;
    uint32_t mBytesPerFrame;
    uint32_t mChannelsPerFrame;
    uint32_t mBitsPerChannel;
    uint32_t mInputSize;
    uint32_t mInputDataStart;
    uint32_t mBufferSize;
    uint8_t* preBuffer;
    size_t preBufferSize;
    size_t preBufferWritePos;
    size_t preBufferReadPos;
    snd_pcm_sframes_t availableFrames;
    snd_pcm_sframes_t periodSize;
    bool firstTime;
};

}
}

#endif //_ALSADATASOURCE_H_
