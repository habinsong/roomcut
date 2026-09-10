<div align="center">

<img src="../icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

[English](../README.md) · **한국어** · [日本語](README.ja.md) · [Français](README.fr.md) · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge)

</div>

> **공식 저장소**
>
> Roomcut은 [habinsong](https://github.com/habinsong)이 만들고 관리합니다. 이 저장소에서 따로 밝히지 않은 복제본·미러·리브랜딩·유사 프로젝트는 Roomcut과 관계없습니다. 소스 코드는 Apache License 2.0으로 배포합니다. Roomcut 이름·브랜딩·스크린샷·문서는 © 2026 송하빈이며 이 라이선스에 포함되지 않습니다.

macOS에는 시스템 전체 EQ가 없습니다. 스피커가 120Hz에서 6dB 튀어도 마침 EQ가 달린 앱 안에서나
고치지, 그 밖에서는 방법이 없습니다.

Roomcut은 **Roomcut Output**이라는 출력 장치를 하나 추가합니다. macOS 출력을 여기로 돌려놓으면
Spotify든 Safari든 Zoom이든 알림음이든 전부 DSP 체인을 지나 실제로 듣는 장치로 나갑니다.
이 가상 장치는 저장소 안에 있는 CoreAudio Audio Server Plug-in이라 BlackHole이나 Soundflower를
따로 깔지 않습니다.

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="../icon/app/main_home.png" alt="Roomcut 홈" width="200"><br><sub>홈</sub></td>
<td align="center" valign="middle"><img src="../icon/app/menubar.png" alt="Roomcut 메뉴 막대" width="270"><br><sub>메뉴 막대</sub></td>
<td align="center" valign="middle"><img src="../icon/app/compact_mode.png" alt="Roomcut 축소 모드" width="220"><br><sub>축소 모드</sub></td>
</tr>
</table>
</div>

## 들어 있는 것

**EQ.** 31Hz부터 16kHz까지 그래픽 10밴드, 그 위에 파라메트릭 6밴드(벨, 로우/하이 셸프,
하이패스, 로우패스, 노치). 주파수로 생각하기 싫을 때 쓰라고 Bass·Warmth·Vocal·Clarity·Air
매크로 다섯 개가 관련 밴드를 한꺼번에 움직입니다. 벨과 셸프 밴드는 다이내믹으로 바꿀 수 있습니다.
임계값과 범위를 주면 그 대역이 실제로 커질 때만 내려갑니다. 평소엔 괜찮다가 특정 음에서만 튀는
공진에 씁니다. 체인 끝에는 룩어헤드 2ms짜리 리미터가 있습니다.

**스테레오 공간.** 요즘 마스터는 넓게 벌려 놓습니다. 노트북 스피커로 들으면 보컬이 가운데에서
빠져나가기도 합니다. Focus를 올리면 사이드가 안쪽으로 당겨져 보컬이 다시 가운데에 붙습니다.
Space는 반대쪽입니다. 둘 다 사이드 신호에만 작용하니 정가운데 보컬은 슬라이더를 끝까지 밀어도
그대로 나옵니다. 이 조건 때문에 한 번 갈아엎었습니다. 처음에는 미드의 위상을 돌린 복사본을 더해
넓혔는데, 모노 호환은 되지만 이미지가 망가집니다. 슬라이더를 올릴수록 보컬이 왼쪽으로 흘렀습니다.
Space는 ±200, Center와 Damping은 200까지 갑니다. 곡선은 그대로라 쓰던 값은 그대로 들립니다.
같은 탭에 Crossfeed와 스피커/헤드폰 전환이 있습니다.

**음량 맞춘 A/B.** 슬롯 두 개가 각자 편집 이력을 가집니다. `⌘Z`와 `⇧⌘Z`는 지금 보고 있는 쪽에
적용되고, 복사 버튼은 현재 설정을 반대쪽에 넘깁니다. 레벨을 켜면 두 체인을 리미터까지 포함해
같은 구간에서 K-가중 미터로 재고 큰 쪽을 낮춥니다. 음량 말고 소리를 비교하라고 넣었습니다.
전환은 15ms 램프를 지납니다.

**Room Tune.** Continuity Camera로 iPhone을 측정 마이크처럼 씁니다. 스윕을 재생하고 뚜렷한
공진을 찾아 깎는 방향으로만 제안한 뒤 프리셋으로 저장합니다. 이게 무엇이 아닌지는 아래에 적었습니다.

**프리셋.** 25개가 들어 있고 Signature·Apple·Speakers·Headphones로 묶여 있습니다. 직접 저장하고
JSON으로 주고받습니다. 출력 장치마다 프리셋을 고정해 두면 헤드폰을 꽂을 때 그 커브가 돌아옵니다.

**Now Playing과 Inspect.** 메뉴 막대 창에 앨범 아트, 재생 조작, [LRCLIB](https://lrclib.net)의
싱크 가사가 뜹니다. Inspect는 읽기 전용입니다. 피크, RMS, 스테레오 폭, 상관도, 샘플레이트,
리미터 동작, 드롭아웃, 그리고 지연 두 가지를 보여 줍니다. 장치가 보고하는 지연과, Roomcut이
직접 더하는 지연(리미터 룩어헤드 + 변환이 있을 때의 리샘플러)을 따로 표시합니다.

인터페이스 언어는 영어·한국어·일본어·프랑스어·독일어입니다. 기본은 시스템 언어를 따르고
Settings에서 직접 고를 수도 있습니다.

## 설치

[Releases](https://github.com/habinsong/roomcut/releases)에서 `Roomcut-1.1.0.pkg`를 받으세요.
`Roomcut-1.1.0.dmg`는 같은 패키지를 디스크 이미지에 담아 둔 것입니다.

이 빌드는 ad-hoc 서명입니다. Developer ID 인증서가 없고 공증도 받지 않았습니다. 실행이 막히면
앱을 한 번 연 뒤 시스템 설정 → 개인정보 보호 및 보안 → **그래도 열기**를 선택하세요. 그 버튼은
막힌 다음에야 생깁니다.

Gatekeeper를 아예 건너뛰려면 터미널에서 설치하세요. `installer`는 Gatekeeper를 보지 않습니다.

```sh
sudo installer -pkg Roomcut-1.1.0.pkg -target /
```

확인할 수 없다가 아니라 **손상되었다**고 뜨면 다른 문제입니다. 내려받다 깨졌거나 서명이 망가진
경우입니다. 받은 파일부터 확인하세요.

```sh
shasum -a 256 Roomcut-1.1.0.pkg
# 2f45b09f446f42c3c8f3f7649dccf910f6a629785f25152d045b635b7431d693
shasum -a 256 Roomcut-1.1.0.dmg
# b0337c5df3b7a45c36785d27597d61f67719532fb1b3db2137d2eea110fe1cdb
```

설치 프로그램은 앱을 `/Applications`에, 드라이버를 시스템 HAL 폴더에, 백그라운드 엔진을
`/Library/Application Support/Roomcut` 아래에 넣습니다. 그다음 `coreaudiod`를 재시작하니 Mac의
모든 소리가 1초쯤 끊깁니다. Roomcut은 Dock 아이콘 없이 메뉴 막대에서 열립니다. 시스템 설정 →
사운드에서 **Roomcut Output**을 고르거나 앱에 맡긴 뒤, 실제로 소리를 낼 장치를 지정하고 처리를 켜세요.

### 소스에서 빌드

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

Xcode 26과 CMake가 필요합니다. 지금 오디오 설정을 건드리고 싶지 않으면 마지막 줄을 빼세요.
그 전까지는 `build/` 안에만 씁니다.

## 안 되는 것

- **Intel Mac과 이전 macOS.** Apple Silicon, macOS 26 (Tahoe) 이상에서만 돕니다.
- **멀티채널.** 가상 장치는 스테레오입니다. 서라운드 콘텐츠는 macOS가 먼저 다운믹스해서 넘깁니다.
- **앱별 처리.** 시스템 출력 전체이거나 아예 안 쓰거나 둘 중 하나입니다.
- **제대로 된 룸 보정.** Room Tune은 방 안 한 지점에서 폰 마이크로 잽니다. 마이크가 평탄하지도
  않고 한 지점이 방 전체도 아닙니다. 결과는 출발점이지 측정값이 아닙니다. 판단은 귀로 하세요.
- **App Store.** Now Playing이 Apple의 비공개 MediaRemote 프레임워크를 읽습니다. 그래서 Mac App
  Store에 올릴 수 없고, macOS 업데이트가 오디오 경로와 무관하게 이 패널만 망가뜨릴 수 있습니다.
- **앱 종료로 끝나지 않는 구조.** DSP는 백그라운드 데몬에서 돕니다. 앱을 종료해도 소리는 계속
  거기를 지나갑니다. 빼려면 제거해야 합니다.

## 구조

```text
시스템 오디오
  → Roomcut Output                 가상 장치
  → Roomcut.driver                 CoreAudio Audio Server Plug-in
  → 공유 메모리 링 버퍼
  → RoomcutAudioEngine             DSP와 렌더링
  → 스피커 / 헤드폰 / DAC / HDMI
```

| 구성 요소 | 하는 일 | 언어 |
|---|---|---|
| `Roomcut.app` | 메뉴 막대 앱과 Now Playing 창 | Swift (SwiftUI + AppKit) |
| `Roomcut.driver` | 가상 출력 장치 | C |
| `RoomcutAudioEngine` | DSP와 오디오 렌더링 | C++ |
| `RoomcutCore` | DSP·분석·프리셋 | C++ |
| `RoomcutNowPlaying.dylib` | Now Playing 브리지 | Objective-C |

오디오를 소유한 쪽은 앱이 아니라 엔진입니다. LaunchDaemon으로 돌면서 자기 상태 파일을 들고 있고,
드라이버의 쓰기 인덱스와 하트비트를 같이 봅니다. 그래야 "아무것도 재생 중이 아님"과 "드라이버가
사라짐"을 구분합니다. 2026-08-01에 겪은 고장 때문에 감시가 하나 더 붙었습니다. iFi DAC이 48kHz로
열렸다가 곧바로 384kHz로 다시 열렸고, 그때부터 렌더 콜백이 실시간의 3.37배로 돌면서 링이 마르고
언더런이 쌓였습니다. 장치도 그대로, 샘플레이트도 그대로라 아무 검사에도 걸리지 않았습니다. 지금은
프레임이 실제로 빠져나가는 속도를 재서, 숫자가 이상하면 출력 유닛을 다시 만듭니다.

## 개인정보

오디오는 Mac 밖으로 나가지 않습니다. 로그에는 카운터와 장치 이름만 남고 샘플은 없습니다.
Room Tune은 측정하는 동안에만 iPhone 마이크를 엽니다. 네트워크를 쓰는 곳은 가사 하나입니다.
제목·아티스트·길이를 LRCLIB에 보내고, 받은 결과는
`~/Library/Caches/com.habinsong.roomcut/lyrics.json`에 캐시합니다.

## 빌드와 테스트

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release
cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

이 커밋 기준 네이티브 43개, Swift 257개입니다. 여기서 다루지 못하는 것은 실제 방, 실제 마이크,
그리고 다른 Mac입니다. 영역별 검증 기록과 아직 열려 있는 한계는
[docs/development](development/README.md)에 있습니다.

## 제거

```sh
sudo bash scripts/uninstall-driver.sh
```

엔진이 종료되면서 시스템 기본 출력을 원래 쓰던 장치로 돌려놓고, 그다음 드라이버를 지우고
`coreaudiod`를 재시작합니다. 그래도 소리가 안 나면 시스템 설정 → 사운드에서 출력 장치를 다시 고르세요.

## 라이선스

Apache License 2.0입니다. [LICENSE](../LICENSE)를 보세요. 출처 표기와 상표 고지는
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)에 있고, 두 파일 모두 앱과 설치 프로그램에
함께 들어갑니다.
