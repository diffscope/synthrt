# util sublibraries for ds-infer.
# Relocated from domains/ds-infer/util/ in v4 Phase 7. These remain separate
# static libraries because they are consumed by plugins (inferutil) and tools
# (wavfile) via BUILD_INTERFACE targets.
add_subdirectory(inputparser)
add_subdirectory(inferutil)
# WavFile subdirectory removed in P4-3: dsinfer-cli now uses srt::audio::AudioFileWriter
# (libsndfile backend) directly. dr_wav third-party code eliminated.
