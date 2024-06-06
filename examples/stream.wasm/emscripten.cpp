#include "ggml.h"
#include "whisper.h"

#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

constexpr int N_THREAD = 8;

// The following variables are SHARED between main and worker
// Lock g_mutex before using them

// Whisper contexts used by running whisper models
std::vector<struct whisper_context *> g_contexts(4, nullptr);

std::string g_status        = "";
std::string g_status_forced = "";

// Transcript of the latest audio clip processed by whisper
std::string g_transcribed   = "";

// Buffer for the audio clip to be processed by whisper
std::vector<float> g_pcmf32;

// SHARED variables end here

// The worker thread running whisper, and its mutex
std::mutex g_mutex;
std::thread g_worker;

// Flag to make te worker thread stop
std::atomic<bool> g_running(false);

void stream_set_status(const std::string & status) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status = status;
}

/**
 * Main function of the worker thread which runs whisper model on the global audio buffer
 * @param index 0-based index of the model's context
 */
void stream_main(size_t index) {
    stream_set_status("loading data ...");

    struct whisper_full_params wparams = whisper_full_default_params(whisper_sampling_strategy::WHISPER_SAMPLING_GREEDY);

    wparams.n_threads        = std::min(N_THREAD, (int) std::thread::hardware_concurrency());
    wparams.offset_ms        = 0;
    wparams.translate        = false;
    wparams.no_context       = true;
    wparams.single_segment   = true;
    wparams.print_realtime   = false;
    wparams.print_progress   = false;
    wparams.print_timestamps = true;
    wparams.print_special    = false;

    wparams.max_tokens       = 32;
    wparams.audio_ctx        = 768; // partial encoder context for better performance

    // disable temperature fallback
    wparams.temperature_inc  = -1.0f;

    wparams.language         = "en";

    printf("stream: using %d threads\n", wparams.n_threads);

    std::vector<float> pcmf32;

    // whisper context
    auto & ctx = g_contexts[index];

    // 5 seconds interval
    const int64_t window_samples = 5*WHISPER_SAMPLE_RATE;

    // run whisper until flag tells us to stop
    while (g_running) {
        stream_set_status("waiting for audio ...");

        // Grab audio from global buffer
        {
            // Use unique_lock because we need to unlock mutex early sometimes
            std::unique_lock<std::mutex> lock(g_mutex);

            // Lock mutex and check every ten ms whether the global audio buffer has enough data 
            if (g_pcmf32.size() < 1024) {
                lock.unlock();

                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                continue;
            }

            // Grab the last k samples from the global buffer and clear it
            pcmf32 = std::vector<float>(g_pcmf32.end() - std::min((int64_t) g_pcmf32.size(), window_samples), g_pcmf32.end());
            g_pcmf32.clear();
        }

        // Run whisper on the audio
        {
            // Time how long whisper takes, optional
            const auto t_start = std::chrono::high_resolution_clock::now();

            stream_set_status("running whisper ...");

            int ret = whisper_full(ctx, wparams, pcmf32.data(), pcmf32.size());
            if (ret != 0) {
                printf("whisper_full() failed: %d\n", ret);
                break;
            }

            const auto t_end = std::chrono::high_resolution_clock::now();

            printf("stream: whisper_full() processed %f seconds of audio and returned %d in %f seconds\n", ((float)pcmf32.size()) / WHISPER_SAMPLE_RATE, ret, std::chrono::duration<double>(t_end - t_start).count());
        }

        // Grab the transcript from whisper's output
        {
            std::string text_heard;

            {
                const int n_segments = whisper_full_n_segments(ctx);
                if (n_segments > 0) {
                    const char * text = whisper_full_get_segment_text(ctx, n_segments - 1);

                    // ??? Mysterious unused times
                    const int64_t t0 = whisper_full_get_segment_t0(ctx, n_segments - 1); 
                    const int64_t t1 = whisper_full_get_segment_t1(ctx, n_segments - 1);

                    printf("transcribed: %s\n", text);

                    text_heard += text;
                }
            }

            // Then update global transcript
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_transcribed = text_heard;
            }
        }
    }

    // then free whisper
    if (index < g_contexts.size()) {
        whisper_free(g_contexts[index]);
        g_contexts[index] = nullptr;
    }
}

EMSCRIPTEN_BINDINGS(stream) {
    /**
     * Initialize a whisper model using the ggml file at the given path
     * @param path_model Path to a ggml model file
     * @returns Index of the created model (starting at 1), or 0 if the model could not be initialized
     */
    emscripten::function("init", emscripten::optional_override([](const std::string & path_model) {
        for (size_t i = 0; i < g_contexts.size(); ++i) {
            // Find the first null g_context
            if (g_contexts[i] == nullptr) {
                // Load the given model into it
                g_contexts[i] = whisper_init_from_file_with_params(path_model.c_str(), whisper_context_default_params());
                if (g_contexts[i] != nullptr) {
                    g_running = true;
                    // Wait for g_worker to finish, then create a new one running stream_main on i
                    if (g_worker.joinable()) {
                        g_worker.join();
                    }
                    g_worker = std::thread([i]() {
                        stream_main(i);
                    });

                    return i + 1;
                } else {
                    return (size_t) 0;
                }
            }
        }

        return (size_t) 0;
    }));

    emscripten::function("free", emscripten::optional_override([](size_t index) {
        if (g_running) {
            g_running = false;
        }
    }));

    /**
     * Set the audio buffer of a given whisper model
     * @param index 1-based index of the model
     * @param audio Float32Array representing an audio clip
     */
    emscripten::function("set_audio", emscripten::optional_override([](size_t index, const emscripten::val & audio) {
        // the index given to the js code is 1-based, we need to make it 0-based first
        --index;

        if (index >= g_contexts.size()) {
            return -1;
        }

        if (g_contexts[index] == nullptr) {
            return -2;
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            // This is safe as g_pcmf is of type vector<float>, and audio is of type Float32Array
            // Which yields typed_memory_view of the correct type
            // See val.h for implementation of convertJSArrayToNumberVector
            g_pcmf32 = emscripten::convertJSArrayToNumberVector<float>(audio);
        }

        return 0;
    }));

    emscripten::function("get_transcribed", emscripten::optional_override([]() {
        std::string transcribed;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            transcribed = std::move(g_transcribed);
        }

        return transcribed;
    }));

    emscripten::function("get_status", emscripten::optional_override([]() {
        std::string status;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            status = g_status_forced.empty() ? g_status : g_status_forced;
        }

        return status;
    }));

    emscripten::function("set_status", emscripten::optional_override([](const std::string & status) {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_status_forced = status;
        }
    }));
}
