#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <stdcorelib/path.h>
#include <stdcorelib/system.h>

namespace fs = std::filesystem;

namespace {

    struct StagedPackage {
        fs::path source;
        fs::path destination;
        fs::path staging;
        bool committed = false;
    };

    void printUsage() {
        std::cerr << "Usage: " << stdc::system::application_name()
                  << " <install-root> <package-source>...\n"
                     "Installs unpacked Package directories. Each source directory must contain "
                     "desc.json.\n";
    }

    void removeIfCreated(const fs::path &path, bool committed) {
        if (!committed) {
            return;
        }
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

}

int main(int, char *[]) {
    const auto arguments = stdc::system::command_line_arguments();
    if (arguments.size() < 3) {
        printUsage();
        return 1;
    }

    const auto installRoot = stdc::path::from_utf8(arguments[1]);
    std::vector<StagedPackage> staged;
    staged.reserve(arguments.size() - 2);

    try {
        fs::create_directories(installRoot);

        const auto token =
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (std::size_t index = 2; index < arguments.size(); ++index) {
            const auto source = fs::absolute(stdc::path::from_utf8(arguments[index]));
            if (!fs::is_directory(source)) {
                throw std::runtime_error("package source is not a directory: " + source.string());
            }
            if (!fs::is_regular_file(source / "desc.json")) {
                throw std::runtime_error("package source has no desc.json: " + source.string());
            }

            const auto name = source.filename();
            if (name.empty()) {
                throw std::runtime_error("package source has no directory name: " +
                                         source.string());
            }
            const auto destination = installRoot / name;
            if (fs::exists(destination)) {
                throw std::runtime_error("package is already installed: " + destination.string());
            }

            const auto staging = installRoot / ("." + name.string() + ".installing-" + token);
            staged.push_back({source, destination, staging});
        }

        for (auto &package : staged) {
            fs::copy(package.source, package.staging,
                     fs::copy_options::recursive | fs::copy_options::copy_symlinks);
        }
        for (auto &package : staged) {
            fs::rename(package.staging, package.destination);
            package.committed = true;
        }

        for (const auto &package : staged) {
            std::cout << "Installed " << package.destination.string() << '\n';
        }
        return 0;
    } catch (const std::exception &exception) {
        for (const auto &package : staged) {
            std::error_code ignored;
            fs::remove_all(package.staging, ignored);
            removeIfCreated(package.destination, package.committed);
        }
        std::cerr << "Installation failed: " << exception.what() << '\n';
        return 1;
    }
}
