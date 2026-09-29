#ifndef EVENTRENDERER_INCLUDE_CONSOLE_HPP_
#define EVENTRENDERER_INCLUDE_CONSOLE_HPP_

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

// All application console writes share this lock. Never call LOG() while holding it.
class ConsoleOutput
{
public:
    static ConsoleOutput& instance()
    {
        static ConsoleOutput output;
        return output;
    }

    static bool isTerminal()
    {
        const char* term = std::getenv("TERM");
        if (term && std::string_view(term) == "dumb")
        {
            return false;
        }
#ifdef _WIN32
        return _isatty(_fileno(stderr)) != 0;
#else
        return isatty(STDERR_FILENO) != 0;
#endif
    }

    void configure(bool enabled, bool interactive)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        finishUnlocked();
        mEnabled     = enabled;
        mInteractive = interactive;
    }

    bool interactive() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mInteractive;
    }

    void log(std::string_view level, std::string_view message, bool error = false)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        eraseUnlocked();
        if (error)
        {
            mActive = false;
        }
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        {
            message.remove_suffix(1);
        }
        // Prefix each line of a multiline message, rather than leaving unlabelled lines.
        do
        {
            const auto end = message.find('\n');
            std::cerr << '[' << level << "] " << message.substr(0, end) << '\n';
            if (end == std::string_view::npos)
            {
                break;
            }
            message.remove_prefix(end + 1);
        } while (!message.empty());
        if (mActive && mInteractive)
        {
            drawUnlocked();
        }
        std::cerr.flush();
    }

    void progress(std::string text)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mEnabled)
        {
            return;
        }
        eraseUnlocked();
        mText   = std::move(text);
        mActive = true;
        if (mInteractive)
        {
            drawUnlocked();
        }
        else
        {
            std::cerr << mText << '\n';
        }
        std::cerr.flush();
    }

    void finishProgress()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        finishUnlocked();
    }

private:
    ConsoleOutput()
        : mInteractive(isTerminal())
    {
    }

    static size_t lineWidth()
    {
#ifndef _WIN32
        winsize size{};
        if (ioctl(STDERR_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 1)
        {
            return std::min<size_t>(size.ws_col - 1, 1024);
        }
#endif
        return 79;  // Also works in classic Windows consoles without ANSI support.
    }

    void eraseUnlocked()
    {
        if (mDrawnWidth != 0)
        {
            std::cerr << '\r' << std::string(mDrawnWidth, ' ') << '\r';
            mDrawnWidth = 0;
        }
    }

    void drawUnlocked()
    {
        const auto width = std::min(mText.size(), lineWidth());
        std::cerr.write(mText.data(), static_cast<std::streamsize>(width));
        mDrawnWidth = width;
    }

    void finishUnlocked()
    {
        if (mActive && mInteractive)
        {
            std::cerr << '\n';
            std::cerr.flush();
        }
        mActive     = false;
        mDrawnWidth = 0;
    }

    mutable std::mutex mMutex;
    bool mEnabled = true;
    bool mInteractive;
    bool mActive       = false;
    size_t mDrawnWidth = 0;
    std::string mText;
};

#endif
