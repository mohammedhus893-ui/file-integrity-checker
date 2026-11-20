#include <iostream>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <fstream>
#include <openssl/sha.h>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <map>

bool g_daemon = false;

std::map<std::string, std::string> last_alerted;

// Computes the SHA-256 hash of a file by reading it in 4096-byte chunks
// and returns it as a 64-character hex string. Returns an empty string
// if the file cannot be opened.
std::string hash_core(std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: cannot open \"" << filename << "\"" << std::endl;
        return "";
    }
    SHA256_CTX sha;
    SHA256_Init(&sha);
    char buffer[4096];
    while (file.read(buffer, sizeof(buffer))) {
        SHA256_Update(&sha, buffer, file.gcount());
    }
    if (file.gcount() > 0) {
        SHA256_Update(&sha, buffer, file.gcount());
    }
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha);
    char hex_output[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hex_output + (i * 2), "%02x", hash[i]);
    }
    return std::string(hex_output);
}

// Returns true if the two hashes are identical (file unchanged),
// and false if they differ (file modified).
bool compare_hash(const std::string& old_hash, const std::string& new_hash) {
    return old_hash == new_hash;
}

// Copies a file to "<file_name>.bak" using raw Linux syscalls
// (open/read/write/close) in 4096-byte chunks, overwriting any
// previous backup and checking that every write succeeds.
void create_backup(std::string file_name) {
    char buffer[4096] ;
    int file_source = open(file_name.c_str(),O_RDONLY) ;
    if(file_source < 0 ) {
        std::cout << "Error: " << strerror(errno) << std::endl ;
        return;
    }
    std::string backup_name = file_name + ".bak";
    int fd = open(backup_name.c_str(),O_CREAT |O_WRONLY| O_TRUNC,0644) ;
        if(fd < 0) {
        std::cout << "Error: " << strerror(errno) << std::endl ;
        close(file_source) ;
        return;
    }
    ssize_t n ;
    while ((n = read(file_source,buffer,sizeof(buffer))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(fd,buffer + written,n - written) ;
            if (w < 0) {
                std::cout << "Error: " << strerror(errno) << std::endl ;
                close(fd) ;
                close(file_source) ;
                return;
            }
            written += w;
        }
    }
    close(fd) ;
    close(file_source) ;
}

// Reads the previously saved hash from a baseline file. Returns an
// empty string if the baseline does not exist yet (first run).
std::string load_baseline(const std::string& baseline_path) {
    std::ifstream baseline_file(baseline_path);
    std::string saved_hash;
    if (baseline_file) {
        baseline_file >> saved_hash;
    }
    return saved_hash;
}

// Writes a hash to the baseline file, replacing any previous content,
// so the next check can compare against it.
void save_baseline(const std::string& baseline_path, const std::string& hash) {
    std::ofstream baseline_file(baseline_path, std::ios::trunc);
    baseline_file << hash;
}

// Shows a desktop notification by running the "notify-send" command
// in a child process, and waits for it to finish.
void send_alert(const std::string& title, const std::string& message, bool critical) {
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Error: fork failed: " << strerror(errno) << std::endl;
        return;
    }
    if (pid == 0) {
        execlp("notify-send", "notify-send",
               "-u", critical ? "critical" : "normal",
               title.c_str(), message.c_str(), (char*)nullptr);
        _exit(1);
    }
    waitpid(pid, nullptr, 0);
}

// Runs the full check for one file: hashes it, compares with the saved
// baseline, and handles the result. On first run it saves a baseline and
// backup. On a change it sends an alert; in daemon mode it alerts once per
// change, otherwise it asks the user whether to accept the change.
void process_file(std::string filename) {
    char answer = 'n';
    std::string baseline = filename + ".baseline";
    std::string new_hash = hash_core(filename);
    if (new_hash.empty()) {
        return;
    }
    std::string old_hash = load_baseline(baseline);
    if (old_hash.empty()) {
        std::cout << "First Run..." << std::endl;
        save_baseline(baseline, new_hash);
        create_backup(filename);
    }
    else {
        if (!compare_hash(old_hash, new_hash)) {
            std::cout << "Error file has been changed!!" << std::endl;
            if (g_daemon) {
                if (last_alerted[filename] != new_hash) {
                    send_alert("File changed", "The file \"" + filename + "\" has been modified.", true);
                    last_alerted[filename] = new_hash;
                }
                return;
            }
            send_alert("File changed", "The file \"" + filename + "\" has been modified.", true);
            std::cout << "Did you change it (Y / n) ? : ";
            std::cin >> answer;
            if (answer == 'Y' || answer == 'y') {
                std::cout << "Baseline and backup updated..." << std::endl;
                save_baseline(baseline, new_hash);
                create_backup(filename);
            }
            else {
                std::cout << "WARNING: change rejected. The file \"" << filename
                          << "\" differs from its baseline." << std::endl;
                std::cout << "The original copy is still saved in \"" << filename
                          << ".bak\"." << std::endl;
            }
        }
    }
}

// Goes through every regular file in a directory (not recursive) and
// calls process_file() on each, skipping ".", "..", and the tool's own
// ".baseline" and ".bak" files.
void scan_directory(std::string dir_path) {
    DIR* dir = opendir(dir_path.c_str());
    if (dir == nullptr) {
        std::cout << "Error: " << strerror(errno) << std::endl;
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;

        bool is_baseline_file = name.size() >= 9 && name.substr(name.size() - 9) == ".baseline";
        bool is_backup_file = name.size() >= 4 && name.substr(name.size() - 4) == ".bak";

        if (name == "." || name == ".." || is_baseline_file || is_backup_file) {
            continue;
        }

        std::string filename = dir_path + "/" + name;
        struct stat entry_info;
        if (stat(filename.c_str(), &entry_info) != 0 || !S_ISREG(entry_info.st_mode)) {
            continue;
        }
        process_file(filename);
    }

    closedir(dir);
}

// Entry point. Reads the file or directory path (and the optional
// --daemon flag) from the command line, then checks the target in an
// endless loop every 10 seconds until stopped with Ctrl+C.
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file_or_directory>" << std::endl;
        return 1;
    }

    std::string path = argv[1];
    if (access(path.c_str(), F_OK) != 0) {
        std::cerr << "Error: \"" << path << "\" does not exist." << std::endl;
        return 1;
    }

    struct stat path_info;
    stat(path.c_str(), &path_info);

    if (argc >= 3 && std::string(argv[2]) == "--daemon") {
        g_daemon = true;
    }

    while (true) {
        if (S_ISDIR(path_info.st_mode)) {
            scan_directory(path);
        } else {
            process_file(path);
        }
        sleep(10);
    }
}
