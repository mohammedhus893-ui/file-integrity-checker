# File Integrity Checker

A command-line C++ tool for Linux that detects unauthorized changes to files.
It computes a SHA-256 fingerprint (hash) of a file, stores it as a **baseline**,
and compares the file against that baseline on later checks. When something
changes, the tool alerts you and keeps a backup copy of the last trusted version.

This is the same core principle used by real-world file integrity monitoring
tools such as Tripwire and AIDE.

The project went through two versions:

| | **v1 (initial)** | **v2 (final)** |
|---|---|---|
| Targets | One file | One file **or a whole directory** |
| Running | Single check, then exits | Runs continuously, checking every 10 seconds |
| On change | Prints a message and **automatically** updates baseline and backup | Prints a message, sends a **desktop notification**, and **asks you** before updating |
| Backup file | One shared `backup_file.txt`, new content **appended** each time | One `<filename>.bak` per file, **overwritten** on update |
| Unattended mode | No | `--daemon` flag (no prompts, alerts once per change) |
| Missing/unreadable file | Silently hashes it as empty | Reports an error and skips it |
| Write errors in backup | Not checked | Checked, including partial writes |

---

## Requirements

- Linux
- A C++ compiler (`g++`)
- OpenSSL development headers (`libssl-dev` on Debian/Ubuntu)
- `notify-send` for desktop alerts in v2 (`libnotify-bin` on Debian/Ubuntu)

## Build

```bash
g++ File_Integrity_Checker.cpp -o checker -lcrypto
```

---

## Final version (v2)

### How it works

1. **First check of a file**: the tool hashes the file, saves the hash to
   `<filename>.baseline`, and saves a backup to `<filename>.bak`.
2. **Every later check**: the tool recomputes the hash and compares it to the baseline.
   - Same hash: nothing happens.
   - Different hash: the tool prints a warning and sends a desktop notification.
3. **Deciding what to do with a change** (interactive mode):
   - Answer `Y`: the new content is accepted, so the baseline and backup are updated.
   - Answer `n`: the change is rejected, the baseline stays as it was, and the
     `.bak` file still holds the original trusted copy.
4. The whole process repeats every **10 seconds** until you stop it with `Ctrl+C`.

### Usage

```bash
./checker <file_or_directory>            # interactive: asks before accepting changes
./checker <file_or_directory> --daemon   # unattended: no prompts
```

In **directory mode**, every regular file directly inside the directory is
monitored (subdirectories are not entered). The tool ignores its own
`.baseline` and `.bak` files.

In **`--daemon` mode**, the tool never asks questions. When a file changes, it
sends one desktop notification for that change (not one every 10 seconds) and
leaves the baseline untouched, so the file stays marked as modified.

### Example (interactive)

Input file `demo2.txt`:
```
Hello linux, this is a config file.
```

```bash
./checker demo2.txt
```

First check:
```
First Run...
```

Now, from another terminal, modify the file:
```bash
echo "Malicious line added!" >> demo2.txt
```

Within 10 seconds the tool reports:
```
Error file has been changed!!
Did you change it (Y / n) ? :
```

Answering `n`:
```
WARNING: change rejected. The file "demo2.txt" differs from its baseline.
The original copy is still saved in "demo2.txt.bak".
```

Answering `Y`:
```
Baseline and backup updated...
```

Files produced:
```
demo2.txt            - the monitored file
demo2.txt.baseline   - the trusted reference hash
demo2.txt.bak        - backup of the last trusted version
```

### Code walkthrough

| Function | What it does |
|---|---|
| `hash_core` | Computes the SHA-256 hash of a file, reading it in 4096-byte chunks (`SHA256_Init` / `Update` / `Final`) so large files are never loaded fully into memory. Returns an empty string if the file can't be opened. |
| `compare_hash` | Returns `true` if two hashes match. |
| `create_backup` | Copies a file to `<name>.bak` using raw Linux syscalls (`open`, `read`, `write`, `close`), overwriting the old backup and checking every write. |
| `load_baseline` | Reads the saved hash from `<name>.baseline`; returns an empty string if none exists (meaning first run). |
| `save_baseline` | Writes a hash to the baseline file, replacing the old value. |
| `send_alert` | Runs `notify-send` in a child process (`fork` + `execlp`) to show a desktop notification. |
| `process_file` | The main logic for one file: hash, compare, alert, and optionally update baseline and backup. |
| `scan_directory` | Runs `process_file` on every regular file in a directory, skipping `.`, `..`, `.baseline` and `.bak` files. |
| `main` | Reads the path and the optional `--daemon` flag, then loops forever, checking every 10 seconds. |

### Limitations

- The baseline and backup are stored next to the monitored file, so someone who
  can modify the file can also modify them. For real protection, keep them
  somewhere write-protected.
- Directory scanning is not recursive.
- In interactive mode, if you answer `n`, the same question appears again on the
  next cycle because the file still differs from its baseline.
- Desktop notifications need a desktop session; `notify-send` may not work from
  cron, SSH, or a root shell.
- The first run trusts the file as it is. If it was already tampered with, that
  state becomes the baseline.
- `SHA256_Init`, `SHA256_Update` and `SHA256_Final` are deprecated since
  OpenSSL 3.0 in favor of the `EVP` interface, but still work.

### Possible next steps

- Recursive directory scanning
- Use `inotify` for instant detection instead of polling every 10 seconds
- Timestamped backups to keep a version history
- Switch to the `EVP` SHA-256 interface
- Make the check interval configurable

---

## Initial version (v1)

The first version was a simple one-shot checker for a single file.

### How it worked

1. **First run**: computed the file's SHA-256 hash, saved it to `<filename>.baseline`,
   and made a backup.
2. **Every later run**: recomputed the hash and compared it to the baseline.
   - Same hash: printed that nothing changed.
   - Different hash: printed a mismatch, then **automatically** updated the
     baseline and made a new backup.

Each run checked once and exited, so you had to run it manually every time.

### Usage

```bash
./checker <filename>
```

### Example

```bash
./checker demo2.txt
```
```
First run: establishing baseline for "demo2.txt"
Baseline hash: 2a1afb6900d06571c223eb66e9c7f4b7effdb73d77fbd77248759308fa2eb14d
```

Running again with no changes:
```
No changes have been made to the file
```

After `echo "Malicious line added!" >> demo2.txt`:
```
Mismatch: The file contains changes!!
Baseline and backup updated.
```

### Problems found in v1 (fixed in v2)

- The backup was taken **after** a change was detected, so it contained the
  modified (possibly malicious) content instead of the trusted one.
- The baseline was updated automatically, so the next run reported "no changes"
  even though the file was still modified.
- The backup was opened with `O_APPEND`, so `backup_file.txt` accumulated every
  version one after another, and all monitored files shared the same backup name.
- A missing file was silently hashed as an empty file.
- The return value of `write()` was not checked.
