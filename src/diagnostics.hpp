#pragma once
#include <array>
#include <cstdarg>
#include <cstddef>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// Fixed-size event ring: hot hooks write to RAM; the main thread flushes periodically.
// Fatal POSIX signals dump the same ring with async-signal-safe write() calls.
namespace afterflowdiag {
    inline constexpr size_t EVENT_CAPACITY = 256;
    inline constexpr size_t EVENT_SIZE = 224;
    inline constexpr size_t MAX_LOG_BYTES = 2 * 1024 * 1024;

    struct Event {
        char text[EVENT_SIZE]{};
        volatile std::sig_atomic_t length = 0;
    };
    inline Event g_events[EVENT_CAPACITY]{};
    inline volatile std::sig_atomic_t g_sequence = 0;
    inline volatile std::sig_atomic_t g_lastFlushed = 0;
    inline volatile std::sig_atomic_t g_recordingEnabled = 0;
    inline volatile std::sig_atomic_t g_inFatalHandler = 0;
    inline const char* volatile g_activeHook = "startup";
    inline std::filesystem::path g_path;
    inline bool g_installed = false;

#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
    inline int g_fd = -1;
    inline constexpr int FATAL_SIGNALS[] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE };
    inline struct sigaction g_previousActions[sizeof(FATAL_SIGNALS) / sizeof(FATAL_SIGNALS[0])]{};
    inline bool g_signalInstalled[sizeof(FATAL_SIGNALS) / sizeof(FATAL_SIGNALS[0])]{};
    alignas(16) inline unsigned char g_alternateStack[64 * 1024]{};
#else
    inline std::ofstream g_stream;
#endif

    inline size_t boundedLength(const char* value, size_t limit) {
        if (!value) return 0;
        size_t count = 0;
        while (count < limit && value[count]) ++count;
        return count;
    }

#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
    inline void rawWrite(const char* data, size_t length) {
        if (g_fd < 0 || !data) return;
        while (length > 0) {
            auto count = ::write(g_fd, data, length);
            if (count <= 0) return;
            data += count;
            length -= static_cast<size_t>(count);
        }
    }
    inline void rawWriteText(const char* value, size_t limit) {
        rawWrite(value, boundedLength(value, limit));
    }
    inline void rawWriteSigned(long long value) {
        char buffer[32];
        size_t length = 0;
        bool negative = value < 0;
        unsigned long long magnitude = negative
            ? static_cast<unsigned long long>(-(value + 1)) + 1ULL
            : static_cast<unsigned long long>(value);
        do {
            buffer[length++] = static_cast<char>('0' + magnitude % 10ULL);
            magnitude /= 10ULL;
        } while (magnitude && length < sizeof(buffer) - 1);
        if (negative && length < sizeof(buffer)) buffer[length++] = '-';
        while (length > 0) rawWrite(&buffer[--length], 1);
    }
    inline void rawWriteHex(uintptr_t value) {
        static constexpr char HEX[] = "0123456789abcdef";
        char buffer[2 * sizeof(uintptr_t)];
        size_t length = 0;
        do {
            buffer[length++] = HEX[value & 0xf];
            value >>= 4;
        } while (value && length < sizeof(buffer));
        rawWrite("0x", 2);
        while (length > 0) rawWrite(&buffer[--length], 1);
    }
    inline void dumpRingFromSignal() {
        std::sig_atomic_t newest = g_sequence;
        std::sig_atomic_t first = newest > static_cast<std::sig_atomic_t>(EVENT_CAPACITY)
            ? newest - static_cast<std::sig_atomic_t>(EVENT_CAPACITY)
            : 0;
        static constexpr char START[] = "\n--- AFTERFLOW RECENT EVENTS (oldest to newest) ---\n";
        static constexpr char END[] = "--- END AFTERFLOW EVENTS ---\n";
        rawWrite(START, sizeof(START) - 1);
        for (std::sig_atomic_t sequence = first + 1; sequence <= newest; ++sequence) {
            auto& event = g_events[static_cast<size_t>(sequence) % EVENT_CAPACITY];
            std::sig_atomic_t length = event.length;
            if (length > 0 && length <= static_cast<std::sig_atomic_t>(EVENT_SIZE)) {
                rawWrite(event.text, static_cast<size_t>(length));
            }
        }
        rawWrite(END, sizeof(END) - 1);
    }
    inline void fatalSignalHandler(int signalNumber, siginfo_t* info, void* context) {
        if (g_inFatalHandler) {
            static constexpr char RECURSIVE[] = "\nAFTERFLOW FATAL HANDLER RE-ENTERED; exiting.\n";
            rawWrite(RECURSIVE, sizeof(RECURSIVE) - 1);
            ::_exit(128 + signalNumber);
        }
        g_inFatalHandler = 1;
        static constexpr char HEADER[] = "\n\n========== AFTERFLOW FATAL SIGNAL ==========\n";
        static constexpr char FOOTER[] = "========== PRESERVING SYSTEM CRASH HANDLER ==========\n";
        rawWrite(HEADER, sizeof(HEADER) - 1);
        rawWrite("signal=", 7);
        rawWriteSigned(signalNumber);
        rawWrite(" code=", 6);
        rawWriteSigned(info ? info->si_code : 0);
        rawWrite(" fault_address=", 15);
        rawWriteHex(reinterpret_cast<uintptr_t>(info ? info->si_addr : nullptr));
        rawWrite("\nactive_hook=", 13);
        rawWriteText(g_activeHook, 112);
        rawWrite("\npid=", 5);
        rawWriteSigned(static_cast<long long>(::getpid()));
        rawWrite("\n", 1);
        dumpRingFromSignal();
        rawWrite(FOOTER, sizeof(FOOTER) - 1);

        for (size_t index = 0; index < sizeof(FATAL_SIGNALS) / sizeof(FATAL_SIGNALS[0]); ++index) {
            if (FATAL_SIGNALS[index] != signalNumber || !g_signalInstalled[index]) continue;
            auto previous = g_previousActions[index];
            if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN && previous.sa_handler != nullptr) {
                if (previous.sa_flags & SA_SIGINFO) previous.sa_sigaction(signalNumber, info, context);
                else previous.sa_handler(signalNumber);
            }
            break;
        }
        struct sigaction defaultAction{};
        defaultAction.sa_handler = SIG_DFL;
        sigemptyset(&defaultAction.sa_mask);
        ::sigaction(signalNumber, &defaultAction, nullptr);
        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, signalNumber);
        ::sigprocmask(SIG_UNBLOCK, &unblock, nullptr);
        ::kill(::getpid(), signalNumber);
        ::_exit(128 + signalNumber);
    }
    inline void installFatalHandlers() {
        stack_t stack{};
        stack.ss_sp = g_alternateStack;
        stack.ss_size = sizeof(g_alternateStack);
        stack.ss_flags = 0;
        (void)::sigaltstack(&stack, nullptr);
        for (size_t index = 0; index < sizeof(FATAL_SIGNALS) / sizeof(FATAL_SIGNALS[0]); ++index) {
            struct sigaction action{};
            action.sa_sigaction = &fatalSignalHandler;
            sigemptyset(&action.sa_mask);
            action.sa_flags = SA_SIGINFO | SA_ONSTACK;
            if (::sigaction(FATAL_SIGNALS[index], &action, &g_previousActions[index]) == 0) {
                g_signalInstalled[index] = true;
            }
        }
    }
#endif

    inline void record(const char* message) {
        if (!g_recordingEnabled) return;
        std::sig_atomic_t next = g_sequence + 1;
        if (next <= 0) next = 1;
        auto& event = g_events[static_cast<size_t>(next) % EVENT_CAPACITY];
        int written = std::snprintf(event.text, sizeof(event.text), "[%08d] %s\n",
            static_cast<int>(next), message ? message : "<null>");
        if (written < 0) {
            event.length = 0;
            return;
        }
        size_t length = static_cast<size_t>(written);
        if (length >= sizeof(event.text)) length = sizeof(event.text) - 1;
        event.length = static_cast<std::sig_atomic_t>(length);
        g_sequence = next;
    }
    inline void recordf(const char* format, ...) {
        if (!g_recordingEnabled) return;
        char message[EVENT_SIZE];
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(message, sizeof(message), format ? format : "<null format>", arguments);
        va_end(arguments);
        record(message);
    }
    inline void setRecordingEnabled(bool enabled) {
        bool wasEnabled = g_recordingEnabled != 0;
        g_recordingEnabled = enabled ? 1 : 0;
        if (!enabled) {
            g_sequence = 0;
            g_lastFlushed = 0;
            for (auto& event : g_events) event.length = 0;
        }
        else if (!wasEnabled) {
            record("PERFORMANCE DIAGNOSTICS ENABLED");
        }
    }
    inline void setActive(const char* hookName) {
        g_activeHook = hookName ? hookName : "<null>";
    }
    class ActiveScope {
    public:
        explicit ActiveScope(const char* hookName) : m_previous(g_activeHook) { setActive(hookName); }
        ActiveScope(const ActiveScope&) = delete;
        ActiveScope& operator=(const ActiveScope&) = delete;
        ~ActiveScope() { setActive(m_previous); }
    private:
        const char* m_previous;
    };
    inline void flush() {
        if (!g_recordingEnabled) return;
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
        if (g_fd < 0) return;
        std::sig_atomic_t newest = g_sequence;
        std::sig_atomic_t first = g_lastFlushed + 1;
        std::sig_atomic_t earliest = newest > static_cast<std::sig_atomic_t>(EVENT_CAPACITY)
            ? newest - static_cast<std::sig_atomic_t>(EVENT_CAPACITY) + 1
            : 1;
        if (first < earliest) first = earliest;
        for (std::sig_atomic_t sequence = first; sequence <= newest; ++sequence) {
            auto& event = g_events[static_cast<size_t>(sequence) % EVENT_CAPACITY];
            std::sig_atomic_t length = event.length;
            if (length > 0 && length <= static_cast<std::sig_atomic_t>(EVENT_SIZE)) {
                rawWrite(event.text, static_cast<size_t>(length));
            }
        }
        g_lastFlushed = newest;
#else
        if (!g_stream.is_open()) return;
        std::sig_atomic_t newest = g_sequence;
        std::sig_atomic_t first = g_lastFlushed + 1;
        std::sig_atomic_t earliest = newest > static_cast<std::sig_atomic_t>(EVENT_CAPACITY)
            ? newest - static_cast<std::sig_atomic_t>(EVENT_CAPACITY) + 1
            : 1;
        if (first < earliest) first = earliest;
        for (std::sig_atomic_t sequence = first; sequence <= newest; ++sequence) {
            auto& event = g_events[static_cast<size_t>(sequence) % EVENT_CAPACITY];
            if (event.length > 0) g_stream.write(event.text, event.length);
        }
        g_stream.flush();
        g_lastFlushed = newest;
#endif
    }
    inline void cleanExit() {
        record("PROCESS_ATEXIT: clean process exit");
        flush();
    }
    inline void install(std::filesystem::path const& logPath) {
        if (g_installed) return;
        g_installed = true;
        g_path = logPath;
        std::error_code error;
        std::filesystem::create_directories(g_path.parent_path(), error);
        auto fileSize = std::filesystem::file_size(g_path, error);
        if (!error && fileSize > MAX_LOG_BYTES) {
            auto previousPath = g_path;
            previousPath += ".previous";
            std::filesystem::remove(previousPath, error);
            error.clear();
            std::filesystem::rename(g_path, previousPath, error);
        }
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
        g_fd = ::open(g_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        installFatalHandlers();
#else
        g_stream.open(g_path, std::ios::out | std::ios::app | std::ios::binary);
#endif
        std::atexit(&cleanExit);
        record("AFTERFLOW DIAGNOSTICS READY");
        flush();
    }
    inline std::string exportText() {
        flush();
        constexpr size_t MAX_EXPORT_BYTES = 96 * 1024;
        std::ifstream input(g_path, std::ios::binary);
        if (!input) {
            return "No Afterflow diagnostics file exists yet. Enable Performance diagnostics and reproduce the lag spike.\n";
        }
        std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (contents.size() > MAX_EXPORT_BYTES) {
            contents.erase(0, contents.size() - MAX_EXPORT_BYTES);
            contents.insert(0, "[TRUNCATED: showing the most recent diagnostics]\n");
        }
        return contents.empty()
            ? std::string("Diagnostics file is empty. Enable Performance diagnostics and reproduce the lag spike.\n")
            : contents;
    }
    inline std::string filePath() { return g_path.string(); }
}
