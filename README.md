# TBLIB AutoBuild — Geometry Dash 2.2081

This project packages the uploaded TBLIB 2 libraries into a Geode mod project.

## Included libraries

- `hell_temp` — 224 PIECES
- `hell_base` — 1 PIECES
- `modern_base` — 1 PIECES
- `modern_temp` — 31 PIECES
- `null_base` — 1 PIECES
- `null_temp` — 180 PIECES
- `pt_base` — 1 PIECES
- `tech_temp` — 113 PIECES
- `wfc_base` — 1 PIECES
- `tech_base` — 1 PIECES

## Use

1. Build with Geode SDK 5.8.2 targeting Geometry Dash 2.2081.
2. Open the Geometry Dash level editor.
3. Tap the **TB** button.
4. Enter a template as `library:piece`, for example `hell_temp:0`.
5. Leave X/Y blank to place at the editor's most recent click position, or enter explicit level coordinates.
6. Press **Place**.

The mod uses Geometry Dash's native `EditorUI::pasteObjects` path so the serialized object properties stored in the TBLIB files are preserved rather than manually reconstructing each object.
