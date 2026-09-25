/* ============================================================================
   FORRÓ BOX — exit probe (test binary only). See ExitProbe.h.

   Deliberately NOT including JuceHeader.h: this file needs <windows.h> and
   <dbghelp.h> in full, and keeping them out of the JUCE translation units is
   cheaper than reconciling the two.
============================================================================ */
#include "ExitProbe.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <thread>
 #include <windows.h>
 #include <tlhelp32.h>
 #include <dbghelp.h>
 #pragma comment (lib, "dbghelp.lib")
#endif

namespace fbtest::exitprobe
{
    bool enabled()
    {
        static const bool on = []
        {
           #if defined (_WIN32)
            // Not getenv: MSVC flags it C4996, and this project ships no warnings
            // from its own sources. Read from WSL, the variable only arrives if
            // WSLENV names it — scripts/build-windows.sh does.
            char value[8] {};
            const DWORD length = GetEnvironmentVariableA ("FORROBOX_EXIT_PROBE", value, sizeof (value));
            return length > 0 && length < sizeof (value) && std::strcmp (value, "0") != 0;
           #else
            const char* value = std::getenv ("FORROBOX_EXIT_PROBE");
            return value != nullptr && value[0] != '\0' && std::strcmp (value, "0") != 0;
           #endif
        }();
        return on;
    }

    namespace
    {
        /** Straight to the OS handle, bypassing the CRT's stream and its lock. The
            watchdog writes while other threads are frozen, and one of them may
            hold that lock. */
        void writeRaw (const char* text, size_t length)
        {
           #if defined (_WIN32)
            DWORD written = 0;
            WriteFile (GetStdHandle (STD_ERROR_HANDLE), text, static_cast<DWORD> (length), &written, nullptr);
           #else
            std::fwrite (text, 1, length, stderr);
            std::fflush (stderr);
           #endif
        }

        void writeLine (const char* text)
        {
            char line[512];
            const int n = std::snprintf (line, sizeof (line), "[exit-probe] %s\n", text);
            if (n > 0)
                writeRaw (line, static_cast<size_t> (n) < sizeof (line) ? static_cast<size_t> (n) : sizeof (line) - 1);
        }

        struct StaticStage
        {
            ~StaticStage() { writeLine ("static destructors running (after main returned)"); }
        };

       #if defined (_WIN32)
        constexpr int kMaxFrames  = 48;
        constexpr int kMaxThreads = 64;

        struct ThreadStack
        {
            DWORD   id = 0;
            int     frames = 0;
            DWORD64 pc[kMaxFrames] {};
        };

        /** Walks one thread while it is suspended, recording raw addresses only.
            Symbols are resolved AFTER it resumes, so nothing that can allocate
            runs while another thread is frozen mid-way through who knows what. */
        void captureStack (HANDLE process, DWORD threadId, ThreadStack& out)
        {
            out.id = threadId;
            HANDLE thread = OpenThread (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                        FALSE, threadId);
            if (thread == nullptr)
                return;

            if (SuspendThread (thread) != static_cast<DWORD> (-1))
            {
                CONTEXT context {};
                context.ContextFlags = CONTEXT_FULL;

                if (GetThreadContext (thread, &context))
                {
                    STACKFRAME64 frame {};
                    frame.AddrPC.Offset    = context.Rip;
                    frame.AddrPC.Mode      = AddrModeFlat;
                    frame.AddrFrame.Offset = context.Rbp;
                    frame.AddrFrame.Mode   = AddrModeFlat;
                    frame.AddrStack.Offset = context.Rsp;
                    frame.AddrStack.Mode   = AddrModeFlat;

                    while (out.frames < kMaxFrames
                           && StackWalk64 (IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
                                           nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)
                           && frame.AddrPC.Offset != 0)
                    {
                        out.pc[out.frames++] = frame.AddrPC.Offset;
                    }
                }

                ResumeThread (thread);
            }

            CloseHandle (thread);
        }

        void printStack (HANDLE process, const ThreadStack& stack, DWORD mainThreadId)
        {
            char line[768];
            int n = std::snprintf (line, sizeof (line), "[exit-probe] thread %lu%s, %d frames\n",
                                   static_cast<unsigned long> (stack.id),
                                   stack.id == mainThreadId ? " (MAIN)" : "", stack.frames);
            writeRaw (line, static_cast<size_t> (n));

            alignas (SYMBOL_INFO) char symbolBuffer[sizeof (SYMBOL_INFO) + 256] {};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*> (symbolBuffer);

            for (int i = 0; i < stack.frames; ++i)
            {
                std::memset (symbolBuffer, 0, sizeof (symbolBuffer));
                symbol->SizeOfStruct = sizeof (SYMBOL_INFO);
                symbol->MaxNameLen   = 255;

                IMAGEHLP_MODULE64 module {};
                module.SizeOfStruct = sizeof (module);
                const char* moduleName = SymGetModuleInfo64 (process, stack.pc[i], &module) ? module.ModuleName : "?";

                DWORD64 displacement = 0;
                const char* name = SymFromAddr (process, stack.pc[i], &displacement, symbol) ? symbol->Name : "?";

                IMAGEHLP_LINE64 source {};
                source.SizeOfStruct = sizeof (source);
                DWORD column = 0;

                if (SymGetLineFromAddr64 (process, stack.pc[i], &column, &source))
                    n = std::snprintf (line, sizeof (line), "    #%02d %s!%s+0x%llx  %s:%lu\n", i, moduleName, name,
                                       static_cast<unsigned long long> (displacement), source.FileName,
                                       static_cast<unsigned long> (source.LineNumber));
                else
                    n = std::snprintf (line, sizeof (line), "    #%02d %s!%s+0x%llx\n", i, moduleName, name,
                                       static_cast<unsigned long long> (displacement));

                if (n > 0)
                    writeRaw (line, static_cast<size_t> (n) < sizeof (line) ? static_cast<size_t> (n) : sizeof (line) - 1);
            }
        }

        void dumpEveryOtherThreadAndDie (DWORD mainThreadId, unsigned seconds)
        {
            char line[256];
            const int n = std::snprintf (line, sizeof (line),
                                         "[exit-probe] WATCHDOG: still alive %u s after the summary — dumping every thread\n",
                                         seconds);
            writeRaw (line, static_cast<size_t> (n));

            HANDLE process = GetCurrentProcess();
            SymSetOptions (SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
            SymInitialize (process, nullptr, TRUE);

            static ThreadStack stacks[kMaxThreads];
            int count = 0;
            const DWORD self = GetCurrentThreadId();

            HANDLE snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);
            if (snapshot != INVALID_HANDLE_VALUE)
            {
                THREADENTRY32 entry {};
                entry.dwSize = sizeof (entry);

                for (BOOL more = Thread32First (snapshot, &entry); more && count < kMaxThreads;
                     more = Thread32Next (snapshot, &entry))
                {
                    if (entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID != self)
                        captureStack (process, entry.th32ThreadID, stacks[count++]);
                }

                CloseHandle (snapshot);
            }

            for (int i = 0; i < count; ++i)
                printStack (process, stacks[i], mainThreadId);

            writeLine ("WATCHDOG: terminating with the watchdog exit code — a hang is not a pass");
            TerminateProcess (process, static_cast<UINT> (kWatchdogExitCode));
        }
       #endif
    }

    void mark (const char* stage)
    {
        if (enabled())
            writeLine (stage);
    }

    void installLateMarkers()
    {
        if (! enabled())
            return;

        static StaticStage lastStatic;
        std::atexit ([] { writeLine ("atexit handlers running (CRT exit has begun)"); });
    }

    void armWatchdog (unsigned seconds)
    {
       #if defined (_WIN32)
        if (! enabled())
            return;

        const DWORD mainThreadId = GetCurrentThreadId();
        std::thread ([mainThreadId, seconds]
        {
            Sleep (static_cast<DWORD> (seconds) * 1000);
            dumpEveryOtherThreadAndDie (mainThreadId, seconds);
        }).detach();

        writeLine ("watchdog armed");
       #else
        (void) seconds;
       #endif
    }
}
