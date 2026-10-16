# facetry-cli

Headless command-line tool: builds models from JSON descriptions
([format](../../tests/models/README.md)), prints their properties, exports STL and checks the
golden models. Built with the `macos-*` presets.

```bash
build/macos-debug/tools/cli/facetry-cli version
build/macos-debug/tools/cli/facetry-cli info tests/models/box-cut-cylinder.json --stl plate.stl
build/macos-debug/tools/cli/facetry-cli check tests/models
```
