# tse-mbo

Minimal C++20 hello-world scaffold.

## What’s here

- `src/main.cpp` prints a test line.
- `docs/assignment-original.docx` is the original Word assignment file.
- `docs/protocol-original.pdf` is the original protocol PDF file.
- `docs/assignment-source.md` contains the extracted Word document text.
- `docs/protocol-source.md` contains the extracted PDF text.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/tse_mbo
```

## Notes

- This machine does not currently have `cmake` or a C++ compiler installed, so I could not compile it here.
- The repo is kept small on purpose so you can verify the document conversion first.
