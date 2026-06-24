# Visual Language Cookbook

Recipes harvested from the project's existing polished layouts. Use these combinations so new UI matches the established look. All asset paths and numbers below are taken from shipping layouts — confirm a texture exists and preview it before using, but prefer this vocabulary over introducing new art.

There are two parallel art sets: the **`fg4_*` family under `Interface/GameUI/`** for in-world game UI, and a **GlueUI set under `Interface/GlueUI/`** for login/character screens. Stay within one set per screen.

## Canonical Asset Families (GameUI, `fg4_*`)

| Purpose | Texture | `borderSize` | Notes |
|---|---|---|---|
| Panel / window background | `Interface/GameUI/fg4_borders_01_19.htex` | `25` | The default panel border. The single most-used border in the project. |
| Recessed content well (lists, text areas) | `Interface/GameUI/fg4_borders_insetBlackSmall.htex` | `11` | Dark inset that reads as "sunken." Pair inside a panel. |
| Larger recessed well | `Interface/GameUI/fg4_borders_insetBlack.htex` | `24` | Same idea, heavier border. |
| Icon / ability slot | `Interface/GameUI/fg4_iconsSlot.htex` | `64` (or `32`) | Frames a square icon. |
| Small button | `Interface/GameUI/fg4_buttonSmall1_{Up,Over,Down}_result.htex` | `22` | The standard button family (Up/Over/Down + a Disabled variant). |
| Tooltip border | `Interface/GameUI/fg4_borders_tooltip.htex` | `64` | Use for hover tooltips only. |
| Selected-row border | `Interface/GameUI/fg4_borders_01_28_2_result.htex` | `24` | A distinct border used for the *checked* state of selectable rows. |
| Progress fill | `Interface/fg4_gradientWhiteV1_result.htex` | — | `ImageComponent` with `tiling="HORZ"` + a `tint`; or `fg4_progressbar1.htex`. |

GlueUI parallels: `ButtonNormal/Hovered/Pushed/Disabled.htex` (`borderSize 30`), `PrimaryButton*.htex` (`64`), `RealmListBorder.htex` (`8`), `TextFieldBorder.htex` (`12`), `CharButton*.htex` (`22`).

## Palette (AARRGGBB)

| Role | Value | Where |
|---|---|---|
| Heading / emphasis text | `FFFFD100` | Most-used label color (gold). Variants: `FFEED37C`, `FFFFCC00`, `FFFACC12`. |
| Body / active text | `FFFFFFFF` | Default and hovered text. |
| Dimmed / pushed text | `FFAAAAAA`, `FFD0D0D0`, `FF808080` | Disabled or pressed captions. |
| Translucent dark fill | `50000000`, `88404040` | Background tint behind a border so content reads over the world. |
| Warm highlight tint | `FFFF9B38`, `FF9E5E22`, `FFB07030`, `A0553300` | Hover/selected highlight on a re-tinted border. |
| Success / error | `FF00FF00` / `FFFF4444` | Status text only — never decorative. |

## Recipe: Window Panel With An Inset Content Well

The standard "framed window with a sunken list/area inside." Outer panel border, then a smaller inset-black border for the content region.

```xml
<ImagerySection name="Background">
    <BorderComponent texture="Interface/GameUI/fg4_borders_01_19.htex" borderSize="25" tint="50000000" />
</ImagerySection>
<ImagerySection name="ContentWell">
    <BorderComponent texture="Interface/GameUI/fg4_borders_insetBlackSmall.htex" borderSize="11">
        <Area><Inset all="12" /></Area>
    </BorderComponent>
</ImagerySection>
```

The inset border lives in the **same frame** via a component `<Inset>`, not a stacked translucent border — so no background bleeds through its center. Use a nested `<Frame>` only when the inner region needs its own anchors or input.

## Recipe: Selectable Row / Tab (the project's idiom)

From `LootMethodFrame.xml`, `SpellBook.xml`, `TalentFrame.xml`. A `CheckboxRenderer` button that **re-tints one border per state** instead of swapping textures, frames an icon in an inset well, and insets the caption to clear the icon. This is the look to copy for lists and tabs.

```xml
<!-- One border, three tints for Normal / Hovered / Pushed -->
<ImagerySection name="NormalBg">
    <BorderComponent texture="Interface/GameUI/fg4_borders_01_19.htex" borderSize="25" tint="50000000" />
</ImagerySection>
<ImagerySection name="HoveredBg">
    <BorderComponent texture="Interface/GameUI/fg4_borders_01_19.htex" borderSize="25" tint="A0553300" />
</ImagerySection>
<ImagerySection name="PushedBg">
    <BorderComponent texture="Interface/GameUI/fg4_borders_01_19.htex" borderSize="25" tint="A0221100" />
</ImagerySection>
<!-- A DIFFERENT border marks the selected (checked) state -->
<ImagerySection name="SelectedBg">
    <BorderComponent texture="Interface/GameUI/fg4_borders_01_28_2_result.htex" borderSize="24" />
</ImagerySection>

<ImagerySection name="IconSlot">
    <BorderComponent texture="Interface/GameUI/fg4_borders_insetBlackSmall.htex" borderSize="11">
        <Area><Inset all="12" /><Size><AbsDimension x="120" y="120" /></Size></Area>
    </BorderComponent>
</ImagerySection>
<ImagerySection name="IconImage">
    <!-- icon inset further so the black well frames it -->
    <ImageComponent tiling="NONE">
        <Area><Inset all="26" /><Size><AbsDimension x="96" y="96" /></Size></Area>
    </ImageComponent>
</ImagerySection>
<ImagerySection name="Caption">
    <!-- left inset clears the icon column -->
    <TextComponent color="FFFFD100" horzAlign="LEFT" vertAlign="CENTER">
        <Area><Inset left="148" top="8" right="12" bottom="8" /></Area>
    </TextComponent>
</ImagerySection>
```

Then compose states (note the per-`Section` `color` override is legal and used to dim the caption while pushed):

```xml
<StateImagery name="Normal">
    <Layer>
        <Section section="NormalBg" /><Section section="IconSlot" />
        <Section section="IconImage" /><Section section="Caption" />
    </Layer>
</StateImagery>
<StateImagery name="Pushed">
    <Layer>
        <Section section="PushedBg" /><Section section="IconSlot" />
        <Section section="IconImage" /><Section section="Caption" color="FFAAAAAA" />
    </Layer>
</StateImagery>
<StateImagery name="NormalChecked">
    <Layer>
        <Section section="SelectedBg" /><Section section="IconSlot" />
        <Section section="IconImage" /><Section section="Caption" />
    </Layer>
</StateImagery>
<!-- ...Hovered, Disabled, HoveredChecked, PushedChecked, DisabledChecked likewise -->
```

Drive it from Lua with `SetChecked(i == selected)` on every refresh (see the renderers reference).

## Do

- Re-**tint one border** across Normal/Hovered/Pushed; reserve a *distinct border texture* for the selected/checked look. Cheaper and more consistent than four separate textures.
- Layer a translucent dark fill (`50000000`-ish) behind the panel border so text stays readable over the 3D world.
- Frame icons in an inset-black well that is larger than the icon (well at `Inset all="12"`, icon at `Inset all="26"`), so a visible dark margin surrounds the art.
- Use gold (`FFFFD100`) for headings/labels and white (`FFFFFFFF`) for active/hover text; dim to `FFAAAAAA` for pressed/disabled.
- Keep `borderSize` equal to the texture's real corner thickness (`fg4_borders_01_19` → 25, `insetBlackSmall` → 11). Wrong sizes smear the corners.
- Match `tooltip` border to tooltips, `iconsSlot` to slots — don't repurpose a button texture as a panel.

## Don't

- Don't stack two translucent borders for one boundary — the lower background bleeds through the transparent center. One self-contained border per edge; if an outer ring is intentional, put it on a slightly larger backing frame behind an **opaque** content frame.
- Don't stretch a small-button nine-slice into a wide panel; its center/corners weren't authored for that aspect. Use a panel border instead.
- Don't introduce new colors when a palette entry fits — drift makes the UI look unowned.
- Don't mix the GlueUI and GameUI art sets on the same screen.
- Don't express the selected state by only changing text color; change the border (`SelectedBg`) so selection is legible at a glance.
- Don't hand-tune `borderSize` to "make it look right" — if corners look wrong, the value should match the source art's corner pixels, not a guess.
