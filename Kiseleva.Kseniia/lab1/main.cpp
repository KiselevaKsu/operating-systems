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
        // защита от повторного запуска
        checkAndKillOldProcess();

        // запись pid
        std::ofstream pidFile(pidFilePath);
        pidFile << getpid();
        pidFile.close();

        // настройка syslog
        openlog("lab1_daemon", LOG_PID | LOG_CONS, LOG_USER);
        syslog(LOG_INFO, "Daemon started, PID: %d", getpid());

        // обработка сигналов
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
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        }
    }

    void readConfig() {
        config.clear();
        interval = 20; // сброс на дефолт при перечитывании
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
                iss >> interval;
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

        std::vector<std::string> dirsToKeep;

        // 1. находим все папки, которые НЕ надо удалять (глубина > maxDepth)
        std::function<void(const std::string&, int)> findDirsToKeep = [&](const std::string& currentPath, int currentDepth) {
            for (const auto& entry : fs::directory_iterator(currentPath)) {
                if (entry.is_directory()) {
                    if (currentDepth > maxDepth) {
                        dirsToKeep.push_back(entry.path().string());
                    } else {
                        findDirsToKeep(entry.path().string(), currentDepth + 1);
                    }
                }
            }
        };
        
        // начинаем с 1-го уровня вложенности
        findDirsToKeep(basePath, 1);

        // 2. удаляем всё содержимое basePath
        for (const auto& entry : fs::directory_iterator(basePath)) {
            std::string pathStr = entry.path().string();
            try {
                fs::remove_all(entry.path());
                syslog(LOG_INFO, "Deleted: %s", pathStr.c_str());
            } catch (const std::exception& e) {
                syslog(LOG_ERR, "Failed to delete %s: %s", pathStr.c_str(), e.what());
            }
        }

        // 3. восстанавливаем папки, которые нужно было сохранить
        for (const auto& keepDir : dirsToKeep) {
            try {
                fs::create_directories(keepDir);
                syslog(LOG_INFO, "Restored (kept): %s", keepDir.c_str());
            } catch (const std::exception& e) {
                syslog(LOG_ERR, "Failed to restore %s: %s", keepDir.c_str(), e.what());
            }
        }
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

            // ждем, проверяя сигналы
            for (int i = 0; i < interval; ++i) {
                if (terminateFlag || reloadFlag) break;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        
        syslog(LOG_INFO, "Daemon is terminating.");
        closelog();
        fs::remove(pidFilePath);
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