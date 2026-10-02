' Silent launcher: only the native GUI is visible, including on a double-click.
Option Explicit
Dim fs, shell, root, executable, candidate, version, command, argument
Set fs = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
root = fs.GetParentFolderName(WScript.ScriptFullName)
executable = ""
For Each version In Array("editorial-v10", "editorial-v9", "editorial-v8", "editorial-v7", "editorial-v6", "editorial-v5", "editorial-v4", "editorial-v3", "editorial-v2")
    candidate = fs.BuildPath(root, "dist\" & version & "\campus-print.exe")
    If fs.FileExists(candidate) Then
        executable = candidate
        Exit For
    End If
Next
If executable = "" Then executable = fs.BuildPath(root, "build\dev\bin\campus-print.exe")
If Not fs.FileExists(executable) Then
    MsgBox "Build the application first. See docs/editorial-frontend.md.", 48, "Paperroom"
    WScript.Quit 1
End If
shell.CurrentDirectory = root
command = Quote(executable) & " --role student --data-dir " & Quote(fs.BuildPath(root, "runtime\editorial-shop"))
For Each argument In WScript.Arguments
    command = command & " " & Quote(argument)
Next
shell.Run command, 1, False

Function Quote(value)
    Quote = Chr(34) & value & Chr(34)
End Function
