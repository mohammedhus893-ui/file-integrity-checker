# File Integrity Checker

A command-line C++ tool that monitors a file for unauthorized
changes. It computes a SHA-256 fingerprint of the file, stores
it as a baseline, and on every later run compares the current
fingerprint against that baseline — alerting you if the file's
content has changed, and automatically keeping a backup copy.

This is the same core principle used by real-world file
integrity monitoring tools such as Tripwire and AIDE.

## How It Works

1. **First run** — the tool computes the file's SHA-256 hash,
   saves it to a baseline file, and takes an initial backup.
2. **Every later run** — the tool recomputes the hash and
   compares it to the saved baseline:
   - If they match, the file is unchanged.
   - If they differ, the tool reports a mismatch, updates the
     baseline to the new hash, and saves a fresh backup.

## Code Walkthrough

### `hash_core(filename)`
Opens the file in binary mode and computes its SHA-256 hash by
reading it in 4096-byte chunks, feeding each chunk into the
hashing algorithm incrementally (`SHA256_Init` /
`SHA256_Update` / `SHA256_Final`). This streaming approach
means the file never has to be fully loaded into memory, so it
works efficiently on files of any size. Returns the hash as a
64-character hexadecimal string.

### `compare_hash(old_hash, new_hash)`
Compares two hash strings and prints whether the file has
changed or remained identical.

### `create_backup(file_name)`
Copies the target file to `backup_file.txt` using raw Linux
syscalls (`open`, `read`, `write`, `close`) instead of C++
stream objects — reading and writing in a loop, 4096 bytes at a
time, so the copy works correctly regardless of file size.

### `load_baseline(baseline_path)`
Reads a previously saved hash from the file's `.baseline` file.
Returns an empty string if no baseline exists yet, which tells
the program this is the first time the file is being monitored.

### `save_baseline(baseline_path, hash)`
Writes the current hash to the `.baseline` file, overwriting
any previous value, so the next run has something to compare
against.

### `main()`
Ties everything together:
1. Reads the target filename from the command line (`argv[1]`).
2. Builds the baseline file path (`<filename>.baseline`).
3. Computes the current hash and loads the saved baseline hash.
4. If no baseline exists, saves one and takes an initial
   backup. Otherwise, compares old vs. new hash and, if they
   differ, updates the baseline and takes a new backup.

## Requirements

- A C++ compiler (`g++`)
- OpenSSL development headers (`libssl-dev` on Debian/Ubuntu)

## Build

```bash
g++ File_Integrity_Checker.cpp -o checker -lcrypto
```

## Usage

```bash
./checker <filename>
```

## Example Run

Input file `demo2.txt`:
```
Hello linux, this is a config file.
```

**Run 1 — first time monitoring the file:**
```bash
./checker demo2.txt
```
Output:
```
First run: establishing baseline for "demo2.txt"
Baseline hash: 2a1afb6900d06571c223eb66e9c7f4b7effdb73d77fbd77248759308fa2eb14d
```

**Run 2 — running again with no changes:**
```bash
./checker demo2.txt
```
Output:
```
No changes have been made to the file
```

**The file is then modified:**
```bash
echo "Malicious line added!" >> demo2.txt
```

**Run 3 — after modification:**
```bash
./checker demo2.txt
```
Output:
```
Mismatch: The file contains changes!!
Baseline and backup updated.
```

**Files produced after these runs:**
```
demo2.txt            — the monitored file
demo2.txt.baseline   — stores the current reference hash
backup_file.txt      — a backup copy taken whenever a change is detected
```

## Notes & Limitations

- `SHA256_Init`, `SHA256_Update`, and `SHA256_Final` are marked
  deprecated as of OpenSSL 3.0 in favor of the newer `EVP`
  interface, but remain fully functional here.
- `create_backup` currently appends to `backup_file.txt` rather
  than creating uniquely named, timestamped backups, so only
  the most recent backup set is easily distinguishable.
- The tool currently monitors one file per run; it does not yet
  support watching multiple files or running continuously in
  the background.

## Possible Next Steps

- Accept a list of files (or a directory) to monitor at once
- Use `inotify` to detect changes in real time instead of
  running the check manually
- Timestamp each backup so a history of versions is kept
- Add an `EVP`-based SHA-256 implementation to replace the
  deprecated API
