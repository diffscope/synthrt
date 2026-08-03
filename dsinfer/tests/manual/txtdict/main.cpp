#include <chrono>
#include <utility>

#include <stdcorelib/system.h>
#include <stdcorelib/console.h>
#include <stdcorelib/vla.h>
#include <stdcorelib/path.h>
#include <stdcorelib/str.h>

#include <dsinfer/Support/PhonemeDict.h>

int main(int /*argc*/, char * /*argv*/[]) {
    auto cmdline = stdc::system::command_line_arguments();
    if (cmdline.size() < 2) {
        stdc::u8println("Usage: %1 <dict> [count] [keys...]", stdc::system::application_name());
        return 1;
    }

    // Parse arg
    const auto &filepath = stdc::path::from_utf8(cmdline[1]);
    int len = cmdline.size() >= 3 ? std::stoi(cmdline[2]) : 1;

    // Record start time
    auto start_time = std::chrono::high_resolution_clock::now();

    // Load file
    std::vector<ds::PhonemeDict> dicts(len);
    for (auto &dict : dicts) {
        if (std::error_code ec; !dict.load(filepath, &ec)) {
            stdc::console::critical("Failed to read dictionary \"%1\": %2", filepath, ec.value());
            return EXIT_FAILURE;
        }
    }

    // Record end time
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    stdc::console::success("Timeout: %1ms", duration.count());

    stdc::u8println("Press Enter to continue...");
    std::ignore = std::getchar();

    // Print first 10 entries of first dictionary
    stdc::u8println("First 10 entries of \"%1\":", filepath);
    {
        int i = 0;
        for (const auto &pair : std::as_const(dicts[0])) {
            stdc::u8println("%1: %2", pair.first, stdc::join(pair.second.vec(), " "));
            i++;
            if (i == 10) {
                break;
            }
        }
    }
    stdc::u8println();

    // Print last 10 entries of first dictionary
    stdc::u8println("Last 10 entries of \"%1\":", filepath);
    {
        int i = 0;
        for (auto it = dicts[0].rbegin(); it != dicts[0].rend(); ++it) {
            auto &pair = *it;
            stdc::u8println("%1: %2", pair.first, stdc::join(pair.second.vec(), " "));
            i++;
            if (i == 10) {
                break;
            }
        }
    }
    stdc::u8println();

    // Test find
    stdc::u8println("Find specified entries:");
    if (cmdline.size() >= 4) {
        for (int i = 3; i < cmdline.size(); i++) {
            const auto &key = cmdline[i];
            auto it = dicts[0].find(key.c_str());
            if (it == dicts[0].end()) {
                stdc::u8println("%1: NOT FOUND", key);
            } else {
                stdc::u8println("%1: %2", key, stdc::join(it->second.vec(), " "));
            }
        }
    }
    return 0;
}