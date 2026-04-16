#include <alsa/asoundlib.h>

#include <array>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__QNXNTO__)
 #include <unistd.h>
 #include <libgen.h>
#endif

namespace
{
    std::atomic<bool> keepRunning { true };
    FILE* logFile = nullptr;

    void writeLog (const std::string& line)
    {
        if (logFile == nullptr)
            return;

        std::fputs ((line + "\n").c_str(), logFile);
        std::fflush (logFile);
    }

    void signalHandler (int)
    {
        keepRunning = false;
    }

    std::string getLogPath (const char* argv0)
    {
       #if defined(__QNXNTO__)
        std::array<char, 1024> buffer {};
        std::strncpy (buffer.data(), argv0 != nullptr ? argv0 : ".", buffer.size() - 1);
        const auto* dir = dirname (buffer.data());
        return std::string (dir != nullptr ? dir : ".") + "/QNXALSAProbe.log";
       #else
        (void) argv0;
        return "QNXALSAProbe.log";
       #endif
    }

    bool playToneOnDevice (const char* deviceName)
    {
        writeLog (std::string ("Trying ALSA device: ") + deviceName);

        snd_pcm_t* handle = nullptr;
        int err = snd_pcm_open (&handle, deviceName, SND_PCM_STREAM_PLAYBACK, 0);

        if (err < 0)
        {
            writeLog ("snd_pcm_open failed: " + std::string (snd_strerror (err)));
            return false;
        }

        constexpr unsigned int sampleRate = 48000;
        constexpr int channels = 2;
        constexpr snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;

        err = snd_pcm_set_params (handle,
                                  format,
                                  SND_PCM_ACCESS_RW_INTERLEAVED,
                                  (unsigned int) channels,
                                  sampleRate,
                                  1,
                                  500000);

        if (err < 0)
        {
            writeLog ("snd_pcm_set_params failed: " + std::string (snd_strerror (err)));
            snd_pcm_close (handle);
            return false;
        }

        writeLog ("ALSA device opened successfully");

        constexpr double frequency = 440.0;
        constexpr int totalSeconds = 2;
        constexpr int framesPerBlock = 512;
        constexpr double twoPi = 6.28318530717958647692;
        constexpr double amplitude = 0.18 * 32767.0;

        std::vector<short> buffer ((size_t) framesPerBlock * (size_t) channels);
        double phase = 0.0;
        const double phaseDelta = twoPi * frequency / (double) sampleRate;
        int framesRemaining = sampleRate * totalSeconds;

        while (keepRunning && framesRemaining > 0)
        {
            const int framesThisBlock = framesRemaining > framesPerBlock ? framesPerBlock : framesRemaining;

            for (int i = 0; i < framesThisBlock; ++i)
            {
                const auto sample = (short) std::lround (std::sin (phase) * amplitude);
                phase += phaseDelta;

                if (phase >= twoPi)
                    phase -= twoPi;

                buffer[(size_t) i * 2] = sample;
                buffer[(size_t) i * 2 + 1] = sample;
            }

            auto written = snd_pcm_writei (handle, buffer.data(), (snd_pcm_uframes_t) framesThisBlock);

            if (written < 0)
            {
                written = snd_pcm_recover (handle, (int) written, 1);

                if (written < 0)
                {
                    writeLog ("snd_pcm_writei failed: " + std::string (snd_strerror ((int) written)));
                    snd_pcm_close (handle);
                    return false;
                }

                continue;
            }

            framesRemaining -= (int) written;
        }

        snd_pcm_drain (handle);
        snd_pcm_close (handle);
        writeLog ("Tone playback completed on device");
        return true;
    }

    std::vector<std::string> getPlaybackDeviceNames()
    {
        std::vector<std::string> result;
        void** hints = nullptr;

        if (snd_device_name_hint (-1, "pcm", &hints) < 0 || hints == nullptr)
            return result;

        for (auto** current = hints; *current != nullptr; ++current)
        {
            char* name = snd_device_name_get_hint (*current, "NAME");
            char* ioid = snd_device_name_get_hint (*current, "IOID");
            char* desc = snd_device_name_get_hint (*current, "DESC");

            const bool isOutput = (ioid == nullptr || std::strcmp (ioid, "Output") == 0);

            if (name != nullptr && isOutput)
            {
                const std::string deviceName (name);

                if (std::find (result.begin(), result.end(), deviceName) == result.end())
                {
                    writeLog ("Enumerated playback device: " + deviceName
                              + (desc != nullptr ? " desc=" + std::string (desc) : std::string()));
                    result.push_back (deviceName);
                }
            }

            if (name != nullptr) free (name);
            if (ioid != nullptr) free (ioid);
            if (desc != nullptr) free (desc);
        }

        snd_device_name_free_hint (hints);
        return result;
    }
}

int main (int argc, char* argv[])
{
    const auto logPath = getLogPath (argc > 0 ? argv[0] : nullptr);
    logFile = std::fopen (logPath.c_str(), "a");

    if (logFile == nullptr)
        return 1;

    writeLog ("\n**********************************************************");
    writeLog ("QNX ALSA Probe");
   #if defined(JUCE_QNX_HELLO_WORLD_BUILD_VERSION)
    writeLog (std::string ("Build version: ") + JUCE_QNX_HELLO_WORLD_BUILD_VERSION);
   #endif
   #if defined(__QNXNTO__)
    writeLog ("Process PID: " + std::to_string ((int) getpid()));
   #endif

    std::signal (SIGINT, signalHandler);
    std::signal (SIGTERM, signalHandler);

    auto deviceNames = getPlaybackDeviceNames();

    if (deviceNames.empty())
    {
        deviceNames = { "default", "plughw:0,0", "hw:0,0", "plughw:1,0", "hw:1,0" };
        writeLog ("Falling back to built-in device name list");
    }

    bool anySucceeded = false;

    for (const auto& deviceName : deviceNames)
    {
        if (playToneOnDevice (deviceName.c_str()))
            anySucceeded = true;
    }

    if (! anySucceeded)
        writeLog ("All ALSA device attempts failed");

    std::fclose (logFile);
    return anySucceeded ? 0 : 1;
}
