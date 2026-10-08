' Silent merchant launcher. Student launchers and existing shop data stay intact.
Option Explicit
Dim fs, shell, root, executable, candidate, version, dataDirectory, command, argument
Set fs = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
root = fs.GetParentFolderName(WScript.ScriptFullName)
executable = ""
For Each version In Array("merchant-v7", "merchant-v6", "merchant-v5", "merchant-v4", "merchant-v3", "merchant-v2", "merchant-v1")
    candidate = fs.BuildPath(root, "dist\" & version & "\campus-print-merchant.exe")
    If Not fs.FileExists(candidate) Then candidate = fs.BuildPath(root, "dist\" & version & "\campus-print.exe")
    If fs.FileExists(candidate) Then
        executable = candidate
        Exit For
    End If
Next
If executable = "" Then executable = fs.BuildPath(root, "build\ci\bin\campus-print-merchant.exe")
If Not fs.FileExists(executable) Then
    MsgBox "Build the merchant application first. See docs/merchant-frontend.md.", 48, "Paperroom Merchant"
    WScript.Quit 1
End If
' Continue using the first release's persistent queue when it already exists.
dataDirectory = fs.BuildPath(root, "dist\merchant-v1\runtime\shop")
If Not fs.FolderExists(dataDirectory) Then dataDirectory = fs.BuildPath(root, "runtime\merchant-shop")
shell.CurrentDirectory = root
command = Quote(executable) & " --data-dir " & Quote(dataDirectory)
For Each argument In WScript.Arguments
    command = command & " " & Quote(argument)
Next
shell.Run command, 1, False

Function Quote(value)
    Quote = Chr(34) & value & Chr(34)
End Function
