# Making a release

Two repositories hold this code:

| Repository | What it is |
|---|---|
| [A2B_PnP_Cpp](https://github.com/Clockworks-Signal-Processing/A2B_PnP_Cpp) | Private. The working repository: every branch, commit and merge |
| [Clockworks_A2B](https://github.com/Clockworks-Signal-Processing/Clockworks_A2B) | Public. One commit per release, and nothing else |

A release is the working repository's tree at a tag, copied into the public one as a single
commit. The public repository never sees a branch, a merge or a working commit, and no
history is rewritten to keep it that way: the copy simply carries none.

## What the public repository looks like

`main` is a chain of release commits, each with the version as its whole message and an
annotated tag of the same name:

```
7feb333  v0.4     <- tag v0.4
c7b1607  v0.3     <- tag v0.3
0189d85  v0.2     <- tag v0.2
```

Each commit's tree is the private repository's **tracked** tree at that tag, file for file:
nothing is left out, `CLAUDE.md` and the ADI documents in `docs/` included. What is not
tracked is not in it either, so `build/` and `__pycache__` never arrive.

## Before tagging

1. The work is on `main` in the private repository, and `main` and the development branch
   are at the same commit.
2. The checks in [testing.md](../a2bcpp/docs/testing.md) pass: `a2bcpp/tools/run_tests.sh`
   (or `run_tests.ps1`), `python3 tools/a2b_eeprom/tests/test_a2beeprom.py`, the
   `clang-format` and `clang-tidy` lines, and a warning-free `doxygen` in `a2bcpp`.
3. `python a2bcpp/tools/ram_report.py` has been rerun if the release added per-bus state,
   and [ram.md](../a2bcpp/docs/ram.md) says what it found.
4. `README.md`'s "Written for Version X.Y" line and date are the release's. That line is in
   the owner's section, which only Clockworks edits ([CLAUDE.md](../CLAUDE.md)).
5. [ROADMAP.md](../ROADMAP.md) describes this release, not the last one: what was added,
   what was tested on hardware, and what still needs a bench.

## Tagging

In the private repository, annotated, with the version as the message:

```
git tag -a v0.5 -m "v0.5"
git push origin v0.5
```

A cloud session cannot do this: its credential may write `refs/heads/*` but a push to
`refs/tags/*` is refused with HTTP 403. Tag from a clone of your own.

## The snapshot

From a clone of the public repository, with `PRIVATE` the path to the working repository and
`v0.5` the tag just made:

```
git clone https://github.com/Clockworks-Signal-Processing/Clockworks_A2B pub
cd pub

git rm -rq .                               # clear the tracked tree, keep .git
git -C PRIVATE archive v0.5 | tar -x -C .  # the tracked tree at the tag
git add -A

git commit -m "v0.5"
git tag -a v0.5 -m "v0.5"
git push origin main
git push origin v0.5
```

`git archive` writes exactly the tracked tree at that ref: no `.git`, no ignored files, so
there is nothing to clean up by hand and no way for history to come across. `git rm -rq .`
first is what makes a file deleted since the last release register as a deletion.

## Checking it before the push

The staged tree must be the private repository's tree at the tag, hash for hash. In `pub`,
after `git add -A`:

```
git write-tree                      # in pub
git -C PRIVATE rev-parse v0.5^{tree}
```

The two must print the same hash. If they differ, something untracked was copied in or a
deletion was missed; do not push. `git diff --cached --stat HEAD` then shows what the release
changes, which is worth reading once in any case.
