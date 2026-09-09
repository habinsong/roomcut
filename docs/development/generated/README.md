# 현재 구현 전체 목록

`python3 scripts/update-development-docs.py`로 생성합니다. 직접 수정하지 마세요.

총 441개 파일: 텍스트 원문 429개, 바이너리 12개.
텍스트 총 63594행.

추적 파일과 Git에서 무시하지 않는 미추적 파일의 **현재 작업 트리**를 수집합니다.
로컬 `AGENTS.md`도 포함합니다. 원문은 `source/`에 바이트 그대로 보존합니다.
바이너리는 원본 링크·바이트 수·SHA-256으로 기록하며 중복 복사하지 않습니다.
빌드·캐시 등 Git 무시 파일, 비밀 파일, 생성 문서 자신은 수집 대상이 아닙니다.
수동 개발 문서와 검증 자료는 포함하며, 심볼릭 링크는 오류로 보고합니다.

`--check`는 파일 추가·수정·삭제와 생성 원문의 변조·누락을 검사합니다.
이 목록은 파일의 존재와 원문 일치를 보장하는 도구이며 동작 검증 결과가 아닙니다.

| 폴더 | 파일 수 |
|---|---:|
| [.](inventory/index.md) | 7 |
| [.github/workflows](inventory/.github/workflows/index.md) | 1 |
| [apps/macos/NowPlayingHelper](inventory/apps/macos/NowPlayingHelper/index.md) | 3 |
| [apps/macos/Roomcut](inventory/apps/macos/Roomcut/index.md) | 61 |
| [apps/macos/RoomcutTests](inventory/apps/macos/RoomcutTests/index.md) | 22 |
| [core/dsp](inventory/core/dsp/index.md) | 14 |
| [core/presets](inventory/core/presets/index.md) | 2 |
| [core/tests](inventory/core/tests/index.md) | 14 |
| [docs](inventory/docs/index.md) | 6 |
| [docs/development](inventory/docs/development/index.md) | 2 |
| [docs/development/architecture](inventory/docs/development/architecture/index.md) | 1 |
| [docs/development/issues](inventory/docs/development/issues/index.md) | 1 |
| [docs/development/plan](inventory/docs/development/plan/index.md) | 1 |
| [docs/development/references](inventory/docs/development/references/index.md) | 1 |
| [docs/development/verification](inventory/docs/development/verification/index.md) | 20 |
| [docs/development/verification/raw/2026-09-08](inventory/docs/development/verification/raw/2026-09-08/index.md) | 10 |
| [docs/development/verification/raw/2026-09-08-app-polling](inventory/docs/development/verification/raw/2026-09-08-app-polling/index.md) | 7 |
| [docs/development/verification/raw/2026-09-08-bypass-ownership](inventory/docs/development/verification/raw/2026-09-08-bypass-ownership/index.md) | 8 |
| [docs/development/verification/raw/2026-09-08-client-boundary](inventory/docs/development/verification/raw/2026-09-08-client-boundary/index.md) | 10 |
| [docs/development/verification/raw/2026-09-08-client-volume](inventory/docs/development/verification/raw/2026-09-08-client-volume/index.md) | 10 |
| [docs/development/verification/raw/2026-09-08-device-control-results](inventory/docs/development/verification/raw/2026-09-08-device-control-results/index.md) | 11 |
| [docs/development/verification/raw/2026-09-08-device-services](inventory/docs/development/verification/raw/2026-09-08-device-services/index.md) | 9 |
| [docs/development/verification/raw/2026-09-08-device-writes](inventory/docs/development/verification/raw/2026-09-08-device-writes/index.md) | 8 |
| [docs/development/verification/raw/2026-09-08-engine-commands](inventory/docs/development/verification/raw/2026-09-08-engine-commands/index.md) | 10 |
| [docs/development/verification/raw/2026-09-08-hello](inventory/docs/development/verification/raw/2026-09-08-hello/index.md) | 10 |
| [docs/development/verification/raw/2026-09-08-meter-polling](inventory/docs/development/verification/raw/2026-09-08-meter-polling/index.md) | 8 |
| [docs/development/verification/raw/2026-09-08-output-recovery](inventory/docs/development/verification/raw/2026-09-08-output-recovery/index.md) | 7 |
| [docs/development/verification/raw/2026-09-08-roomtune-input](inventory/docs/development/verification/raw/2026-09-08-roomtune-input/index.md) | 7 |
| [docs/development/verification/raw/2026-09-08-roomtune-lifecycle](inventory/docs/development/verification/raw/2026-09-08-roomtune-lifecycle/index.md) | 6 |
| [docs/development/verification/raw/2026-09-08-sound-check](inventory/docs/development/verification/raw/2026-09-08-sound-check/index.md) | 22 |
| [docs/development/verification/raw/2026-09-10-dsp-cost](inventory/docs/development/verification/raw/2026-09-10-dsp-cost/index.md) | 2 |
| [driver/RoomcutHAL](inventory/driver/RoomcutHAL/index.md) | 2 |
| [driver/RoomcutHAL/include](inventory/driver/RoomcutHAL/include/index.md) | 2 |
| [driver/RoomcutHAL/src](inventory/driver/RoomcutHAL/src/index.md) | 4 |
| [engine](inventory/engine/index.md) | 1 |
| [engine/client](inventory/engine/client/index.md) | 5 |
| [engine/client/include](inventory/engine/client/include/index.md) | 1 |
| [engine/include](inventory/engine/include/index.md) | 33 |
| [engine/src](inventory/engine/src/index.md) | 22 |
| [engine/tests](inventory/engine/tests/index.md) | 29 |
| [icon](inventory/icon/index.md) | 1 |
| [icon/app](inventory/icon/app/index.md) | 11 |
| [scripts](inventory/scripts/index.md) | 15 |
| [scripts/release](inventory/scripts/release/index.md) | 4 |
| [scripts/tests](inventory/scripts/tests/index.md) | 1 |
| [shared/protocol](inventory/shared/protocol/index.md) | 5 |
| [shared/schemas](inventory/shared/schemas/index.md) | 4 |
