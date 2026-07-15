Scriptname DragonBoardVR Native Hidden

; Native DragonBoardVR RmlUi panel API for Papyrus-only mods.
; The receiver must be an Alias with a script that implements:
; Event OnDragonBoardPanelEvent(Int panel, String eventType, String elementId, String value, Float numericValue)

Bool Function IsInstalled() Global Native

Int Function RegisterPanel(Alias receiver, String panelId, String documentPath) Global Native

Bool Function UnregisterPanel(Alias receiver, Int panel) Global Native
Bool Function ShowPanel(Int panel) Global Native
Bool Function HidePanel(Int panel) Global Native
Bool Function IsPanelVisible(Int panel) Global Native

Bool Function SetElementText(Int panel, String elementId, String text) Global Native

Bool Function SetElementAttribute(Int panel, String elementId, String name, String value) Global Native

Bool Function RemoveElementAttribute(Int panel, String elementId, String name) Global Native

Bool Function SetElementClass(Int panel, String elementId, String className, Bool enabled) Global Native
