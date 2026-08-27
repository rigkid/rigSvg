# svg

![Ghostscript tiger](data/tiger.svg)

Example for **rigSvg** — `loadFile` the [Ghostscript tiger](https://commons.wikimedia.org/wiki/File:Ghostscript_tiger_(original_background).svg) into `ParsedPathDoc` and present fills + strokes. Mouse X rotates, mouse Y scales. The shipped file drops the paper `#cccccc` rect.

The drawing is **GNU AGPL-3.0-or-later** (Ghostscript authors). Example code is MIT. See `data/LICENSE.tiger.txt`.

This is I/O, not an editor. Path edit lives in the host **svgEditor** example / **rigSvgEditorUi**.

```bash
cmake -S . -B build
cmake --build build --target svg
./build/bin/svg
./build/bin/svg --smoke
```
