# Action bar paging

## Intent

Players get 24 action slots, but the second bar is hidden by default to keep the HUD slim. The main
bar can now show the second bar's slots in place of its own, so all 24 slots are reachable without
a second row of buttons on screen.

## Behaviour

- The main bar has a page (1 or 2). Its twelve buttons stand for slots 1-12 on page 1 and slots
  13-24 on page 2: icons, tooltips, cooldowns, costs, clicks and drag & drop all follow the page.
- Three key bindings in the Action Bar category, no separate mode option. The mode is whichever
  the player binds:
  - `ACTIONBARPAGE1` / `ACTIONBARPAGE2` switch the page until it is switched again
    (default Shift-1 / Shift-2).
  - `ACTIONBARPAGE2HOLD` shows page 2 while the key is held (unbound by default).
- A small plate left of the health rail shows the page number, quiet on page 1 and lit on page 2.
  It is deliberately not the level diamond of the target frame, so it is not read as a level.
- The page is not saved; the main bar starts on page 1 after every login.
- The second bar, when enabled in the options, still shows slots 13-24, and the Ctrl-1 ... Ctrl-=
  bindings of those slots stay.

## Approaches

1. **Page offset on the main bar's buttons (chosen).** `ActionButton_GetSlot` maps a button to its
   slot from the bar's page. UI only, the client's action bar API already takes slot indices.
2. Swapping the two bar frames. Rejected: the request is that the UI element stays the same.
3. A page concept in the client's C++ `ActionBar`. Rejected: nothing outside the UI needs it.

## Key handling (client C++)

- A modifier key can be bound on its own: during a key capture it is taken when it is released
  without another key having been pressed (`WorldState::OnKeyUp`).
- A modifier bound on its own stops forming combinations (`DecorateWithModifiers`): with Shift bound
  to "hold", Shift + 1 is the plain key 1 and triggers button 1 of page 2, even though Shift-1 has a
  default binding. Shift-M and other Shift combinations do not fire while Shift is bound alone.
- Key presses on action buttons always use the slot. Only a Shift-click picks a button up.

## Non-goals

More than two pages, saving the page, a page per stance or class.
