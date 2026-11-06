# Rules for Claude in this repository

## Read the docs first

At the start of every session, before answering anything about the code, read every
Markdown file tracked in the repo, in full:

1. List them with `git ls-files '*.md'` (no `head`, no filtering by guessed relevance).
2. Read each one to its last line.
3. Say how many you read, e.g. "Read 26 .md files."

Never say a tool, file, or feature doesn't exist without searching the whole tree
(`git ls-files`, then Grep) and reading the result untruncated.

## README.md: the owner's section

The top of `README.md` is written by Clockworks. It starts with the line

    Clockworks generated text. No AI allowed until after "AI Section"

and runs down to the line that begins `"AI Section"`. Don't change anything in that span:
no rewording, reflowing, typo or date fixes, and no moving it. Edit `README.md` only below
the `"AI Section"` line. If something in the owner's section looks wrong or out of date,
say so instead of changing it.

Commit the owner's own text without a Co-Authored-By line: it isn't co-authored.
