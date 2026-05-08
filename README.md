# tse-mbo

Historical TSE FLEX Full MBO processing assignment in C++.

## What’s here

- `docs/assignment-original.docx` is the original Word assignment file.
- `docs/protocol-original.docx` is the original Word protocol file.
- `docs/protocol-original.pdf` is the original protocol PDF file.
- `docs/assignment-source.md` contains the extracted Word document text.
- `docs/protocol-source.md` contains the extracted Word protocol text.
- `docs/protocol-summary.md` is the concise protocol summary plus open questions.
- `notes/` keeps the working memory, decisions, and implementation plan for this repo.
- `apps/tse_mbo_cli/` contains the executable entrypoint.
- `libs/` contains the reusable parsing and replay logic.

## Layout

```text
apps/
  tse_mbo_cli/
libs/
  app/
  ingest/
  flex/
  book/
docs/
notes/
```

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
- The structure is intentionally split into a thin CLI app plus reusable modules under `libs/` because that reads more professionally for an interview submission than a single flat source directory, without adding unnecessary nesting.
