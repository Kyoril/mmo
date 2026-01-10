# UI Tutorials

These tutorials walk through existing UI components and show how to build similar ones from scratch.

## Tutorial 1: Action Bar Button

References:
- `data/client/Interface/GameUI/ActionBarButton.xml`
- `data/client/Interface/GameUI/ActionBar.lua`

Goal: Build a drag-and-drop action button that shows an icon and a count.

### Layout (XML)

```xml
<Frame name="ActionBarButtonTemplate" type="Button" renderer="ButtonRenderer">
  <Property name="Icon" />
  <Property name="Font" value="SmallLabelFont" />
  <Property name="DragEnabled" value="true" />
  <Property name="DropEnabled" value="true" />

  <Visual>
    <ImagerySection name="Image">
      <ImageComponent>
        <Area><Inset all="16" /></Area>
        <PropertyValue property="Icon" />
      </ImageComponent>
    </ImagerySection>
    <ImagerySection name="Counter">
      <TextComponent color="FFFFFFFF" horzAlign="RIGHT" vertAlign="BOTTOM">
        <Area><Inset all="16" /></Area>
      </TextComponent>
    </ImagerySection>
    <ImagerySection name="NormalFrame">
      <BorderComponent texture="Interface/BW_ButtonSmall_Up.htex" borderSize="30" />
    </ImagerySection>
    <StateImagery name="Normal">
      <Layer>
        <Section section="NormalFrame" />
        <Section section="Image" />
        <Section section="Counter" />
      </Layer>
    </StateImagery>
  </Visual>

  <Area>
    <Size><AbsDimension x="128" y="128" /></Size>
  </Area>

  <Scripts>
    <OnDrag>
      return function(this, button, position)
        ActionBarButton_OnDrag(this, button, position)
      end
    </OnDrag>
    <OnDrop>
      return function(this, button, position)
        ActionBarButton_OnDrop(this, button, position)
      end
    </OnDrop>
  </Scripts>
</Frame>
```

### Behavior (Lua)

```lua
function ActionBarButton_OnDrag(this, button, position)
  PickupActionButton(this.id - 1)
end

function ActionBarButton_OnDrop(this, button, position)
  PickupActionButton(this.id - 1)
end
```

Key takeaways:
- Use `PropertyValue` to bind the icon property into the imagery.
- Use `DragEnabled`/`DropEnabled` plus `OnDrag`/`OnDrop` scripts to wire drag logic.
- `ButtonRenderer` expects `Normal`, `Hovered`, `Pushed`, and `Disabled` states.

## Tutorial 2: Chat Log with Input

References:
- `data/client/Interface/GameUI/ChatFrame.xml`
- `data/client/Interface/GameUI/ChatFrame.lua`

Goal: Build a scrolling chat log with an input box.

### Layout (XML)

```xml
<Frame name="ChatFrame" parent="GameParent" type="ScrollingMessageFrame">
  <Property name="Font" value="ChatFont" />
  <Area>
    <Anchor point="LEFT" offset="64" />
    <Anchor point="BOTTOM" offset="-96" />
    <Anchor point="RIGHT" offset="-64" />
    <Anchor point="TOP" offset="-320" />
  </Area>
  <Scripts>
    <OnLoad>
      return function(this)
        ChatFrame_OnLoad(this)
      end
    </OnLoad>
  </Scripts>
</Frame>

<Frame name="ChatInput" type="TextField" renderer="TextFieldRenderer">
  <Property name="TextSection" value="Text" />
  <Property name="HorzAlign" value="LEFT" />
  <Property name="VertAlign" value="CENTER" />
  <Visual>
    <ImagerySection name="Caret">
      <ImageComponent texture="Interface/GlueUI/Caret.htex" />
    </ImagerySection>
    <StateImagery name="Enabled" />
    <StateImagery name="Caret">
      <Layer><Section section="Caret" /></Layer>
    </StateImagery>
  </Visual>
  <Scripts>
    <OnEnterPressed>
      return function(this)
        ChatFrame_SendMessage(this)
      end
    </OnEnterPressed>
  </Scripts>
</Frame>
```

### Behavior (Lua)

```lua
function ChatFrame_OnLoad(self)
  self:RegisterEvent("CHAT_MSG_SAY", ChatFrame_OnMessage)
end

function ChatFrame_OnMessage(self, sender, text)
  self:AddMessage(sender .. ": " .. text, 1.0, 1.0, 1.0)
end

function ChatFrame_SendMessage(input)
  local text = input:GetText()
  if text ~= "" then
    SendChatMessage(text, "SAY")
    input:SetText("")
  end
end
```

Key takeaways:
- `ScrollingMessageFrame` provides `AddMessage`, `ScrollUp`, `ScrollDown`.
- `TextFieldRenderer` uses `Enabled` and `Caret` state imagery.
- Hook `OnEnterPressed` to send messages.

## Tutorial 3: Side Panel Window

References:
- `data/client/Interface/GameUI/GameTemplates.xml`
- `data/client/Interface/GameUI/GameParent.lua`

Goal: Create a new left-side panel using the existing panel system.

### Layout (XML)

```xml
<Frame name="MySidePanel" inherits="SidePanelTemplate">
  <Frame name="MySidePanelTitle" inherits="TitleBar">
    <Property name="Text" value="MY_PANEL_TITLE" />
    <Area>
      <Anchor point="TOP" offset="24" />
      <Anchor point="LEFT" offset="24" />
      <Anchor point="RIGHT" offset="-24" />
    </Area>
  </Frame>

  <Frame name="MySidePanelContent" renderer="DefaultRenderer">
    <Area>
      <Anchor point="TOP" relativePoint="BOTTOM" relativeTo="MySidePanelTitle" offset="16" />
      <Anchor point="LEFT" offset="24" />
      <Anchor point="RIGHT" offset="-24" />
      <Anchor point="BOTTOM" offset="-24" />
    </Area>
  </Frame>
</Frame>
```

### Behavior (Lua)

```lua
UIPanelWindows["MySidePanel"] = { area = "left", pushable = 1 }

function MySidePanel_Toggle()
  if MySidePanel:IsVisible() then
    HideUIPanel(MySidePanel)
  else
    ShowUIPanel(MySidePanel)
  end
end
```

Key takeaways:
- `SidePanelTemplate` already wires the close button via `SidePanel_OnLoad`.
- Register the panel in `UIPanelWindows` so `ShowUIPanel` handles positioning.
- Use `ShowUIPanel` / `HideUIPanel` instead of calling `Show()` directly.
