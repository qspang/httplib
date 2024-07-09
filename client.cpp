#include "httplib.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <map>
#include <mutex>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

void upload_file(const std::string& local_file, const std::string& server_address, const std::string& server_url) {
    std::ifstream file(local_file, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << local_file << std::endl;
        return;
    }

    std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    httplib::Client cli(server_address.c_str());

    std::cout << "Uploading file to: " << server_address + server_url << std::endl;

    auto res = cli.Put(server_url.c_str(), buffer.data(), buffer.size(), "application/octet-stream");
    std::cout << "server_address: " << server_address << std::endl;
    std::cout << "server_url: " << server_url << std::endl;
    if (res) {
        std::cout << "Response status: " << res->status << std::endl;
        if (res->status == 200) {
            std::cout << "File uploaded successfully: " << local_file << std::endl;
        } else {
            std::cerr << "File upload failed: " << local_file << std::endl;
        }
    } else {
        std::cerr << "Error uploading file: " << local_file << std::endl;
    }
}

void download_file(const std::string& server_address, const std::string& server_url, const std::string& local_file) {
    httplib::Client cli(server_address.c_str());

    std::cout << "Downloading file from: " << server_address + server_url << std::endl;

    auto res = cli.Get(server_url.c_str());

    if (res) {
        std::cout << "Response status: " << res->status << std::endl;
        if (res->status == 200) {
            std::ofstream file(local_file, std::ios::binary);
            file.write(res->body.c_str(), res->body.size());
            std::cout << "File downloaded successfully: " << local_file << std::endl;
        } else {
            std::cerr << "File download failed: " << local_file << std::endl;
        }
    } else {
        std::cerr << "Error downloading file: " << local_file << std::endl;
    }
}

void concurrent_upload_download(const std::string& local_dir, const std::string& server_address, const std::string& server_base_url, int num_sessions) {
    std::vector<std::thread> threads;
    std::mutex mtx;

    for (const auto& entry : fs::recursive_directory_iterator(local_dir)) {
        if (fs::is_regular_file(entry)) {
            std::string local_file = entry.path().string();
            std::string server_url = server_base_url + entry.path().string().substr(local_dir.length());
            std::cout << "server_base_url: " << server_base_url<< std::endl;
            threads.emplace_back([=, &mtx]() {
                upload_file(local_file, server_address, server_url);
                download_file(server_address, server_url, local_file + ".downloaded");
                // 数据校验（略）
            });

            if (threads.size() >= static_cast<size_t>(num_sessions)) {
                for (auto& t : threads) {
                    t.join();
                }
                threads.clear();
            }
        }
    }

    for (auto& t : threads) {
        t.join();
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <local_dir> <server_address> <server_base_url> [num_sessions]" << std::endl;
        return 1;
    }

    std::string local_dir = argv[1];
    std::string server_address = argv[2];
    std::string server_base_url = argv[3];
    int num_sessions = argc > 4 ? std::stoi(argv[4]) : 1;

    concurrent_upload_download(local_dir, server_address, server_base_url, num_sessions);

    return 0;
}
