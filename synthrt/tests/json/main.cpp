#include <iostream>
#include <fstream>

#include <synthrt/Support/JSON.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <json-file>" << std::endl;
        return 1;
    }

    std::string json_file_path = argv[1];
    std::ifstream json_file(json_file_path);
    if (!json_file.is_open()) {
        std::cerr << "Failed to open file: " << json_file_path << std::endl;
        return 1;
    }

    std::string json_str((std::istreambuf_iterator<char>(json_file)),
                         std::istreambuf_iterator<char>());
    srt::JsonValue json = srt::JsonValue::fromJson(json_str, true);
    std::cout << "JSON: " << std::endl;
    std::cout << json.toJson(4) << std::endl;
    return 0;
}