# ManInTheMeeting

Records a call and transcribes it.

While recording, it grabs two sounds at the same time: whatever is playing on your
PC (the other person's voice) and your microphone (your voice). It mixes them into
one stereo file — the other person on the left channel, you on the right — so the
two voices stay apart.

When recording stops, it writes out the audio and a text transcript next to it. The
speech-to-text runs offline: a small English Whisper model (base.en, quantized,
~57 MB) is built into the .exe, so nothing is downloaded and no internet is needed.

## Build


From an **x64 Native Tools / Developer Command Prompt**, in the project folder:

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
msbuild ManInTheMeeting.vcxproj /p:Configuration=Release /p:Platform=x64 /m
```


## Usage

```
ManInTheMeeting.exe <seconds> <output.m4a>
```

Example — 30-minute recording:

```
ManInTheMeeting.exe 1800 call.m4a
```

Produces `call.m4a` (~22 MB/hr) and `call.txt` (transcript, lines tagged
`[REMOTE]` / `[MIC]`). The transcript is skipped if the Whisper model isn't embedded.

Example `call.txt`:

```
[00:00:04.20] [MIC]    I don't know who you are.
[00:00:06.80] [MIC]    I don't know what you want.
[00:00:09.50] [MIC]    If you are looking for ransom, I can tell you I don't have money.
[00:00:14.30] [MIC]    But what I do have are a very particular set of skills.
[00:00:18.90] [MIC]    Skills I have acquired over a very long career.
[00:00:22.60] [MIC]    Skills that make me a nightmare for people like you.
[00:00:27.10] [MIC]    If you let my daughter go now, that'll be the end of it.
[00:00:31.40] [MIC]    I will not look for you, I will not pursue you.
[00:00:35.80] [MIC]    But if you don't, I will look for you, I will find you
[00:00:42.30] [REMOTE] Good luck.
```

## Transcription model

A [whisper.cpp](https://github.com/ggml-org/whisper.cpp) GGML model,
`vendor/ggml-base.en-q5_1.bin` (~57 MB) — the `base` model, English-only (`.en`),
quantized to `q5_1`. It runs on the CPU, fully offline. 

The `.bin` is linked into the executable as a Win32 `RCDATA`
resource. 

## How to replace it 

Any GGML whisper model works — no code changes, just swap the
embedded file:

1. Get a `ggml-*.bin` model 
2. Drop it in `vendor/`.
3. Point the resource at it in `ManInTheMeeting.rc`, e.g.
   `IDR_WHISPER_MODEL RCDATA "vendor/ggml-small.en-q5_1.bin"`.
4. Rebuild (Release | x64).

Common choices (English, quantized):

| Model            | Size    | Notes                                   |
|------------------|---------|-----------------------------------------|
| `tiny.en-q5_1`   | ~30 MB  | smallest, lowest accuracy               |
| `base.en-q5_1`   | ~57 MB  | current default — balanced              |
| `small.en-q5_1`  | ~180 MB | noticeably better, bigger exe           |

Note: it must be a **GGML whisper** model compatible with the bundled whisper.cpp libs.
Non-whisper engines (Moonshine, Parakeet, etc.) won't load without replacing the
inference code.

## Disclaimer

This tool captures system audio and microphone input and transcribes conversations.
Recording calls is regulated by consent and wiretapping laws that vary by jurisdiction —
many require the consent of one or all parties.

**Before recording, obtain authorization/consent from every other party on the call.**
Use this tool only on systems and conversations you are authorized to record, and only in
compliance with applicable law and policy. It is provided as-is, with no warranty; the
author accepts no liability for misuse.

Lefty @ 2026
