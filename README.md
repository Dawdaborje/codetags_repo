Codetags turns tagged inline comment into a powerful and extensible autodocumentation tool. It is designed to work with most common programming language comment syntaxes and supports the following inline tags:
```bash
NOTE: text...
TODO: text...
WARNING: text...
WARN: text...
FIXME: text...
FIX: text...
BUG: text...
```


When a file changes, codetags appends a stable unique ID ([CT‑…]) to the codetag line in‑place and updates a codetags.md file at the repo root with all codetags, their file path, and their line number.

Line numbers change dynamically as files are edited, so the unique ID is used to keep track of the tag even if its line number changes. This allows you to easily reference and find tags later, even if the file has been modified.

## Installation

Build and install `codetags` from source:
```bash
./install.sh
```

Initialize within a project folder:
```bash
codetags init
```

This will create a `codetags.md` file in the current directory, which will be used to store the content of the tags under it's relevant category, along with its unique identifier, relative path, and line number.

There are many ways to extend the tagging system, but this is the first iteration which simply collects and sorts tagged comments into a single file for easy reference, making the software development lifecycle a little bit easier.

## Background Process
After initialization, the repository name and absolute path is added a list stored in `~/.ctags/registered_repos.txt`.

This file is monitored by the codetags watcher daemon, which runs in the background after installation from system boot time, in order to keep track of changes in your target repositories. The watcher will automatically update the `codetags.md` file with new tags as they are added or modified.

The codetags watcher daemon monitores file changes within a target repository using `inotify-tools`.

As previously mentioned, after initialization the repository name is stored. This is achieved by the watcher daemon monitoring the registered_repos.txt file upon installation, so that if you add a new repository to it with `codetags init`, the watcher will automatically start monitoring that repository.

For existing projects, you can run this scan to collect tags into the codetags.md after initialization:

```bash
codetags scan .
```


## Feature roadmap

### Near-term

- Tag formatting: Support multiple output styles (table, list, minimal).

- Split tags into multiple files: E.g., codetags.md for dev TODOs, and warnings.md for warnings/bugs.

### Mid/long-term
- Ticketing system integration:

- Convert codetags into trackable tickets

- Calendar scheduling for deadlines

- Email notifications for assigned items

- Full autodoc generation:

- Collate codetags + source doc comments into full documentation

## Automation pipelines:

- Make codetag data machine-readable for agentic processes (AI/dev bots)

- Expose as API or message bus for other tools

### Why use codetags?
- Immediate visibility: Every codetag is tracked in one place, always up‑to‑date.

- Persistent IDs: IDs stay fixed so you can reference them in tickets, commits, or docs.

Zero manual upkeep: System‑wide watcher means no per‑repo watcher scripts.

Extensible: Designed to grow into more powerful project management and automation tooling.

