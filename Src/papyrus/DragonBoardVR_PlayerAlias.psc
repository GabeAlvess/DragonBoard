ScriptName DragonBoardVR_PlayerAlias Extends ReferenceAlias

Event OnInit()
    Maintenance()
EndEvent

Event OnPlayerLoadGame()
    Maintenance()
EndEvent

Function Maintenance()
    ; Call VRIK API directly. If VRIK is loaded, it registers.
    VRIK.VrikAddGestureAction("DragonBoardVR_Toggle", "Toggle DragonBoardVR Menu")
EndFunction