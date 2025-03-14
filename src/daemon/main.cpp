//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "engine/config.hpp"
#include "engine/engine.hpp"
#include "setup_logging.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <signal.h>  // NOLINT
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{

const auto* const s_init_complete = "init_complete";

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile sig_atomic_t g_running = 1;

extern "C" void signalHandler(const int sig)
{
    switch (sig)
    {
    case SIGTERM:
    case SIGINT:
        g_running = 0;
        break;
    default:
        break;
    }
}

void setupSignalHandlers()
{
    struct sigaction sigbreak
    {};
    sigbreak.sa_handler = &signalHandler;
    ::sigaction(SIGINT, &sigbreak, nullptr);
    ::sigaction(SIGTERM, &sigbreak, nullptr);
}

void exitWithFailure(const int fd, const char* const msg)
{
    const char* const err_txt = std::strerror(errno);
    writeString(fd, msg);
    writeString(fd, err_txt);
    ::exit(EXIT_FAILURE);
}

void step_01_close_all_file_descriptors(std::array<int, 2>& pipe_fds)
{
    rlimit rlimit_files{};
    if (getrlimit(RLIMIT_NOFILE, &rlimit_files) != 0)
    {
        const char* const err_txt = std::strerror(errno);
        std::cerr << "Failed to getrlimit(RLIMIT_NOFILE): " << err_txt << "\n";
        ::exit(EXIT_FAILURE);
    }
    constexpr int first_fd_to_close = 3;  // 0, 1 & 2 for standard input, output, and error.
    for (int fd = first_fd_to_close; fd <= rlimit_files.rlim_cur; ++fd)
    {
        (void) ::close(fd);
    }

    // Create a pipe to communicate with the original process.
    //
    if (::pipe(pipe_fds.data()) == -1)
    {
        const char* const err_txt = std::strerror(errno);
        std::cerr << "Failed to create pipe: " << err_txt << "\n";
        ::exit(EXIT_FAILURE);
    }
}

void step_02_03_setup_signal_handlers()
{
    setupSignalHandlers();
}

void step_04_sanitize_environment()
{
    // Not sure what to sanitize exactly.
}

bool step_05_fork_to_background(std::array<int, 2>& pipe_fds)
{
    // Fork off the parent process
    const pid_t parent_pid = fork();
    if (parent_pid < 0)
    {
        const char* const err_txt = std::strerror(errno);
        std::cerr << "Failed to fork: " << err_txt << "\n";
        ::exit(EXIT_FAILURE);
    }

    if (parent_pid == 0)
    {
        // Close read end on the child side.
        ::close(pipe_fds[0]);
        pipe_fds[0] = -1;
    }
    else
    {
        // Close write end on the parent side.
        ::close(pipe_fds[1]);
        pipe_fds[1] = -1;
    }

    return parent_pid == 0;
}

void step_06_create_new_session(const int pipe_write_fd)
{
    if (::setsid() < 0)
    {
        exitWithFailure(pipe_write_fd, "Failed to setsid: ");
    }
}

void step_07_08_fork_and_exit_again(int& pipe_write_fd)
{
    assert(pipe_write_fd != -1);

    // Fork off the parent process
    const pid_t pid = fork();
    if (pid < 0)
    {
        exitWithFailure(pipe_write_fd, "Failed to fork: ");
    }
    if (pid > 0)
    {
        ::close(pipe_write_fd);
        pipe_write_fd = -1;
        ::exit(EXIT_SUCCESS);
    }
}

void step_09_redirect_stdio_to_devnull(const int pipe_write_fd)
{
    const int fd = ::open("/dev/null", O_RDWR);  // NOLINT *-vararg
    if (fd == -1)
    {
        exitWithFailure(pipe_write_fd, "Failed to open(/dev/null): ");
    }

    ::dup2(fd, STDIN_FILENO);
    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);

    if (fd > 2)
    {
        ::close(fd);
    }
}

void step_10_reset_umask()
{
    ::umask(0);
}

void step_11_change_curr_dir(const int pipe_write_fd)
{
    if (::chdir("/") != 0)
    {
        exitWithFailure(pipe_write_fd, "Failed to chdir(/): ");
    }
}

void step_12_create_pid_file(const int pipe_write_fd)
{
    const int fd = ::open("/var/run/ocvsmd.pid", O_RDWR | O_CREAT, 0644);  // NOLINT *-vararg
    if (fd == -1)
    {
        exitWithFailure(pipe_write_fd, "Failed to create on PID file: ");
    }

    if (::lockf(fd, F_TLOCK, 0) == -1)
    {
        exitWithFailure(pipe_write_fd, "Failed to lock PID file: ");
    }

    if (::ftruncate(fd, 0) != 0)
    {
        exitWithFailure(pipe_write_fd, "Failed to ftruncate PID file: ");
    }

    constexpr std::size_t             max_pid_str_len = 32;
    std::array<char, max_pid_str_len> buf{};
    const auto len = ::snprintf(buf.data(), buf.size(), "%ld\n", static_cast<long>(::getpid()));  // NOLINT *-vararg
    if (::write(fd, buf.data(), len) != len)
    {
        exitWithFailure(pipe_write_fd, "Failed to write to PID file: ");
    }

    // Keep the PID file open until the process exits.
}

void step_13_drop_privileges()
{
    // Not sure what to drop exactly.
}

void step_14_notify_init_complete(int& pipe_write_fd)
{
    assert(pipe_write_fd != -1);

    // From the daemon process, notify the original process started that initialization is complete. This can be
    // implemented via an unnamed pipe or similar communication channel created before the first fork() and
    // hence available in both the original and the daemon process.

    // Closing the writing end of the pipe will signal the original process that the daemon is ready.
    writeString(pipe_write_fd, s_init_complete);
    ::close(pipe_write_fd);
    pipe_write_fd = -1;
}

void step_15_exit_org_process(int& pipe_read_fd)
{
    // Call exit() in the original process. The process that invoked the daemon must be able to rely on that this exit()
    // happens after initialization is complete and all external communication channels are established and accessible.

    constexpr std::size_t      buf_size = 1024;
    std::array<char, buf_size> msg_from_child{};
    const auto                 res = ::read(pipe_read_fd, msg_from_child.data(), msg_from_child.size() - 1);
    if (res == -1)
    {
        const char* const err_txt = std::strerror(errno);
        std::cerr << "Failed to read pipe: " << err_txt << "\n";
        ::exit(EXIT_FAILURE);
    }
    msg_from_child[res] = '\0';  // NOLINT

    if (::strcmp(msg_from_child.data(), s_init_complete) != 0)
    {
        std::cerr << "Child init failed: " << msg_from_child.data() << "\n";
        ::exit(EXIT_FAILURE);
    }

    ::close(pipe_read_fd);
    pipe_read_fd = -1;
    ::exit(EXIT_SUCCESS);
}

/// Implements the daemonization procedure as described in the `man 7 daemon` manual page.
///
int daemonize()
{
    std::array<int, 2> pipe_fds{-1, -1};

    step_01_close_all_file_descriptors(pipe_fds);
    step_02_03_setup_signal_handlers();
    step_04_sanitize_environment();
    if (step_05_fork_to_background(pipe_fds))
    {
        // Child process.
        assert(pipe_fds[0] == -1);
        assert(pipe_fds[1] != -1);
        auto& pipe_write_fd = pipe_fds[1];

        step_06_create_new_session(pipe_write_fd);
        step_07_08_fork_and_exit_again(pipe_write_fd);
        step_09_redirect_stdio_to_devnull(pipe_write_fd);
        step_10_reset_umask();
        step_11_change_curr_dir(pipe_write_fd);
        step_12_create_pid_file(pipe_write_fd);
        step_13_drop_privileges();

        // `step_14_notify_init_complete(pipe_write_fd);` will be called by the main
        // when the engine has been successfully initialized.
        return pipe_write_fd;
    }

    // Original parent process.
    assert(pipe_fds[0] != -1);
    assert(pipe_fds[1] == -1);
    auto& pipe_read_fd = pipe_fds[0];

    step_15_exit_org_process(pipe_read_fd);
    return -1;  // Unreachable actually b/c of `::exit` call.
}

ocvsmd::daemon::engine::Config::Ptr loadConfig(const int          err_fd,
                                               const bool         is_daemonized,
                                               const int          argc,
                                               const char** const argv)
{
    static const std::string cfg_file_name      = "ocvsmd.toml";
    static const std::string config_file_prefix = "CONFIG_FILE=";

    const std::string cfg_file_dir  = is_daemonized ? "/etc/ocvsmd/" : "./";
    auto              cfg_file_path = cfg_file_dir + cfg_file_name;
    for (int i = 1; i < argc; i++)
    {
        const std::string arg_str = argv[i];  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        if (0 == arg_str.compare(0, config_file_prefix.size(), config_file_prefix))
        {
            cfg_file_path = arg_str.substr(config_file_prefix.size());
        }
    }

    try
    {
        return ocvsmd::daemon::engine::Config::make(cfg_file_path);

    } catch (const std::exception& ex)
    {
        std::stringstream ss;
        ss << "Failed to load configuration file (path='" << cfg_file_path << "').\n" << ex.what();
        writeString(err_fd, ss.str().c_str());

    } catch (...)
    {
        writeString(err_fd, "Failed to load configuration file.");
    }
    ::exit(EXIT_FAILURE);
}

}  // namespace

int main(const int argc, const char** const argv)
{
    bool should_daemonize = true;
    for (int i = 1; i < argc; ++i)
    {
        if (::strcmp(argv[i], "--dev") == 0)  // NOLINT
        {
            should_daemonize = false;
        }
    }

    // If daemonizing is disabled (in dev mode) then use the standard error output for reporting,
    // otherwise use the pipe to communicate with the parent process.
    int pipe_write_fd = 2;
    if (should_daemonize)
    {
        pipe_write_fd = daemonize();
        // We are in a child process now!
    }
    else
    {
        setupSignalHandlers();
    }

    const auto config = loadConfig(pipe_write_fd, should_daemonize, argc, argv);
    setupLogging(pipe_write_fd, should_daemonize, argc, argv, config);

    spdlog::info("OCVSMD started (ver='{}.{}').", VERSION_MAJOR, VERSION_MINOR);
    int result = EXIT_SUCCESS;
    {
        try
        {
            ocvsmd::daemon::engine::Engine engine{config};
            if (const auto failure_str = engine.init())
            {
                spdlog::critical("Failed to init engine: {}", *failure_str);

                // Report the failure to the parent process (if daemonized; otherwise goes to stderr).
                writeString(pipe_write_fd, "Failed to init engine: ");
                writeString(pipe_write_fd, failure_str->c_str());
                ::exit(EXIT_FAILURE);
            }
            if (should_daemonize)
            {
                step_14_notify_init_complete(pipe_write_fd);
            }

            engine.runWhile([] { return g_running == 1; });

            config->save();

        } catch (const std::exception& ex)
        {
            spdlog::critical("Unhandled exception: {}", ex.what());
            result = EXIT_FAILURE;
        }

        if (g_running == 0)
        {
            spdlog::debug("Received termination signal.");
        }
    }
    spdlog::info("OCVSMD daemon terminated.");

    return result;
}
