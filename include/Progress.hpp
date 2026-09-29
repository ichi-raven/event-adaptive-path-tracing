#ifndef PROGRESS_H
#define PROGRESS_H

#include "Console.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

class ProgressBar
{
public:
    explicit ProgressBar(uint64_t total, std::string label = "Frames", bool showEvents = false)
        : mTotal(total)
        , mLabel(std::move(label))
        , mShowEvents(showEvents)
    {
        if (total == 0)
        {
            throw std::invalid_argument("progress total must be positive");
        }
    }

    ~ProgressBar()
    {
        // An interrupted operation must not display a fabricated 100%.
        try
        {
            if (mStarted)
            {
                ConsoleOutput::instance().finishProgress();
            }
        }
        catch (...)
        {
        }
    }

    ProgressBar(const ProgressBar&)            = delete;
    ProgressBar& operator=(const ProgressBar&) = delete;

    void start()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mStarted)
        {
            startUnlocked();
        }
    }

    // Call only after the frame has been decoded and its image has been saved.
    void step(std::optional<uint64_t> detectedEvents = std::nullopt)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mStarted)
        {
            startUnlocked();
        }
        if (mCompleted == mTotal)
        {
            return;
        }
        ++mCompleted;
        if (detectedEvents)
        {
            mEvents     = *detectedEvents;
            mShowEvents = true;
        }
        drawUnlocked(mCompleted == mTotal);
        if (mCompleted == mTotal)
        {
            ConsoleOutput::instance().finishProgress();
        }
    }

    static std::string formatCount(uint64_t value)
    {
        auto text = std::to_string(value);
        for (size_t i = text.size(); i > 3; i -= 3)
        {
            text.insert(i - 3, 1, ',');
        }
        return text;
    }

    static std::string formatDuration(double seconds)
    {
        if (!std::isfinite(seconds) || seconds < 0 ||
            seconds >= static_cast<double>(std::numeric_limits<uint64_t>::max()))
        {
            return "--";
        }
        const auto s = static_cast<uint64_t>(seconds);
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setfill('0');
        if (s >= 3600)
        {
            out << s / 3600 << ':' << std::setw(2) << s / 60 % 60 << ':';
        }
        else
        {
            out << std::setw(2) << s / 60 << ':';
        }
        out << std::setw(2) << s % 60;
        return out.str();
    }

private:
    using Clock = std::chrono::steady_clock;

    void startUnlocked()
    {
        mStarted  = true;
        mStart    = Clock::now();
        mLastDraw = mStart;
        drawUnlocked(true);
    }

    void drawUnlocked(bool force)
    {
        const auto now         = Clock::now();
        const bool interactive = ConsoleOutput::instance().interactive();
        const double fraction  = static_cast<double>(mCompleted) / static_cast<double>(mTotal);
        const int bucket       = static_cast<int>(fraction * 10);
        const double sinceDraw = std::chrono::duration<double>(now - mLastDraw).count();
        // Always show start/end. Pipes get one update per 10% or 5 seconds at most.
        if (!force && (interactive ? sinceDraw < 0.1 : sinceDraw < 5.0 && bucket == mLastBucket))
        {
            return;
        }
        const double elapsed = std::chrono::duration<double>(now - mStart).count();
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << mLabel << ' ';
        if (interactive)
        {
            const auto filled = static_cast<size_t>(fraction * 10);
            out << '[' << std::string(filled, '=') << std::string(10 - filled, '-') << "] ";
        }
        out << formatCount(mCompleted) << '/'                                            //
            << formatCount(mTotal)                                                       //
            << (interactive ? " (" : " frames (") << std::fixed << std::setprecision(1)  //
            << fraction * 100 << "%)";
        if (mShowEvents)
        {
            out << " | " << formatCount(mEvents) << " events";
        }
        out << (interactive ? " | " : " | elapsed ")  //
            << formatDuration(elapsed)                //
            << (interactive ? "/" : " | ETA ");
        if (mCompleted == mTotal)
        {
            out << "00:00";
        }
        else if (mCompleted >= 3 && elapsed > 0.0)
        {
            out << '~'
                << formatDuration(elapsed / static_cast<double>(mCompleted) * static_cast<double>(mTotal - mCompleted));
        }
        else
        {
            out << "--";
        }
        out << " | ";
        if (mCompleted > 0 && elapsed > 0.0)
        {
            const double fps = static_cast<double>(mCompleted) / elapsed;
            if (fps >= 1.0)
            {
                out << fps << (interactive ? " f/s" : " frames/s");
            }
            else
            {
                out << elapsed / static_cast<double>(mCompleted)  //
                    << (interactive ? " s/f" : " s/frame");
            }
        }
        else
        {
            out << "--";
        }

        ConsoleOutput::instance().progress(out.str());
        mLastDraw   = now;
        mLastBucket = bucket;
    }

    std::mutex mMutex;
    const uint64_t mTotal;
    const std::string mLabel;
    bool mShowEvents;
    bool mStarted       = false;
    uint64_t mCompleted = 0;
    uint64_t mEvents    = 0;
    Clock::time_point mStart{};
    Clock::time_point mLastDraw{};
    int mLastBucket = -1;
};

#endif
