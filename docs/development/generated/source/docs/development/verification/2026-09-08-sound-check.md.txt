# 2026-09-08 실제 앱과 오디오 경로 확인

사용자가 실제 앱 조작·사운드 체크를 목표에 추가해 현재 앱을 빌드하고 설치된 엔진과 실제 출력 경로를 검사했습니다.
원래 볼륨·EQ·PEQ·장치·샘플레이트는 유지했으며 UI의 처리 우회만 잠시 바꾼 뒤 원래대로 복구했습니다.

## 실행 방식과 코드

새 `--attach-engine` 옵션은 기존 엔진에 Mach로 접속하고 실제 창을 엽니다.
시작/종료 때 엔진 서비스를 올리거나 내리지 않으며 재시작 예약도 하지 않습니다.
기존 `sudo` 서비스 제어를 실행하지 않고 실제 UI를 검사하기 위한 명시적 진단 옵션입니다.
일반 실행의 서비스 수명 동작은 유지합니다.

`AppLaunch.swift`는 기존 UIFixture에 있던 실행 모드 설정과 평가용 로그를 별도 파일로 이동한 것입니다.
`main.swift`에서 옵션을 읽고 `RoomcutApp.swift`에서 서비스 관리 여부만 판단합니다.
새 책임을 기존 앱 델리게이트에 추가로 누적하지 않으며 별도 IPC는 없습니다.

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer bash scripts/build-app.sh release
open -n build/Roomcut.app --args --attach-engine
```

이번 검사는 최신 앱 복사본의 번들 ID를 `com.roomcut.sound-qa`로 바꿔 로컬 서명 후 실행했습니다.
종료 후에도 설치 엔진 PID 717이 그대로 실행 중인 것을 확인했습니다.
`--attach-engine`은 실제 클라이언트 모드이므로 UI 조작은 실제 엔진에 반영됩니다.

## 확인한 실제 환경

- 시스템 기본 출력: `RoomcutOutput:com.roomcut`.
- 엔진 실제 출력: `iFi USB Audio SE`.
- 출력 형식: 384kHz·24-bit. 현재 볼륨 scalar 0.063488125801086426.
- UI Inspect: 같은 장치·384kHz·24-bit·6% 표시, 장치 지연 구성값 1.8ms 표시.
  이 지연은 전체 왕복 지연의 실측값이 아닙니다.
- 설치 엔진 capability 31이며 현재 작업 트리에서 빌드한 엔진과 SHA-256이 다릅니다.
  따라서 **최신 앱 + 기존 설치 엔진** 검증이지 새 엔진의 실기기 검증은 아닙니다.
  [환경/복구 검사](raw/2026-09-08-sound-check/validation.txt).

## 검사음과 관찰값

48kHz stereo 1kHz 사인, peak 0.01(-40dBFS), 6초, 양 끝 100ms fade인 WAV를 생성했습니다.
볼륨을 올리지 않고 `afplay -v 1`로 시스템 기본 출력에 재생했습니다.
그동안 상태와 분석 API를 약 0.2초마다 읽었습니다. 검사음은 각 조건에서 한 번씩 재생했습니다.

| 조건 | afplay 종료값 | 신호가 있는 관찰 수 | 엔진 최대 피크 | 해당 구간 언더런 증가 |
|---|---:|---:|---:|---:|
| 기존 처리 켬 | 0 | 27 | 0.007373 / -42.647dBFS | 0 |
| UI에서 처리 우회 | 0 | 26 | 0.010000 / -40.000dBFS | 0 |

[처리 켬 원시 상태/분석](raw/2026-09-08-sound-check/processing-on.json),
[우회 원시 상태/분석](raw/2026-09-08-sound-check/processing-bypassed.json),
[측정 스크립트](raw/2026-09-08-sound-check/measure.py.txt).
기존 설정의 결과가 달라지는 경로와 우회 시 입력 피크 보존을 확인했습니다.
이 두 피크만으로 전체 주파수 응답·출력 음질·클릭 유무를 판정하지 않습니다.

실제 앱 Settings에서 처리 Off를 누르고 API의 `manualBypass=true`를 확인한 뒤 우회 검사를 실행했습니다.
이후 같은 UI로 On을 누르고 `manualBypass=false`로 복구했습니다.
EQ·PEQ·볼륨·포맷·기본 출력의 검사 전/복구 후 읽기 결과가 바이트 단위로 동일했습니다.
프리셋/파라미터 revision도 7로 유지됐습니다. 원래 21369이던 누적 언더런은 검사 후에도 같은 값이었습니다.

실제 하드웨어에서 음원을 재생했지만 별도 아날로그/마이크 녹음은 하지 않았습니다.
도구의 엔진 신호 측정 결과와 주관적으로 들었다는 주장은 구분하며, 이번에는 청취 음질 합격을 주장하지 않습니다.

## 빌드와 보존

- [최신 앱 빌드](raw/2026-09-08-sound-check/app-build.txt) 성공.
- 최종 `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release --jobs 4`: [211/211 통과](raw/2026-09-08-sound-check/swift-tests-final.txt).
- `codesign --verify --deep --strict build/Roomcut.app` 종료값 0.
- [앱 접속 직후 서비스](raw/2026-09-08-sound-check/service-after-attach.txt), [앱 종료 후 서비스](raw/2026-09-08-sound-check/service-after-quit.txt): PID 717 유지.
- 평가 앱 두 개 모두 CUA 종료 후 앱 목록에서 사라짐을 확인했습니다.
- 최초 plist 읽기에 `plutil -extract ... json`의 출력 경로를 빠뜨려 권한 오류가 났습니다.
  `-o -`를 지정해 stdout으로 읽었고 시스템 파일은 변경하지 않았습니다.

## 마지막 회귀 검사에서 수정한 대기 조건

전체 재검사에서 `testFailedWriteRestoresEngineParams`가 실패했습니다.
검사는 요청 뒤 고정 40ms만 기다리고 있어 실제 쓰기/복구의 완료를 보장하지 않았습니다.
클라이언트 응답을 80ms 지연시켜 같은 실패를 재현한 뒤 `isSoundWritePending`이 끝날 때까지 제한된 범위에서 기다리도록 바꿨습니다.
동일 입력에서 복구값과 오류 문구 검사가 통과했으며 제품 복구 코드는 변경하지 않았습니다.
[전체 검사 실패](raw/2026-09-08-sound-check/swift-tests-before.txt),
[80ms 지연 재현](raw/2026-09-08-sound-check/restore-wait-before.txt),
[완료 조건으로 검사](raw/2026-09-08-sound-check/restore-wait-after.txt).

## 다음 검증

실제 앱 빌드·조작·신호/사운드 체크를 각 의미 있는 기능의 QA에 포함합니다.
현재 설치 엔진, 작업 트리의 새 엔진, 모의 HAL, 실제 하드웨어, 오프라인 DSP, 청취/녹음 증거는 구분합니다.
새 엔진의 실기기 검증과 설치/업그레이드, 재연결·절전·장시간 재생·전체 주파수/지연/음질 검사는 남아 있습니다.
전체 목표는 활성 상태입니다.
