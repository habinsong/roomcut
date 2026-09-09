<div align="center">

<img src="../icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

[English](../README.md) · **한국어** · [日本語](README.ja.md) · [Français](README.fr.md) · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge)

Roomcut 1.0.9는 macOS 26 이상을 실행하는 Apple Silicon Mac용입니다.

</div>

> **공식 저장소**
>
> Roomcut은 [habinsong](https://github.com/habinsong)이 만들고 관리합니다. 이 저장소에서 따로 밝히지 않은 복제본·미러·리브랜딩·유사 프로젝트는 Roomcut과 관계없습니다. 소스 코드는 Apache License 2.0으로 배포합니다. Roomcut 이름·브랜딩·스크린샷·문서는 © 2026 송하빈이며 이 라이선스에 포함되지 않습니다.

Roomcut은 macOS 전체에 적용되는 오디오 프로세서입니다. 가상 출력 장치를 하나 만들고,
Mac에서 재생되는 소리를 처리한 뒤 스피커·헤드폰·DAC로 보냅니다. CoreAudio 드라이버를
직접 사용하므로 BlackHole이나 Soundflower 같은 별도 루프백 드라이버가 필요 없습니다.

## 사운드에 맞는 공간감 조절

Focus는 과하게 퍼진 스테레오 이미지를 안쪽으로 모으고, Space는 이미지를 넓힙니다.
프리셋으로 시작한 뒤 필요하면 각 조절기를 직접 다룰 수 있습니다.

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="../icon/app/main_home.png" alt="Roomcut 홈" width="200"><br><sub>홈</sub></td>
<td align="center" valign="middle"><img src="../icon/app/menubar.png" alt="Roomcut 메뉴 막대" width="270"><br><sub>메뉴 막대</sub></td>
<td align="center" valign="middle"><img src="../icon/app/compact_mode.png" alt="Roomcut 축소 모드" width="220"><br><sub>축소 모드</sub></td>
</tr>
</table>
</div>

## 1.0.9에서 달라진 점

- A/B 비교는 A와 B의 편집 이력을 따로 보관하고, 비교 전에 재생 레벨을 맞출 수 있습니다.
  전환에는 짧은 램프를 써서 보통의 비교 과정에서 클릭음이 나지 않게 했습니다.
- 출력 복구, 장치 읽기, 장치 쓰기, 앱 폴링의 책임을 나눴습니다. 늦게 도착한 응답이나
  오래된 쓰기가 새 선택을 덮어쓰지 않도록 한 변경입니다.
- Room Tune은 취소되거나 지연된 측정 회차의 정리 경로를 분명히 했습니다. 임시 우회도
  아직 그 측정이 소유한 경우에만 되돌립니다.
- 릴리스는 `.pkg` 설치 파일과 그 파일을 담은 `.dmg` 두 형태로 제공합니다.

## 할 수 있는 일

- **EQ와 톤 조절.** 10밴드 그래픽 EQ, 6밴드 파라메트릭 EQ, 프리앰프, 출력 트림,
  리미터와 Bass·Warmth·Vocal·Clarity·Air 빠른 조절을 함께 제공합니다.
- **공간 조절.** Focus로 이미지를 모으거나 Space로 넓힙니다. Center, Damping,
  크로스피드, 스피커/헤드폰 모드, 서라운드는 필요할 때만 꺼내 쓰면 됩니다.
- **A/B와 실행 취소.** 두 가지 소리 설정을 보관하고 한쪽을 다른 쪽으로 복사하거나,
  레벨을 맞춰 비교할 수 있습니다. 한 번의 드래그는 한 번의 편집으로 되돌아갑니다.
- **Room Tune.** Continuity Camera로 iPhone을 마이크처럼 사용해 방을 측정합니다.
  뚜렷한 공진은 깎는 방향으로만 보정해 프리셋으로 저장합니다. 보정 마이크를 대신하는
  기능은 아니라는 점도 분명히 합니다.
- **Now Playing.** 메뉴 막대 창에서 앨범 아트, 재생 조작, [LRCLIB](https://lrclib.net)의
  싱크 가사를 볼 수 있습니다.
- **프리셋과 기기별 설정.** 기본 프리셋에서 시작해 직접 저장하고 JSON으로 주고받을 수
  있습니다. 출력 장치마다 프리셋을 기억하게 할 수도 있습니다.
- **Inspect.** 피크, RMS, 스테레오 폭, 상관도, 샘플레이트, 지연, 리미터 동작,
  드롭아웃을 읽기 전용으로 확인합니다.
- **다섯 가지 인터페이스 언어.** 영어·한국어·일본어·프랑스어·독일어를 지원하며,
  시스템 언어를 따르거나 Settings에서 직접 고를 수 있습니다.

## 동작 방식

macOS는 가상 장치인 **Roomcut Output**으로 오디오를 보냅니다. `Roomcut.driver`는
`coreaudiod` 안에서 실행되며 공유 링 버퍼로 프레임을 넘깁니다. `RoomcutAudioEngine`이
DSP를 적용하고 실제 출력 장치로 렌더링합니다.

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

## 요구 사항

- Apple Silicon
- macOS 26 (Tahoe) 이상
- 소스 빌드 시 Xcode 26과 CMake

## 설치

### 릴리스에서 설치

[Releases](https://github.com/habinsong/roomcut/releases)에서 `Roomcut-1.0.9.pkg` 또는
`Roomcut-1.0.9.dmg`를 받습니다. 디스크 이미지를 받았다면 열어서 안의 패키지를
더블클릭하세요. 설치 프로그램은 앱을 `/Applications`에 넣고 가상 드라이버와 엔진을
설치한 뒤 `coreaudiod`를 재시작합니다. 잠시 소리가 멈출 수 있습니다.

이 빌드는 ad-hoc 서명이고 공증되지 않았습니다. macOS가 패키지를 막으면 Control-클릭 후
**열기**를 선택하고, 필요하면 **개인정보 보호 및 보안**에서 승인하세요. 터미널에서는 다음처럼
설치할 수 있습니다.

```sh
sudo installer -pkg Roomcut-1.0.9.pkg -target /
```

Applications에서 Roomcut을 엽니다. Dock 아이콘 없이 메뉴 막대에서 실행됩니다. 그다음
System Settings → Sound에서 **Roomcut Output**을 선택하거나 앱이 선택하게 두면 됩니다.

### 소스에서 빌드

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

설치하면 `coreaudiod`가 재시작됩니다. 현재 오디오 설정을 바꾸고 싶지 않다면 마지막 설치
명령만 빼고 빌드할 수 있습니다.

## 사용 방법

1. Mac의 출력 장치로 **Roomcut Output**을 선택합니다.
2. Roomcut이 렌더링할 실제 장치를 고르고 처리를 켭니다. 처리를 꺼도 앱은 종료되지 않고
   바이패스만 됩니다.
3. 헤드폰이나 스피커에 맞는 프리셋에서 시작하고, 들리는 이유가 있을 때만 조절합니다.

다섯 개 탭은 역할을 겹치지 않게 나뉘어 있습니다.

- **Home**: Now Playing, 처리 스위치, 빠른 톤 조절, 볼륨, 프리셋, 펼칠 수 있는 전체 EQ
- **Space**: 스테레오 폭, 센터 포커스, 댐핑, 크로스피드, Focus/Widen 시작점
- **Tune**: iPhone 측정과 결과 프리셋 저장
- **Inspect**: 엔진 상태를 바꾸지 않고 읽어 주는 미터
- **Settings**: 출력 장치·포맷, 기기별 프리셋, 시작 설정, 외관, 언어, 프리셋 파일, 가사 캐시

사운드 시트를 끝까지 펼치면 **A/B**가 보입니다. A와 B는 각각 독립된 편집 이력을 가지며
`⌘Z`와 `⇧⌘Z`는 현재 선택한 쪽에 적용됩니다. 레벨 매칭은 공정한 비교에 도움이 되지만,
실제 재생 신호가 있어야 매칭 상태를 알려 줄 수 있습니다.

## 개인정보와 한계

오디오 처리와 분석은 Mac 안에서 끝납니다. 로그에는 오디오 샘플이 아닌 카운터와 장치 이름만
남습니다. Room Tune은 측정할 때만 iPhone 마이크를 사용합니다. 가사는 제목·아티스트·길이로
LRCLIB에 요청한 뒤 로컬에 저장합니다. LRCLIB에 없는 곡은 표시되지 않을 수 있습니다.

Roomcut은 Apple의 비공개 MediaRemote 프레임워크로 Now Playing 정보를 읽습니다. 그래서
Mac App Store에 낼 수 없으며 macOS 업데이트가 오디오 경로와 별개로 Now Playing 화면에
영향을 줄 수 있습니다.

## 빌드와 테스트

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release
cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

전체 소스 목록, 개발 기록, 검증 기록, 아직 남은 하드웨어 검증 범위는
[docs/development](development/README.md)에 있습니다.

## 제거

```sh
sudo bash scripts/uninstall-driver.sh
```

가능한 경우 이전 출력 장치를 복구하고 `coreaudiod`를 재시작합니다. 필요하면 System Settings →
Sound에서 출력 장치를 다시 고르세요.

## 라이선스와 출처

Roomcut은 Apache License 2.0으로 배포합니다. [LICENSE](../LICENSE)를 참고하세요.
출처 표기와 상표 고지는 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)에 있으며,
두 파일은 앱과 설치 프로그램에 함께 들어갑니다.
