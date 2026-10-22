#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <limits.h>
#include <functional>

namespace fs = std::filesystem;

class Daemon {
public:
    static Daemon& getInstance() {
        static Daemon instance;
        return instance;
    }

    void run(const std::string& configPath) {
        this->configPath = configPath;
        setup();
        loop();
    }

private:
    Daemon() {}
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    std::string configPath;
    std::string pidFilePath = "/tmp/lab1_daemon.pid";
    int interval = 20; // интервал по умолчанию
    std::vector<std::pair<std::string, int>> config;
    volatile sig_atomic_t reloadFlag = 0;
    volatile sig_atomic_t terminateFlag = 0;

    static void signalHandler(int sig) {
        if (sig == SIGHUP) {
            Daemon::getInstance().reloadFlag = 1;
        } else if (sig == SIGTERM) {
            Daemon::getInstance().terminateFlag = 1;
        }
    }

    void setup() {
        checkAndKillOldProcess();

        // запись pid
        std::ofstream pidFile(pidFilePath);
        pidFile << getpid();
        pidFile.close();

        // настройка syslog
        openlog("lab1_daemon", LOG_PID | LOG_CONS, LOG_USER);
        syslog(LOG_INFO, "Daemon started, PID: %d", getpid());

        signal(SIGHUP, signalHandler);
        signal(SIGTERM, signalHandler);

        readConfig();
    }

    void checkAndKillOldProcess() {
        std::ifstream pidFile(pidFilePath);
        if (pidFile.is_open()) {
            pid_t oldPid;
            pidFile >> oldPid;
            pidFile.close();
            if (oldPid > 0) {
                std::string procPath = "/proc/" + std::to_string(oldPid);
                if (fs::exists(procPath)) {
                    syslog(LOG_INFO, "Old daemon (PID %d) is running. Sending SIGTERM.", oldPid);
                    kill(oldPid, SIGTERM);
                    
                    // ждем реального завершения старого процесса (максимум 5 секунд)
                    int waitCount = 0;
                    while (fs::exists(procPath) && waitCount < 50) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        waitCount++;
                    }
                    if (fs::exists(procPath)) {
                        syslog(LOG_WARNING, "Old daemon didn't finish in time, sending SIGKILL.");
                        kill(oldPid, SIGKILL);
                    }
                }
            }
        }
    }

    void readConfig() {
        config.clear();
        interval = 20; 
        std::ifstream file(configPath);
        if (!file.is_open()) {
            syslog(LOG_ERR, "Cannot open config: %s", configPath.c_str());
            return;
        }
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            std::string first;
            iss >> first;
            
            if (first == "interval") {
                std::string valStr;
                if (iss >> valStr) {
                    try {
                        int val = std::stoi(valStr);
                        if (val > 0) {
                            interval = val;
                        } else {
                            syslog(LOG_WARNING, "Invalid interval %s, using default 20.", valStr.c_str());
                        }
                    } catch (...) {
                        syslog(LOG_WARNING, "Invalid interval format %s, using default 20.", valStr.c_str());
                    }
                }
            } else {
                int depth;
                if (iss >> depth) {
                    config.push_back({first, depth});
                }
            }
        }
        syslog(LOG_INFO, "Config reloaded. %zu entries, interval %d.", config.size(), interval);
    }

    void deleteSubfolders(const std::string& basePath, int maxDepth) {
        if (!fs::exists(basePath) || !fs::is_directory(basePath)) return;

        std::function<void(const std::string&, int)> traverse = [&](const std::string& currentPath, int currentDepth) {
            DIR* dir = opendir(currentPath.c_str());
            if (!dir) return;

            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                if (name == "." || name == "..") continue;
                
                std::string fullPath = currentPath + "/" + name;
                struct stat statbuf;
                
                if (stat(fullPath.c_str(), &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
                    // если мы достигли нужной глубины, удаляем папку целиком
                    if (currentDepth == maxDepth) {
                        try {
                            fs::remove_all(fullPath);
                            syslog(LOG_INFO, "Deleted: %s", fullPath.c_str());
                        } catch (const std::exception& e) {
                            syslog(LOG_ERR, "Failed to delete %s: %s", fullPath.c_str(), e.what());
                        }
                    } else {
                        // иначе идем глубже
                        traverse(fullPath, currentDepth + 1);
                    }
                }
            }
            closedir(dir);
        };

        // начинаем с 1-го уровня (файлы в корне basePath не трогаем)
        traverse(basePath, 1);
    }

    void doWork() {
        for (const auto& entry : config) {
            const std::string& path = entry.first;
            int depth = entry.second;
            syslog(LOG_INFO, "Processing: %s (depth: %d)", path.c_str(), depth);
            deleteSubfolders(path, depth);
        }
    }

    void loop() {
        while (!terminateFlag) {
            if (reloadFlag) {
                reloadFlag = 0;
                readConfig();
            }
            doWork();

            // ждем interval секунд, проверяя сигналы
            for (int i = 0; i < interval; ++i) {
                if (terminateFlag || reloadFlag) break;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        
        syslog(LOG_INFO, "Daemon is terminating.");
        closelog();
        
        // удаляем pid-файл только если он наш!
        std::ifstream pidFile(pidFilePath);
        if (pidFile.is_open()) {
            pid_t currentPid;
            pidFile >> currentPid;
            pidFile.close();
            if (currentPid == getpid()) {
                fs::remove(pidFilePath);
            }
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }
    
    // абсолютный путь к конфигу
    char resolvedPath[PATH_MAX];
    std::string finalConfigPath = argv[1];
    if (realpath(argv[1], resolvedPath) != nullptr) {
        finalConfigPath = resolvedPath;
    }

    // демонизация
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) {
        std::cout << "Daemon started in background. PID: " << pid << std::endl;
        exit(EXIT_SUCCESS);
    }

    umask(0);
    if (setsid() < 0) exit(EXIT_FAILURE);

    // закрытие стандартных дескрипторов (в /dev/null)
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    Daemon::getInstance().run(finalConfigPath);
    
    return 0;
}