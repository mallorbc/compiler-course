#include "../vendor/doctest.h"
#include "../../NativeToolchain.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{

class native_temp_directory
{
public:
    native_temp_directory()
    {
        const std::filesystem::path base =
            std::filesystem::temp_directory_path() / "compiler_native_unit_XXXXXX";
        std::string pattern = base.string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        char *created = ::mkdtemp(writable.data());
        REQUIRE(created != nullptr);
        path = created;
    }

    ~native_temp_directory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    std::filesystem::path path;
};

RestrictedCResult successful_c()
{
    RestrictedCResult result;
    result.status = RestrictedCStatus::Success;
    result.text = "int main(void) { return 0; }\n";
    return result;
}

void write_file(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output.is_open());
    output << text;
    output.close();
    REQUIRE(output.good());
}

} // namespace

TEST_CASE("native toolchain rejects unsuccessful emitter input before launch")
{
    native_temp_directory root;
    const std::filesystem::path source = root.path / "input.src";
    const std::filesystem::path output = root.path / "program";
    write_file(source, "source sentinel\n");
    write_file(output, "output sentinel\n");

    RestrictedCResult failed;
    failed.status = RestrictedCStatus::Unsupported;
    NativeToolchain toolchain;
    const NativeToolchainResult result = toolchain.compile(
        failed, source, output, "/definitely/missing/compiler");

    CHECK(result.status == NativeToolchainStatus::InvalidInput);
    std::ifstream preserved(output);
    std::string line;
    std::getline(preserved, line);
    CHECK(line == "output sentinel");
}

TEST_CASE("native toolchain rejects unknown link dependencies before reserving or launching")
{
    native_temp_directory root;
    const std::filesystem::path source = root.path / "input.src";
    const std::filesystem::path output = root.path / "program";
    const std::filesystem::path marker = root.path / "compiler-launched";
    const std::filesystem::path fake_compiler = root.path / "fake-compiler";
    write_file(source, "source sentinel\n");
    write_file(output, "output sentinel\n");
    REQUIRE(::chmod(output.c_str(), 0751) == 0);
    write_file(fake_compiler,
               "#!/bin/sh\n: > '" + marker.string() + "'\nexit 0\n");
    REQUIRE(::chmod(fake_compiler.c_str(), 0700) == 0);
    struct stat before{};
    REQUIRE(::lstat(output.c_str(), &before) == 0);

    RestrictedCResult invalid = successful_c();
    invalid.links.push_back(static_cast<RestrictedCLink>(99));
    NativeToolchain toolchain;
    const NativeToolchainResult result = toolchain.compile(
        invalid, source, output, fake_compiler.string());

    CHECK(result.status == NativeToolchainStatus::InvalidInput);
    CHECK(result.diagnostic == "emitted C contains an unknown link dependency");
    CHECK_FALSE(std::filesystem::exists(marker));
    struct stat after{};
    REQUIRE(::lstat(output.c_str(), &after) == 0);
    CHECK(after.st_ino == before.st_ino);
    CHECK((after.st_mode & 0777) == (before.st_mode & 0777));
    std::ifstream preserved(output);
    std::string line;
    std::getline(preserved, line);
    CHECK(line == "output sentinel");
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator(root.path))
    {
        CHECK(entry.path().filename().string().find(".compiler-native-tmp-") != 0U);
    }
}

TEST_CASE("native toolchain classifies launch, compiler, and missing-product failures")
{
    native_temp_directory root;
    const std::filesystem::path source = root.path / "input.src";
    const std::filesystem::path output = root.path / "program";
    write_file(source, "source sentinel\n");
    write_file(output, "output sentinel\n");
    NativeToolchain toolchain;

    const NativeToolchainResult missing = toolchain.compile(
        successful_c(), source, output, "/definitely/missing/compiler");
    CHECK(missing.status == NativeToolchainStatus::LaunchError);

    if (::access("/bin/false", X_OK) == 0)
    {
        const NativeToolchainResult failed = toolchain.compile(
            successful_c(), source, output, "/bin/false");
        CHECK(failed.status == NativeToolchainStatus::CompilerError);
    }
    if (::access("/bin/true", X_OK) == 0)
    {
        const NativeToolchainResult no_product = toolchain.compile(
            successful_c(), source, output, "/bin/true");
        CHECK(no_product.status == NativeToolchainStatus::CompilerError);
    }
    std::ifstream preserved(output);
    std::string line;
    std::getline(preserved, line);
    CHECK(line == "output sentinel");
    CHECK(std::filesystem::directory_iterator(root.path) !=
          std::filesystem::directory_iterator());
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator(root.path))
    {
        CHECK(entry.path().filename().string().find(".compiler-native-tmp-") != 0U);
    }
}

TEST_CASE("native toolchain rejects output aliases and direct symlinks")
{
    native_temp_directory root;
    const std::filesystem::path source = root.path / "input.src";
    write_file(source, "source sentinel\n");
    NativeToolchain toolchain;

    const NativeToolchainResult missing_source = toolchain.compile(
        successful_c(), root.path / "missing.src", root.path / "unused-output",
        "/bin/true");
    CHECK(missing_source.status == NativeToolchainStatus::IoError);

    const NativeToolchainResult alias = toolchain.compile(
        successful_c(), source, source, "/bin/true");
    CHECK(alias.status == NativeToolchainStatus::IoError);

    const std::filesystem::path target = root.path / "target";
    const std::filesystem::path link = root.path / "output-link";
    write_file(target, "target sentinel\n");
    REQUIRE(::symlink(target.c_str(), link.c_str()) == 0);
    const NativeToolchainResult symlink = toolchain.compile(
        successful_c(), source, link, "/bin/true");
    CHECK(symlink.status == NativeToolchainStatus::IoError);
    CHECK(std::filesystem::is_symlink(link));
}
