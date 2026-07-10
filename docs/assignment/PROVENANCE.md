# Assignment PDF provenance

The original assignment PDFs for this project (EECS/EECE 6083, University of
Cincinnati, Prof. Philip A. Wilsey, taken ~Spring 2019) were never committed to
this repo. These copies were recovered from the web on 2026-07-10.

## Files

- `2023-snapshot/` — Wayback Machine captures of the professor's own site,
  the **closest recoverable version to the 2019 offering**:
  - `projectLanguage.pdf` — "Spring 2023, Revision 0" (dated 1/8/23).
    Captured 2023-07-18: <https://web.archive.org/web/20230718050346/https://eecs.ceas.uc.edu/~wilseypa/classes/eece6083/project/projectLanguage.pdf>
  - `project.pdf` — dated January 8, 2023.
    Captured 2023-03-01: <https://web.archive.org/web/20230301031104/https://eecs.ceas.uc.edu/~wilseypa/classes/eece6083/project/project.pdf>
- `2024-current/` — the versions live on the course page as of 2026-07-10
  (rewritten January 2024; same grammar, reworded/renumbered semantics section):
  - <https://eecs.ceas.uc.edu/~wilseypa/classes/eece6083/project/projectLanguage.pdf>
  - <https://eecs.ceas.uc.edu/~wilseypa/classes/eece6083/project/project.pdf>

Course page (still live 2026-07): <https://eecs.ceas.uc.edu/~wilseypa/classes/eece6083/>
(also hosts `testPgms.tgz`, the same professor-provided test programs as
`testPgms/correct/` here).

## Why no exact 2019 copy exists

The Wayback Machine has **no snapshot of the course's project files before
2023** — the `~wilseypa` personal directory was barely crawled until 2020-2023,
on every historic UC domain checked. The best available lineage evidence:

- A classmate-repo copy dated **Spring 2021 "Revision 6"**
  (`github.com/BStarcheus/compiler`) is textually identical to the 2023
  snapshot apart from line wrapping and header dates — the document was stable
  across 2021→2023, so it very plausibly matches 2019 as well.
- A **2013-era** copy (`github.com/ajalt/eece6083-compiler`) has a genuinely
  different grammar (procedures had *no return type*), which does **not** match
  this repo's parser — so the spec did change at some point between 2013 and
  2019.
- The grammar in the 2021/2023/2024 versions matches what this repo's
  scanner/parser/typechecker implement element-for-element (`program <id> is
  ... end program.` with trailing period, `procedure <identifier> :
  <type_mark>`, optional `global` prefix, `for (<assignment>; <expr>)`,
  nested block comments, `getInteger`/`putBool`/... built-ins).

Treat `2023-snapshot/` as "almost certainly the 2019 spec, unverifiable
byte-for-byte"; use `2024-current/` for its cleaner wording when finishing the
compiler.
