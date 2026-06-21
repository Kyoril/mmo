# HTEX Workflow

Use `.agents/skills/mmo-material-editor/scripts/htex_tool.py` for metadata and previews. Never judge a texture by filename or patch HTEX bytes manually.

```powershell
python .agents/skills/mmo-material-editor/scripts/htex_tool.py inspect `
  data/client/Interface/GameUI/texture.htex --json

python .agents/skills/mmo-material-editor/scripts/htex_tool.py preview `
  data/client/Interface/GameUI/texture.htex `
  --output artifacts/texture.png
```

Prefer existing textures when they fit the visual language and nine-slice behavior. Preview source dimensions and transparency, then test the actual `BorderComponent` border size or `ImageComponent` inset in game.

To add artwork:

1. Create a source PNG at the intended aspect ratio, with transparent padding where nine-slice corners require it.
2. Import it through `mmo_edit` as texture type `Color (Diffuse, Albedo, UI)`.
3. Preserve alpha; use compression only when its artifacts are acceptable for line art and textural borders.
4. Place the resulting HTEX under the appropriate `data/client/Interface` directory.
5. Preview the imported HTEX and inspect dimensions/format.
6. Reference it with an asset-root-relative path such as `Interface/GameUI/Name.htex`.

For scalable panels and buttons, use `BorderComponent` and choose border sizes based on immutable corner thickness. For icons and fixed ornaments, use `ImageComponent`. Do not stretch a square button texture into a tab unless its corners and center were designed for nine-slice scaling.
