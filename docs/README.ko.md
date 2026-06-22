<div align="center">

<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

**공간감은 줄이고, 소리는 남깁니다.**

[English](../README.md) · **한국어** · [日本語](README.ja.md) · [Français](README.fr.md) · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge) ![Core Audio plug-in](https://img.shields.io/badge/Core%20Audio-Plug--In-8957E5?style=for-the-badge)

![System-wide](https://img.shields.io/badge/System--wide-EQ%20%2B%20DSP-0A84FF?style=for-the-badge) ![EQ](https://img.shields.io/badge/EQ-10--band%20%2B%20Parametric-5E5CE6?style=for-the-badge) ![Spatial](https://img.shields.io/badge/Spatial-narrow%20%C2%B7%20widen-1F6FEB?style=for-the-badge) ![Room Tune](https://img.shields.io/badge/Room%20Tune-iPhone%20mic-2EA043?style=for-the-badge) ![Now Playing](https://img.shields.io/badge/Now%20Playing-Lyrics-C2185B?style=for-the-badge) ![UI languages](https://img.shields.io/badge/UI-5%20languages-FB8500?style=for-the-badge)

![Native tests](https://img.shields.io/badge/ctest-passing-2EA043?style=for-the-badge) ![Swift tests](https://img.shields.io/badge/swift%20test-passing-2EA043?style=for-the-badge) ![Driver](https://img.shields.io/badge/HAL%20driver-loads-0A84FF?style=for-the-badge)

Roomcut 1.0은 현재 시스템 전역 라우팅, EQ, 리미터, 공간 제어, 분석기, 프리셋, Now Playing, Room Tune을 제공합니다.<br>Apple Silicon Mac(macOS 26 이상)에서 테스트했습니다.

</div>

> **공식 저장소 안내**
>
> 이 저장소는 **Roomcut**의 공식 저장소이며, [habinsong](https://github.com/habinsong)인 제가 직접 만들고 관리합니다.
>
> 이 저장소의 원본 문서, README 텍스트, 아키텍처 설명, 기능 설명, 스크린샷, UI 콘셉트, 프로젝트 메타데이터, 기타 원본 자료를 복제·미러링·리브랜딩하거나 유사하게 모방한 저장소, 패키지, 마켓플레이스 등록물, 웹사이트, 서비스, 기타 프로젝트는 이 저장소에 명시되지 않는 한 본 프로젝트와 무관합니다.
>
> 소스 코드는 Apache License 2.0으로 배포됩니다([LICENSE](../LICENSE) 참고). Roomcut 이름, 브랜딩, 스크린샷, 문서는 © 2026 송하빈이며 해당 라이선스의 적용을 받지 않습니다.

Roomcut은 macOS용 시스템 전역 오디오 프로세서입니다. <br> <br>
가상 출력 장치를 추가해, Mac이 재생하는 모든 소리를 실시간 DSP 체인에 통과시킨 뒤 실제로 사용하는 스피커·헤드폰·DAC로 내보냅니다. <br>
네이티브 CoreAudio Audio Server Plug-in 위에서 동작하므로 <br>
다른 별도의 루프백 드라이버가 필요 없습니다.

대부분의 "오디오 인핸서"는 소리를 바깥으로 넓히기만 합니다. <br>
Roomcut은 양방향으로 움직이고, 그 정도를 직접 정할 수 있습니다. <br><br>

Focus는 과한 공간감과 스테레오 퍼짐을 덜어내 보컬과 대사를 원음에 가깝게 당기고, <br>
Widen은 더 넓은 무대가 필요할 때 펼칩니다. <br><br>
줄이든, 넓히든, 그 사이 어디든 맞출 수 있습니다.<br>

---
#### 쉽게 말해, roomcut 앱과 오디오로 귀가 재밌는 음악을 들으며 <br> 애플 liquid glass UI 기반 UI/UX 로 보기만해도 예쁜 앱입니다.
---

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home.png" alt="메인 화면" width="200"><br><sub>메인 화면</sub></td>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/menubar.png" alt="메뉴바" width="270"><br><sub>메뉴바</sub></td>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/compact_mode.png" alt="축소 모드" width="220"><br><sub>축소 모드</sub></td>
</tr>
</table>
</div>

## 기능

- **글로벌 EQ.** 10밴드 그래픽 EQ와 6밴드 파라메트릭 EQ(벨, 셸프, 하이/로우 패스, 노치)를 함께 쌓고, 실시간 응답 곡선을 보여줍니다.
- **노브** Bass·Warmth·Vocal·Clarity·Air 노브가 알맞은 밴드를 대신 움직여, 주파수를 일일이 떠올리지 않아도 됩니다.
- **양방향 공간 제어.** 이미지를 좁히거나 넓히는 걸 자유롭게: Space(폭), Center(팬텀 센터 포커스), Damping(룸 감쇠), 크로스피드/크로스토크. <br>
스피커·헤드폰 모드 전환과 서라운드 토글을 제공하고, 슬라이더를 움직이면 실시간 스테레오 필드 뷰가 반응합니다. Focus·Widen은 원탭 프리셋.
- **리미터와 게인.** 프리앰프·출력 트림과 피크 리미터로, <br>
과한 EQ 곡선이 출력을 클리핑하지 않게 합니다.
- **고해상도 대응.** 내부적으로 32비트 플로트로 처리하고, 출력 장치의 샘플레이트와 비트 뎁스를 선택할 수 있습니다. <br>
Now Playing 카드에 현재 포맷과 지연이 표시됩니다.
- **분석기.** 실시간 피크, RMS, 스테레오 폭, 스펙트럴 센트로이드,<br>
 스펙트럼 뷰, 그리고 지금 재생 중인 소리를 설명하는 라벨.
- **프리셋.** Signature·Apple 기기·Speakers·Headphones로 묶인 <br>
내장 라이브러리와 직접 저장한 프리셋입니다.
- **Room Tune.** iPhone을 마이크로 써서 방을 측정하고, 룸튜닝을 진행합니다.<br>
 가장 두드러진 공진에 보수적인 EQ 보정을 제안합니다. <br>
깎기만 하고 올리지 않으며, 결과를 프리셋으로 저장합니다.
- **Now Playing.** 아트워크 기반 테마, [LRCLIB](https://lrclib.net)을 통한 싱크 가사, 메뉴바 창의 재생 컨트롤.
- **다국어 지원.** 영어·한국어·일본어·프랑스어·독일어. 시스템 언어를 따르거나 직접 선택할 수 있습니다.
- **안전한 폴백.** 엔진이 비정상 종료되면 출력이 실제 장치로 복구되고, 원본 오디오는 Mac을 떠나지 않습니다.

## 작동 방식

macOS는 가상 장치인 "Roomcut Output"으로 오디오를 보냅니다. 드라이버는 얇은 부분입니다. `coreaudiod` 안에서 실행되고, DSP를 하지 않으며, 들어온 프레임을 공유 링 버퍼로 헬퍼 프로세스에 넘기기만 합니다. 헬퍼인 `RoomcutAudioEngine`이 DSP 체인을 돌리고 실제 출력 장치로 렌더링합니다.

```
시스템 오디오
  → Roomcut Output            가상 장치
  → Roomcut.driver            Audio Server Plug-in, coreaudiod 내부 샌드박스
  → 공유 메모리 링 버퍼        Mach 서비스로 전달
  → RoomcutAudioEngine        DSP + 렌더링
  → 스피커 / AirPods / DAC / HDMI
```

Audio Server Plug-in은 `coreaudiod` 안에서 샌드박스로 돌아 임의의 소켓이나 공유 메모리를 열 수 없습니다. Roomcut은 `AudioServerPlugIn_MachServices` 연결을 통해 링 버퍼를 부트스트랩하며, 이는 Background Music과 같은 방식입니다.

| 구성 요소 | 역할 | 언어 |
|---|---|---|
| `Roomcut.app` | 메뉴바 앱과 Now Playing UI | Swift (SwiftUI + AppKit) |
| `Roomcut.driver` | 가상 출력 장치 (Audio Server Plug-in) | C |
| `RoomcutAudioEngine` | 백그라운드 헬퍼: DSP와 렌더링 | C++ |
| `RoomcutCore` | 공유 DSP·프리셋·분석 | C++ |
| `RoomcutNowPlaying.dylib` | Now Playing용 MediaRemote 브리지 | Objective-C |

## 요구 사항

- macOS 26 (Tahoe) 이상. 인터페이스가 시스템 Liquid Glass API를 사용합니다.
- Apple Silicon.
- 빌드 시: Xcode 26(macOS 26 SDK 포함)과 CMake.

## 설치

두 가지 방법이 있습니다. 지금은 소스에서 빌드하는 쪽을 권장합니다. 내려받은 비공증 바이너리에서 나오는 Gatekeeper 경고를 피할 수 있기 때문입니다.

### 소스에서 빌드

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

# 네이티브 드라이버 + 엔진
cmake -S . -B build
cmake --build build

# 메뉴바 앱
bash scripts/build-app.sh

# 드라이버 + 엔진 설치 (암호를 묻고 coreaudiod를 재시작)
sudo bash scripts/install-driver.sh
```

`build/Roomcut.app`을 엽니다(메뉴바에 있으며 Dock 아이콘은 없습니다). 그런 다음 System Settings ▸ Sound에서 "Roomcut Output"을 선택하거나 앱이 대신 설정하게 둡니다. 설치 시 `coreaudiod`가 재시작되어 시스템 오디오가 1초쯤 끊깁니다.

### 사전 빌드 릴리스

[Releases](https://github.com/habinsong/roomcut/releases)에서 최신 `.pkg`(더블클릭) 또는 `.zip`(터미널)을 받습니다. 사전 빌드는 현재 애플 개발자 서명이 있지 않아, 처음 실행할 때 macOS가 승인을 요구할 수 있습니다. `.pkg`를 우클릭해 열기를 선택하거나 `sudo installer -pkg Roomcut-*.pkg -target /`를 실행하세요. `.zip`에는 나머지를 처리하는 `install.sh`가 들어 있습니다. 소스에서 빌드하면 이 승인 단계가 없습니다.

## 사용법

드라이버를 설치하고 앱을 연 상태에서:

1. System Settings ▸ Sound에서 **Roomcut Output**을 선택합니다(또는 앱이 설정하게 둡니다). 이제 Mac이 재생하는 모든 소리가 Roomcut을 거칩니다.
2. Roomcut이 렌더링할 실제 장치(스피커·헤드폰·DAC)를 정하고 Roomcut을 켭니다. 끄면 깔끔한 바이패스입니다.
3. 기기에 맞는 프리셋에서 시작해 세부를 다듬습니다.

창에는 다섯 개의 탭이 있습니다:

- **Home.** Now Playing, on/off 토글, 위로 끌어 올리는 사운드 컨트롤 시트: Bass / Warmth / Vocal / Clarity / Air 매크로, 볼륨, 프리셋 선택기. 더 끌면 10밴드·파라메트릭 EQ 전체가 나옵니다. 예쁩니다.
- **Space.** 스테레오 이미지를 좁히거나 넓히고, 센터 포커스·룸 댐핑·크로스피드를 설정합니다. 스피커·헤드폰용 Focus / Widen 프리셋 포함.
- **Tune.** iPhone(Continuity 마이크)으로 방을 측정하고 보수적인 EQ 보정을 적용합니다.
- **Inspect.** 읽기 전용 미터: 피크, 리미터, 드롭아웃, 상관도, 폭, 샘플레이트, 지연, 엔진 상태.
- **Settings.** 출력 장치와 포맷, 볼륨, 로그인 시 실행, 외관(테마·레이아웃·언어), 가사 캐시.

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_space.png" alt="Space" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_start.png" alt="Tune" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_measuring.png" alt="Tune 측정 중" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_result.png" alt="Tune 결과" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_inspect.png" alt="Inspect" width="150">
</div>

창은 모든 가장자리·모서리에서 크기를 조절할 수 있고, Home에서는 ↑ / ↓ 키로 사운드 컨트롤 시트를 열고 닫습니다.

상단의 얇은 핸들 바로 창을 옮깁니다. 한 번 클릭하면 전체 창이 축소된 Now Playing 카드로 접히고, 축소 모드에서 핸들을 한 번 더 클릭하면 창을 치웁니다(Roomcut은 메뉴바에 계속 떠 있으며 종료되지 않습니다). 카드를 탭하면 다시 펼쳐집니다. 그 바에 있는 토글을 켜면 창이 다른 모든 앱 위에 고정되어 가려지지 않습니다. Roomcut은 메뉴바에도 재생 컨트롤이 있는 작은 Now Playing 팝오버로 존재합니다.

### Basic과 Advanced

Home의 사운드 컨트롤 시트에는 두 가지 모드가 있습니다:

- **Basic** 은 가볍게: 다섯 개의 매크로 노브(Bass·Warmth·Vocal·Clarity·Air), 100%를 넘어 200%까지 올릴 수 있는 볼륨 슬라이더, EQ 요약 곡선, 프리셋 선택기.
- **Advanced** 는 다섯 개의 하위 탭으로 전체 기능을 엽니다:
  - **graph.** 합쳐진 EQ 응답을 읽기 전용 곡선 하나로 표시.
  - **10-Band.** 클래식 그래픽 EQ. 각 밴드를 직접 끕니다.
  - **Parametric.** 6개 바이쿼드 밴드(벨·셸프·하이/로우 패스·노치)를 주파수·게인·Q로 조정.
  - **Limiter.** 피크 리미터와 프리앰프·출력 게인.
  - **Analyzer.** 실시간 스펙트럼과 피크·RMS·스테레오 폭·스펙트럴 센트로이드.

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home_basic.png" alt="Basic 컨트롤" width="240"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home_advanced.png" alt="Advanced 컨트롤" width="240">
</div>

### 외관

Settings ▸ Appearance에는 세 가지 빠른 선택이 있습니다:

- **Auto / Light / Dark.** 시스템을 따르거나, 라이트/다크 테마를 강제합니다.
- **Halo / Cover / Mesh Gradient.** Now Playing 배경: 카드 주위의 잔잔한 글로우 링(Halo), 창 전체를 채우는 앨범 아트(Cover), 아트워크 색에서 뽑은 애니메이션 메시 그라데이션(Mesh Gradient).
- **Card / Poster.** Now Playing 레이아웃: 가운데 단일 카드(Card) 또는 가장자리까지 채우는 커버(Poster).

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_settings.png" alt="Settings · 외관" width="240">
</div>

## 제거

```sh
sudo bash scripts/uninstall-driver.sh
```

드라이버와 엔진을 제거하고 이전 출력 장치를 복구한 뒤 `coreaudiod`를 재시작합니다. 그 후에도 소리가 안 나면 System Settings ▸ Sound에서 직접 장치를 고르세요.

## 문제 해결

**"Roomcut Output"이 Sound 설정에 안 보입니다.** 드라이버는 `coreaudiod`가 재시작될 때 로드됩니다. `sudo bash scripts/install-driver.sh`를 다시 실행하거나(재시작을 대신 해 줍니다) 재부팅하세요. 내려받은 드라이버는 격리(quarantine)되어 있으면 안 되는데, `.pkg`와 `install.sh`가 이를 처리합니다.

**설치 후 소리가 안 납니다.** Roomcut이 출력할 실제 장치가 설정됐는지, Roomcut이 켜져 있는지 확인하세요. 그래도 무음이면 `sudo bash scripts/reset-audio-output.sh`를 실행하거나 System Settings ▸ Sound에서 직접 장치를 고르세요.

**앱이 안 열립니다("확인되지 않은 개발자").** 사전 빌드는 아직 애플 개발자 서명이 있지 않습니다. 앱을 우클릭해 열기를 선택하거나 System Settings ▸ Privacy & Security에서 허용하세요. 소스에서 빌드하면 이 문제가 없습니다.

**일부 곡의 가사가 안 보입니다.** 가사는 제목·아티스트·길이로 매칭해 LRCLIB에서 가져옵니다. macOS는 스트리밍 앱의 자체 가사를 외부에 제공하지 않으므로, Apple Music에는 가사가 있어도 LRCLIB에 없으면 여기서는 안 보일 수 있습니다.

**공간 컨트롤이 비활성화돼 있습니다.** 해당 기능을 지원하는 엔진 빌드가 필요합니다. 소스에서 최신 엔진을 다시 설치하세요.

## 개인정보

Roomcut은 사용자가 듣는 모든 소리를 처리하므로 경계가 중요합니다. 원본 오디오는 기기를 떠나지 않고, DSP와 분석은 로컬에서 실행되며, 로그에는 샘플이 아니라 카운터와 장치 이름만 남습니다. Room Tune은 측정 중에만 iPhone 마이크를 사용합니다.

## 빌드와 테스트

```sh
swift test                          # 앱 / Swift 단위 테스트
ctest --test-dir build --output-on-failure   # 네이티브(DSP / 엔진) 테스트
```

두 테스트 모음은 푸시와 풀 리퀘스트마다 macOS 26 runner에서 CI로 실행됩니다:

[![CI](https://github.com/habinsong/roomcut/actions/workflows/ci.yml/badge.svg)](https://github.com/habinsong/roomcut/actions/workflows/ci.yml)

## 기여

이슈와 풀 리퀘스트를 환영합니다. 위 단계로 작업 트리를 만들 수 있고, 무언가를 맡을 생각이면 먼저 이슈를 여는 편이 보통 모두의 시간을 아낍니다.

## 가사 및 크레딧

트랙 정보(제목·아티스트·아트워크)는 시스템의 Now Playing 메타데이터에서 옵니다. 싱크 가사는 제목·아티스트·길이로 [LRCLIB](https://lrclib.net)에서 매칭해 필요할 때 가져오고 로컬에 캐시합니다. Roomcut은 `User-Agent`로 스스로를 밝히며, 이 저장소에 가사를 포함하거나 재배포하지 않습니다. 가사는 각 권리자의 소유입니다.

macOS는 스트리밍 앱의 인앱 가사를 다른 앱에 넘기지 않으므로, Roomcut은 LRCLIB에서 자체적으로 가져옵니다. 어떤 곡은 Apple Music에는 가사가 있어도 LRCLIB에 아직 없으면 여기서는 안 보일 수 있습니다.

LRCLIB의 서버와 클라이언트는 MIT 라이선스이며, Roomcut은 공개 HTTP API로만 통신하므로 그 코드는 여기 포함되지 않습니다.

## 라이선스

Roomcut은 Apache License 2.0으로 배포됩니다. 전문은 [LICENSE](../LICENSE)를 참고하세요. 이 라이선스는 소스 코드에 적용되며, Roomcut 이름과 브랜딩은 적용 대상이 아닙니다(Apache 2.0은 상표권을 부여하지 않습니다).
