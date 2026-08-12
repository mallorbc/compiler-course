#include "NativeToolchain.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <optional>
#include <spawn.h>
#include <string>
#include <system_error>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace
{

NativeToolchainResult failure(NativeToolchainStatus status, const std::string &diagnostic)
{
    NativeToolchainResult result;
    result.status = status;
    result.diagnostic = diagnostic;
    return result;
}

bool inspect_direct_output(const std::filesystem::path &path, bool &missing)
{
    struct stat status{};
    if (::lstat(path.c_str(), &status) == 0)
    {
        missing = false;
        return S_ISREG(status.st_mode);
    }
    missing = errno == ENOENT;
    return missing;
}

enum class AliasResult
{
    Distinct,
    Alias,
    Error
};

AliasResult paths_alias(const std::filesystem::path &source,
                        const std::filesystem::path &output,
                        bool output_missing)
{
    std::error_code error;
    if (!output_missing)
    {
        const bool equivalent = std::filesystem::equivalent(source, output, error);
        if (error)
        {
            return AliasResult::Error;
        }
        return equivalent ? AliasResult::Alias : AliasResult::Distinct;
    }
    const std::filesystem::path canonical_source =
        std::filesystem::weakly_canonical(source, error);
    if (error)
    {
        return AliasResult::Error;
    }
    error.clear();
    const std::filesystem::path canonical_output =
        std::filesystem::weakly_canonical(output, error);
    if (error)
    {
        return AliasResult::Error;
    }
    return canonical_source == canonical_output ? AliasResult::Alias : AliasResult::Distinct;
}

std::optional<std::filesystem::path> resolved_compiler(const std::string &compiler)
{
    if (compiler.find('/') != std::string::npos)
    {
        const std::filesystem::path candidate(compiler);
        struct stat status{};
        if (::stat(candidate.c_str(), &status) == 0 && S_ISREG(status.st_mode) &&
            ::access(candidate.c_str(), X_OK) == 0)
        {
            return candidate;
        }
        return std::nullopt;
    }
    const char *configured_path = std::getenv("PATH");
    std::string search_path;
    if (configured_path != nullptr)
    {
        search_path = configured_path;
    }
    else
    {
        const std::size_t length = ::confstr(_CS_PATH, nullptr, 0);
        if (length != 0U)
        {
            std::vector<char> buffer(length);
            ::confstr(_CS_PATH, buffer.data(), buffer.size());
            search_path = buffer.data();
        }
    }
    std::size_t begin = 0;
    while (begin <= search_path.size())
    {
        const std::size_t end = search_path.find(':', begin);
        const std::string entry = search_path.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        const std::filesystem::path candidate =
            (entry.empty() ? std::filesystem::path(".") : std::filesystem::path(entry)) /
            compiler;
        struct stat status{};
        if (::stat(candidate.c_str(), &status) == 0 && S_ISREG(status.st_mode) &&
            ::access(candidate.c_str(), X_OK) == 0)
        {
            return candidate;
        }
        if (end == std::string::npos)
        {
            break;
        }
        begin = end + 1U;
    }
    return std::nullopt;
}

bool validate_destination(const std::filesystem::path &source,
                          const std::filesystem::path &output,
                          const std::optional<std::filesystem::path> &compiler,
                          std::string &diagnostic)
{
    struct stat source_status{};
    if (::stat(source.c_str(), &source_status) != 0 || !S_ISREG(source_status.st_mode))
    {
        diagnostic = "source path is no longer a regular file";
        return false;
    }
    if (output.empty() || output.filename().empty())
    {
        diagnostic = "output path is empty";
        return false;
    }
    const std::filesystem::path parent = output.parent_path().empty() ?
        std::filesystem::path(".") : output.parent_path();
    struct stat parent_status{};
    if (::stat(parent.c_str(), &parent_status) != 0 || !S_ISDIR(parent_status.st_mode))
    {
        diagnostic = "output parent directory is unavailable";
        return false;
    }
    bool missing = false;
    if (!inspect_direct_output(output, missing))
    {
        diagnostic = "output path is not a direct regular file";
        return false;
    }
    const AliasResult source_alias = paths_alias(source, output, missing);
    if (source_alias == AliasResult::Error)
    {
        diagnostic = "cannot establish that output is distinct from source";
        return false;
    }
    if (source_alias == AliasResult::Alias)
    {
        diagnostic = "output aliases source";
        return false;
    }
    if (compiler)
    {
        const AliasResult compiler_alias = paths_alias(*compiler, output, missing);
        if (compiler_alias == AliasResult::Error)
        {
            diagnostic = "cannot establish that output is distinct from host compiler";
            return false;
        }
        if (compiler_alias == AliasResult::Alias)
        {
            diagnostic = "output aliases host compiler";
            return false;
        }
    }
    return true;
}

class ReservedDirectory
{
public:
    ReservedDirectory() = default;
    ReservedDirectory(const ReservedDirectory &) = delete;
    ReservedDirectory &operator=(const ReservedDirectory &) = delete;

    ~ReservedDirectory()
    {
        if (!path_.empty())
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }

    bool reserve(const std::filesystem::path &parent)
    {
        std::error_code error;
        const std::filesystem::path absolute_parent = std::filesystem::absolute(parent, error);
        if (error)
        {
            return false;
        }
        std::string pattern = (absolute_parent / ".compiler-native-tmp-XXXXXX").string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        char *created = ::mkdtemp(writable.data());
        if (created == nullptr)
        {
            return false;
        }
        path_ = std::filesystem::path(created);
        if (::chmod(path_.c_str(), S_IRWXU) != 0)
        {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
            path_.clear();
            return false;
        }
        return true;
    }

    const std::filesystem::path &path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

bool write_all(int descriptor, const std::string &text)
{
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const std::size_t remaining = text.size() - offset;
        const std::size_t maximum = static_cast<std::size_t>(SSIZE_MAX);
        const std::size_t count = remaining < maximum ? remaining : maximum;
        const ssize_t written = ::write(descriptor, text.data() + offset, count);
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (written == 0)
        {
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

bool write_private_source(const std::filesystem::path &path, const std::string &text)
{
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                                  S_IRUSR | S_IWUSR);
    if (descriptor < 0)
    {
        return false;
    }
    if (::fchmod(descriptor, S_IRUSR | S_IWUSR) != 0)
    {
        ::close(descriptor);
        return false;
    }
    const bool written = write_all(descriptor, text);
    const bool closed = ::close(descriptor) == 0;
    return written && closed;
}

bool validate_links(const RestrictedCResult &emitted, bool &link_math)
{
    link_math = false;
    for (RestrictedCLink link : emitted.links)
    {
        switch (link)
        {
        case RestrictedCLink::Math:
            link_math = true;
            break;
        default:
            return false;
        }
    }
    return true;
}

NativeToolchainResult run_compiler(const std::filesystem::path &source,
                                   const std::filesystem::path &executable,
                                   const std::string &host_compiler,
                                   bool link_math)
{
    std::vector<std::string> arguments;
    arguments.push_back(host_compiler);
    arguments.push_back("-std=c11");
    arguments.push_back(source.string());
    arguments.push_back("-o");
    arguments.push_back(executable.string());
    if (link_math)
    {
        arguments.push_back("-lm");
    }
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1U);
    for (std::string &argument : arguments)
    {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    const int actions_initialized = ::posix_spawn_file_actions_init(&actions);
    if (actions_initialized != 0)
    {
        return failure(NativeToolchainStatus::LaunchError,
                       "cannot prepare host C compiler process");
    }
    const int stdin_action = ::posix_spawn_file_actions_addopen(
        &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (stdin_action != 0)
    {
        ::posix_spawn_file_actions_destroy(&actions);
        return failure(NativeToolchainStatus::LaunchError,
                       "cannot isolate host C compiler input");
    }
    pid_t child = -1;
    const int launched = ::posix_spawnp(&child, host_compiler.c_str(), &actions, nullptr,
                                        argv.data(), environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (launched != 0)
    {
        return failure(NativeToolchainStatus::LaunchError,
                       "cannot launch host C compiler: " +
                       std::string(std::strerror(launched)));
    }
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = ::waitpid(child, &status, 0);
    }
    while (waited < 0 && errno == EINTR);
    if (waited < 0)
    {
        return failure(NativeToolchainStatus::LaunchError,
                       "cannot wait for host C compiler");
    }
    if (WIFSIGNALED(status))
    {
        return failure(NativeToolchainStatus::CompilerError,
                       "host C compiler terminated by signal " +
                       std::to_string(WTERMSIG(status)));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        const int exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return failure(NativeToolchainStatus::CompilerError,
                       "host C compiler exited with status " +
                       std::to_string(exit_status));
    }
    return NativeToolchainResult{NativeToolchainStatus::Success, std::string()};
}

bool direct_regular_executable(const std::filesystem::path &path)
{
    struct stat status{};
    return ::lstat(path.c_str(), &status) == 0 && S_ISREG(status.st_mode) &&
           status.st_size > 0 && status.st_nlink == 1 &&
           (status.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0 &&
           ::access(path.c_str(), X_OK) == 0;
}

} // namespace

NativeToolchainResult NativeToolchain::compile(const RestrictedCResult &emitted,
                                               const std::filesystem::path &source,
                                               const std::filesystem::path &output,
                                               const std::string &host_compiler) const
{
    if (!emitted.succeeded() || emitted.text.empty())
    {
        return failure(NativeToolchainStatus::InvalidInput,
                       "native compilation requires successful emitted C");
    }
    if (host_compiler.empty())
    {
        return failure(NativeToolchainStatus::InvalidInput,
                       "host C compiler name is empty");
    }
    bool link_math = false;
    if (!validate_links(emitted, link_math))
    {
        return failure(NativeToolchainStatus::InvalidInput,
                       "emitted C contains an unknown link dependency");
    }
    const std::optional<std::filesystem::path> compiler_path =
        resolved_compiler(host_compiler);
    if (!compiler_path)
    {
        return failure(NativeToolchainStatus::LaunchError,
                       "cannot resolve host C compiler");
    }
    std::string diagnostic;
    if (!validate_destination(source, output, compiler_path, diagnostic))
    {
        return failure(NativeToolchainStatus::IoError, diagnostic);
    }

    const std::filesystem::path parent = output.parent_path().empty() ?
        std::filesystem::path(".") : output.parent_path();
    ReservedDirectory temporary;
    if (!temporary.reserve(parent))
    {
        return failure(NativeToolchainStatus::IoError,
                       "cannot reserve a sibling temporary directory");
    }
    const std::filesystem::path c_source = temporary.path() / "source.c";
    const std::filesystem::path executable = temporary.path() / "program";
    if (!write_private_source(c_source, emitted.text))
    {
        return failure(NativeToolchainStatus::IoError,
                       "cannot write temporary C source");
    }
    NativeToolchainResult compiled = run_compiler(c_source, executable, host_compiler,
                                                  link_math);
    if (!compiled.succeeded())
    {
        return compiled;
    }
    if (!direct_regular_executable(executable))
    {
        return failure(NativeToolchainStatus::CompilerError,
                       "host C compiler did not produce a regular executable");
    }
    if (!validate_destination(source, output, compiler_path, diagnostic))
    {
        return failure(NativeToolchainStatus::IoError, diagnostic);
    }
    if (::rename(executable.c_str(), output.c_str()) != 0)
    {
        return failure(NativeToolchainStatus::IoError,
                       "cannot publish executable atomically");
    }
    return NativeToolchainResult{NativeToolchainStatus::Success, std::string()};
}
