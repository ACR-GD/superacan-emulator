#pragma once
#include <chrono>
#include <deque>
#include <numeric>

// Performance monitoring and statistics
class PerfMon {
public:
    using clock = std::chrono::high_resolution_clock;
    using time_point = clock::time_point;
    using duration = std::chrono::duration<double, std::milli>;

    PerfMon(size_t history_size = 60) : max_history(history_size) {}

    void frame_start() {
        frame_start_time = clock::now();
    }

    void frame_end() {
        auto end = clock::now();
        duration frame_time = end - frame_start_time;
        frame_times.push_back(frame_time.count());
        
        if (frame_times.size() > max_history) {
            frame_times.pop_front();
        }

        frames_rendered++;
        
        // Update per-second stats
        auto elapsed_since_last_second = end - last_second_time;
        if (elapsed_since_last_second >= std::chrono::seconds(1)) {
            fps = frames_this_second;
            frames_this_second = 0;
            last_second_time = end;
        }
        frames_this_second++;
    }

    // Get average frame time in milliseconds
    double get_avg_frame_time() const {
        if (frame_times.empty()) return 0.0;
        return std::accumulate(frame_times.begin(), frame_times.end(), 0.0) / frame_times.size();
    }

    // Get minimum frame time in milliseconds
    double get_min_frame_time() const {
        if (frame_times.empty()) return 0.0;
        return *std::min_element(frame_times.begin(), frame_times.end());
    }

    // Get maximum frame time in milliseconds
    double get_max_frame_time() const {
        if (frame_times.empty()) return 0.0;
        return *std::max_element(frame_times.begin(), frame_times.end());
    }

    // Get current FPS
    double get_fps() const {
        return fps;
    }

    // Get instantaneous FPS from last frame
    double get_instant_fps() const {
        if (frame_times.empty()) return 0.0;
        double last_frame_ms = frame_times.back();
        return last_frame_ms > 0.0 ? 1000.0 / last_frame_ms : 0.0;
    }

    // Get total frames rendered
    uint64_t get_total_frames() const {
        return frames_rendered;
    }

    void reset() {
        frame_times.clear();
        frames_rendered = 0;
        frames_this_second = 0;
        fps = 0.0;
        last_second_time = clock::now();
    }

private:
    time_point frame_start_time;
    time_point last_second_time = clock::now();
    std::deque<double> frame_times;
    size_t max_history;
    uint64_t frames_rendered = 0;
    uint64_t frames_this_second = 0;
    double fps = 0.0;
};
