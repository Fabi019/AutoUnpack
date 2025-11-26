# AutoUnpack

Small and lightweight (~12kb) win32 application that automatically extracts archives (zip, rar, 7z, tar, etc.) when they are downloaded to your computer.
Links to **msvcrt** to generate a minimal binary without requiring an additional c-runtime (CRT/STL) installed.

Note: The application uses [7-Zip](https://www.7-zip.org/) for the extraction and needs to be installed on your computer.

## Settings

All settings are loaded at the start of the program from a settings.ini file. This file has to be in the same folder as the executable. When the program can't find the file, it uses default hardcoded settings. To adjust them, click on the Settings entry in the tray menu. If no config file exists, it will ask you to create one. After pressing Yes a editor with the file should open up. The contents will look like the following:

```ini
[Settings]
Folders=%userprofile%\Downloads     ; Comma separated list of folders to monitor
Extensions=.zip,.7z                 ; Comma separated list of archive extensions to monitor
WaitTimeMs=1000                     ; Time in ms to wait after the file was created/modified before starting extraction
MaxFileSizeMB=250                   ; Maximum file size in MB to extract
ZipExe=7z.exe                       ; Path to zip extractor executable
DeleteAfter=0                       ; 1 = delete archive after extraction, 0 = keep archive
AutoStart=0                         ; 1 = start with Windows, 0 = do not start with Windows
EfficiencyMode=1                    ; 1 = enable the EcoQos mode for lower CPU usage
```

Enabling `AutoStart` will create a entry in the Registry at `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`. This will enable the appilcation to launch when the computer starts. Disabling this setting will remove the entry on the next start of the application.