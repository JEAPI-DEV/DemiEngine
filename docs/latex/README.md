# DemiEngine LaTeX Documentation

Entry point:

```bash
docs/latex/main.tex
```

Build from `docs/latex` with a local LaTeX installation:

```bash
pdflatex main.tex
```

The documentation is split into `chapters/`, `appendices/`, and `shared/` so sections can be edited independently.

Please read the docs under docs/latex first to understand the examples/minimal_2d and the game engine limitation and functionality. I want you to work on runtime scene switching so for that I want you to add two levels (no requirement just different kinds of levels, one is the current and the second should procedually generate a spiral like upwards jumping terrain.)