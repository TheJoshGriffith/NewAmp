# miniaudio

Vendored from https://github.com/mackron/miniaudio at tag 0.11.21, unmodified:
`miniaudio.h`, `extras/stb_vorbis.c` and `LICENSE` (public domain or MIT-0, at
your choice).

NewAmp uses only the decoders (MP3, FLAC, WAV, Ogg Vorbis). Device I/O, the
engine and encoders are compiled out; see `src/audio/MiniaudioImpl.c`.
