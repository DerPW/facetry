# Contributing

Thanks for your interest in Facetry. The project is at a very early stage, so the most helpful
contributions right now are ideas, feedback on the [roadmap](docs/ROADMAP.md) and bug reports
once there is something to run.

## Language

Everything in this repository is in **English**: code, comments, documentation, commit
messages, issues and pull requests.

## Workflow

- `main` is protected. Every change goes through a pull request, including the maintainer's own.
- Create a branch from `main` with a prefix that matches the change:
  `feat/…`, `fix/…`, `docs/…`, `refactor/…`, `test/…`, `chore/…`, `ci/…`
- Keep pull requests small and focused on one roadmap item or issue.
- Pull request titles follow [Conventional Commits](https://www.conventionalcommits.org/)
  (`feat(sketch): add tangent arc`), because PRs are squash-merged and the title becomes the
  commit message on `main`.
- Link the roadmap milestone or issue the PR belongs to. If the PR completes a roadmap item,
  tick its checkbox in the phase document in the same PR.
- All CI checks must pass and all review conversations must be resolved before merging.
- Larger changes, or anything that changes an architecture decision, start with an issue or an
  ADR in `docs/adr/`.

## AI-assisted contributions

Much of Facetry is written with AI assistance (see the [README](README.md#built-with-ai)).
AI-assisted contributions are welcome under the same rules as any other: you understand the
change, it is tested, and it follows the [architecture rules](docs/ARCHITECTURE.md#rules).
Mention substantial AI assistance in the PR description.

## License

By contributing, you agree that your contributions are licensed under the
[MIT License](LICENSE).
