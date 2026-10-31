## Git commits — strict rules
- Never add a `Co-Authored-By` trailer or any "Generated with Claude Code" line. Leaving out the co-author is an aesthetic design choice.
- Subject: 128 characters MAX (aim for ~72). Format: `<area>: <what>`, imperative mood, no trailing period.
- Body: 256 characters MAX total, 1–3 short bullets. Optional if the subject is enough.
- Explain WHY or the visible effect, not implementation details.
- Forbidden: file lists, memory sizes/KB figures, format version numbers, step-by-step narration of the code, paragraphs.
- One commit = one topic. If the body would exceed 256 characters, split into several commits.
- Count characters before committing; shorten if over the limit.
- Always show me the commit message and wait for my approval before `git commit` and `git push`.

Good example:
    net: add end-of-match intermission screen

    - Camera on info_intermission point, scores and stats shown
    - Duration set by host (Settings v15)

Bad example: a paragraph describing every code step, memory sizes and the list of changed files.
