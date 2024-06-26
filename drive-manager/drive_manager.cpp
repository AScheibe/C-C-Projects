#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <sys/types.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>
#endif

std::vector<std::string> getAvailableDrives() {
    std::vector<std::string> drives;

#ifdef _WIN32
    char driveStrings[MAX_PATH] = {0};
    GetLogicalDriveStrings(MAX_PATH, driveStrings);
    char* drive = driveStrings;
    while (*drive) {
        drives.push_back(std::string(drive));
        drive += strlen(drive) + 1;
    }
#elif defined(__APPLE__) || defined(__linux__)
    // On Linux and macOS, get the list of mount points using 'mount' command
    FILE *fp = popen("mount -v | awk '{print $3}'", "r");
    if (fp) {
        char path[PATH_MAX];
        while (fgets(path, sizeof(path), fp) != NULL) {
            size_t len = strlen(path);
            if(len > 0 && path[len - 1] == '\n') {
                path[len - 1] = '\0';
            }
            drives.push_back(std::string(path));
        }
        pclose(fp);
    }
#else
    #error "Unknown platform"
#endif

    return drives;
}

std::string getDriveWithMostFreeSpace() {
    auto drives = getAvailableDrives();
    std::string targetDrive;
    uint64_t maxFreeSpace = 0;

    for (const auto& drive : drives) {
#ifdef _WIN32
        ULARGE_INTEGER freeBytesAvailable;
        if (GetDiskFreeSpaceEx(drive.c_str(), &freeBytesAvailable, NULL, NULL) && freeBytesAvailable.QuadPart > maxFreeSpace) {
            maxFreeSpace = freeBytesAvailable.QuadPart;
            targetDrive = drive;
        }
#elif defined(__APPLE__) || defined(__linux__)
        struct statvfs stat;
        if (statvfs(drive.c_str(), &stat) == 0) {
            uint64_t freeSpace = stat.f_bsize * stat.f_bfree;
            if (freeSpace > maxFreeSpace) {
                maxFreeSpace = freeSpace;
                targetDrive = drive;
            }
        }
#endif
    }

    return targetDrive;
}

bool storeDataOnDrive(const std::string& drive, const std::string& data) {
    std::string filename;
    
#ifdef _WIN32
    filename = drive + "output.txt";
#else
    // For Linux and macOS, just save in the root for simplicity. You can customize as needed.
    filename = drive + "output.txt";
#endif

    std::ofstream outFile(filename);
    if (outFile.is_open()) {
        outFile << data;
        outFile.close();
        return true;
    }
    
    return false;
}

int main() {
    std::vector<std::string> drives = getAvailableDrives();
    for (const std::string& drive : drives) {
        std::cout << drive << std::endl;
    }

    return 0;
}
