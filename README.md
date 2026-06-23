# keeterPrime

Tiny Windows voice-to-text hotkey app built around NVIDIA Parakeet.

Hold `Ctrl+\`` to talk. Release the keys and keeter records the clip, sends it to `parakeet-cli.exe`, then pastes the transcript into the active window.

## What It Is

* Optional install script included for daily use.
* Single-file C program using basic Win32 + WinMM.
* Local transcription through the Parakeet CLI and `.gguf` model.
* No cloud service, no background account, no app UI beyond the console.

## Running

1. Run `keeter.exe`.
2. Hold `Ctrl+\`` to record, release to transcribe and paste.

On first run, if the Parakeet CLI or model is missing, `keeter.exe` runs `download.cmd`. The script downloads the Parakeet v0.3.2 Windows CPU bundle and `tdt-0.6b-v3-q8_0.gguf` model into the directory above `keeterPrime`.

## Building

Run:

```bat
build.cmd
```

This uses the bundled `cpc.exe` compiler and builds:

```text
keeter.exe
```

## Config

`options.ini`:

```ini
[debug]
saveWav=0

[parakeet]
cli=..\parakeet-v0.3.2-bin-win-cpu-x64\parakeet-cli.exe
model=..\parakeet-v0.3.2-bin-win-cpu-x64\models\tdt-0.6b-v3-q8_0.gguf
```

Set `saveWav=1` if you want the last captured clip saved as `keeter_capture_debug.wav`.

## Install / Uninstall

To run keeter on Windows startup:

```bat
install.cmd
```

To remove the startup launcher:

```bat
uninstall.cmd
```

## Downloading Parakeet

To prepare the local Parakeet files manually:

```bat
download.cmd
```

This creates or updates:

```text
..\parakeet-v0.3.2-bin-win-cpu-x64\parakeet-cli.exe
..\parakeet-v0.3.2-bin-win-cpu-x64\models\tdt-0.6b-v3-q8_0.gguf
```

## Notes

This is deliberately small and Windows-focused. It's a practical wrapper around microphone capture, Parakeet transcription and clipboard paste.
