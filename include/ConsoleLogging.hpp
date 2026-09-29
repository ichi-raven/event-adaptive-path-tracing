#ifndef EVENTRENDERER_INCLUDE_CONSOLE_LOGGING_HPP_
#define EVENTRENDERER_INCLUDE_CONSOLE_LOGGING_HPP_

#include "Console.hpp"
#include <ng-log/logging.h>
#include <atomic>

class ConsoleLogging final : public nglog::LogSink
{
public:
    explicit ConsoleLogging(const char* program)
    {
        nglog::InitializeLogging(program);
        // The sink owns console output; avoid duplicate stderr writes and implicit log files.
        FLAGS_logtostderr     = false;
        FLAGS_logtostdout     = false;
        FLAGS_alsologtostderr = false;
        FLAGS_stderrthreshold = nglog::NUM_SEVERITIES;
        FLAGS_v               = 0;
        for (int level = 0; level < nglog::NUM_SEVERITIES; ++level)
        {
            nglog::SetLogDestination(static_cast<nglog::LogSeverity>(level), "");
        }
        nglog::AddLogSink(this);
    }

    ~ConsoleLogging() override
    {
        nglog::RemoveLogSink(this);
        nglog::ShutdownLogging();
    }

    ConsoleLogging(const ConsoleLogging&)            = delete;
    ConsoleLogging& operator=(const ConsoleLogging&) = delete;

    // Configure before worker threads start; FLAGS_v is ng-log's verbosity control.
    void configure(bool verbose)
    {
        mVerbose.store(verbose);
        FLAGS_v = verbose ? 1 : 0;
    }

    void send(nglog::LogSeverity severity, const char*, const char* filename, int line, const nglog::LogMessageTime&,
              const char* message, size_t length) override
    {
        std::string text;
        if (mVerbose.load())
        {
            text = '[' + std::string(filename) + ':' + std::to_string(line) + "] ";
        }
        text.append(message, length);
        // LogSink::send runs under ng-log's lock. Do not use LOG() or CHECK() here.
        ConsoleOutput::instance().log(nglog::GetLogSeverityName(severity), text, severity >= NGLOG_ERROR);
    }

private:
    std::atomic<bool> mVerbose{ false };
};

#endif
