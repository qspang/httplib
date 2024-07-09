#include "httplib.h"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <thread>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
using json = nlohmann::json;

json read_config(const std::string& config_path) {
    std::ifstream config_file(config_path);
    json config;
    config_file >> config;
    return config;
}

void switch_user(const std::string& username) {
    struct passwd* pw = getpwnam(username.c_str());
    if (pw) {
        setuid(pw->pw_uid);
    } else {
        std::cerr << "User not found: " << username << std::endl;
        exit(1);
    }
}

class Handler {
public:
    virtual void handle_get(const httplib::Request& req, httplib::Response& res) = 0;
    virtual void handle_put(const httplib::Request& req, httplib::Response& res) = 0;
};

class FileHandler : public Handler {
public:
    FileHandler(const std::string& dir) : dir_(dir) {}

    void handle_get(const httplib::Request& req, httplib::Response& res) override {
        std::cout << "GET request path: " << req.path << std::endl;

        std::string filepath = dir_ + req.path;
        std::cout << "GET request file path: " << filepath << std::endl;

        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            res.status = 404;
            res.set_content("File not found", "text/plain");
            std::cout << "GET request for file not found: " << filepath << std::endl;
            return;
        }

        res.status = 200;
        res.set_content_provider(
            "application/octet-stream",
            [filepath](size_t offset, httplib::DataSink &sink) {
                std::ifstream file(filepath, std::ios::binary);
                file.seekg(offset);
                std::vector<char> buffer(8192);
                file.read(buffer.data(), buffer.size());
                sink.write(buffer.data(), file.gcount());
                return true;
            },
            [](bool success) {
                // Cleanup if needed
            });
        std::cout << "File served for GET request: " << filepath << std::endl;
    }

    void handle_put(const httplib::Request& req, httplib::Response& res) override {
        std::cout << "PUT request path: " << req.path << std::endl;

        std::string filepath = dir_ + req.path;
        std::cout << "PUT request file path: " << filepath << std::endl;

        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            res.status = 500;
            res.set_content("Failed to open file for writing", "text/plain");
            std::cout << "Failed to open file for writing: " << filepath << std::endl;
            return;
        }

        file.write(req.body.c_str(), req.body.size());
        res.status = 200;
        res.set_content("File uploaded successfully", "text/plain");
        std::cout << "File uploaded successfully for PUT request: " << filepath << std::endl;
    }

private:
    std::string dir_;
};

std::shared_ptr<Handler> create_handler(const std::string& handler_type, const json& params) {
    if (handler_type == "data") {
        return std::make_shared<FileHandler>(params["dir"]);
    }
    return nullptr;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config_path>" << std::endl;
        return 1;
    }

    std::string config_path = argv[1];
    json config = read_config(config_path);

    std::string address = config["address"];
    int port = config["port"];
    int max_keep_alive_count = config["max_keep_alive_count"];
    int max_concurrent_sessions = config["max_concurrent_sessions"];

    if (geteuid() == 0 && config.contains("run_as_user")) {
        switch_user(config["run_as_user"]);
    }

    httplib::Server svr;
    svr.new_task_queue = [&] { return new httplib::ThreadPool(max_concurrent_sessions); };

    std::cout << "Server configuration:" << std::endl;
    std::cout << "Address: " << address << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Max Keep Alive Count: " << max_keep_alive_count << std::endl;
    std::cout << "Max Concurrent Sessions: " << max_concurrent_sessions << std::endl;

    for (const auto& route : config["routes"]) {
        std::string url = route["url"];
        std::string handler_type = route["handler"];
        auto handler = create_handler(handler_type, route["params"]);
        if (handler) {
            std::cout << "Setting up route: " << url << " with handler type: " << handler_type << std::endl;
            svr.Get(url.c_str(), [handler](const httplib::Request& req, httplib::Response& res) {
                std::cout << "Received GET request for URL: " << req.path << std::endl;
                handler->handle_get(req, res);
            });
            svr.Put(url.c_str(), [handler](const httplib::Request& req, httplib::Response& res) {
                std::cout << "Received PUT request for URL: " << req.path << std::endl;
                handler->handle_put(req, res);
            });
        }
    }

    svr.set_keep_alive_max_count(max_keep_alive_count);
    std::cout << "Starting server on " << address << ":" << port << std::endl;
    if (!svr.listen(address.c_str(), port)) {
        std::cerr << "Error starting server on " << address << ":" << port << std::endl;
        return 1;
    }

    return 0;
}
