#include  <iostream> 
#include  <string> 
#include  <unistd.h> 
#include  <fcntl.h>
#include <linux/input.h>
#include <fstream>
#include <openssl/sha.h>
#include  <cerrno>
#include <cstring>

// Computes the SHA-256 hash of a file and returns it as a
// 64-character hexadecimal string. Reads the file in 4096-byte
// chunks so it works efficiently even on very large files.
std::string hash_core(std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
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

// Compares two hashes and prints whether the file has changed.
void compare_hash(std::string& old_hash,std::string new_hash) {
    if(old_hash == new_hash) {
        std::cout << "No changes have been made to the file" << std::endl ;
    }
    else if(old_hash != new_hash) {
        std::cout << "Mismatch: The file contains changes!!" << std::endl ;
    }
}

// Copies the given file to "backup_file.txt" using raw Linux
// syscalls (open/read/write/close), reading and writing in
// 4096-byte chunks so it works for files of any size.
void create_backup(std::string file_name) {
    char buffer[4096] ;
    int file_sourc = open(file_name.c_str(),O_RDONLY) ;
    if(file_sourc < 0 ) {
        std::cout << "Error: " << strerror(errno) << std::endl ;
        close(file_sourc) ;
        return;
    }
    int fd = open("backup_file.txt",O_CREAT |O_WRONLY| O_APPEND,0644) ;
        if(fd < 0) {
        std::cout << "Erorr: " << strerror(errno) << std::endl ;
        close(file_sourc) ;
        return;
    }
    ssize_t n ;
    while ((n = read(file_sourc,buffer,sizeof(buffer))) > 0) {
        write(fd,buffer,n) ;
    }
    close(fd) ;
    close(file_sourc) ;
}

// Reads a previously saved hash from the baseline file.
// Returns an empty string if the baseline file doesn't exist
// yet (meaning this file has never been checked before).
std::string load_baseline(const std::string& baseline_path) {
    std::ifstream baseline_file(baseline_path);
    std::string saved_hash;
    if (baseline_file) {
        baseline_file >> saved_hash;
    }
    return saved_hash;
}

// Writes the given hash to the baseline file, overwriting
// (truncating) any previous content, so the next run can
// compare against this saved value.
void save_baseline(const std::string& baseline_path, const std::string& hash) {
    std::ofstream baseline_file(baseline_path, std::ios::trunc);
    baseline_file << hash;
}

int main(int argc, char* argv[]) {
    // argc counts how many words were typed on the command
    // line, including the program name itself. If the user
    // didn't provide a file path, argc will be less than 2,
    // so we print usage instructions and stop.
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
        return 1;
    }

    // argv[1] is the first argument after the program name —
    // the path to the file the user wants to monitor.
    std::string filename = argv[1];

    // Build the name of this file's baseline file by appending
    // ".baseline" to the original filename, e.g.
    // "test.txt" -> "test.txt.baseline"
    std::string baseline_path = filename + ".baseline";

    // Step 1: compute the current hash of the target file.
    std::string current_hash = hash_core(filename);

    // Step 2: try to load a hash that was saved on a previous
    // run. This will be empty if no baseline exists yet.
    std::string old_hash = load_baseline(baseline_path);

    if (old_hash.empty()) {
        // No previous baseline found: this is the first time
        // this file is being monitored. Save the current hash
        // as the new baseline and take an initial backup.
        std::cout << "First run: establishing baseline for \"" << filename << "\"" << std::endl;
        save_baseline(baseline_path, current_hash);
        create_backup(filename);
        std::cout << "Baseline hash: " << current_hash << std::endl;
    } else {
        // A previous baseline exists: compare it against the
        // current hash and report whether the file changed.
        compare_hash(old_hash, current_hash);

        if (old_hash != current_hash) {
            // The file changed since the last check: update
            // the stored baseline and take a fresh backup so
            // future comparisons use the new content as the
            // reference point.
            save_baseline(baseline_path, current_hash);
            create_backup(filename);
            std::cout << "Baseline and backup updated." << std::endl;
        }
    }

    return 0;
}