# rigSvg

![preview](examples/svg/img/preview.png)

SVG to `ParsedPathDoc` - layers of `CPaths` plus the doc `CPaintLibrary`.
Projects that bag through a RigWorks document (`writeDocument` / `readDocument`).

**Import:** `path` (M/L/H/V/C/S/Q/T/A/Z - arcs stay A, smooth S/T), `line`, `rect`,
`circle`, `ellipse`, `polyline`, `polygon`, nested `g` layers, `transform`, root
`viewBox`, fill/stroke colours, `stroke-width`, `<text>` / `<textPath>`,
`clipPath`, `@font-face`, `<linearGradient>` / `<radialGradient>` via `url(#...)`.

**Export:** groups per layer, solid fill/stroke, gradient defs (OkLab/HSV densify
to sRGB stops; intensity bakes in linear light and clamps), analytic `A`, text,
clip, and named font-faces.

Depends on **rigPlotComponent**.

When a host `IMui` is attached, the pack registers a `.svg` file-browser Icons
decoder. The worker paints `PathFlatten` contours and stamps glyphs with the
`imstb_truetype.h` already shipped by **rigImTui** / **rigImGui** (no extra copy).
CPU RGBA (Blend2D only if that target is already linked). Import/export is still
`loadFile` / `writeFile`. The fallback face is `Roboto-Regular.ttf` from the host
fonts directory.

```cpp
rigkit::plot::ParsedPathDoc doc;
rigkit::svg::loadFile(path, doc);
std::string json;
rigkit::svg::writeDocument(doc, json);
rigkit::svg::writeFile(doc, outPath, paperWmm, paperHmm);
```

[API/docs](https://rigkid.github.io/rigSvg/)
