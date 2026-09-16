<div align="center">

<img src="../icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

[English](../README.md) · **한국어** · [日本語](README.ja.md) · [Français](README.fr.md) · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge)

</div>

> **공식 저장소**
>
> Roomcut은 [habinsong](https://github.com/habinsong)이 만들고 관리합니다. 이 저장소에서 따로 밝히지 않은 복제본·미러·리브랜딩·유사 프로젝트는 Roomcut과 관계없습니다. 소스 코드는 Apache License 2.0으로 배포합니다. Roomcut 이름·브랜딩·스크린샷·문서는 © 2026 송하빈이며 이 라이선스에 포함되지 않습니다.

Roomcut은 자체 가상 오디오 드라이버를 통해 Mac에서 나오는 모든 소리를 시스템 전역에서 제어하는 네이티브 macOS 앱입니다. 별도의 서드파티 도구 없이 음악, 영상, 화상회의 등 모든 사운드를 정밀한 DSP로 보정하며, iOS 감성의 세련되고 직관적인 UI/UX를 제공합니다.

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="../icon/app/main_home.png" alt="Roomcut 홈" width="200"><br><sub>홈</sub></td>
<td align="center" valign="middle"><img src="../icon/app/menubar.png" alt="Roomcut 메뉴 막대" width="270"><br><sub>메뉴 막대</sub></td>
<td align="center" valign="middle"><img src="../icon/app/compact_mode.png" alt="Roomcut 축소 모드" width="220"><br><sub>축소 모드</sub></td>
</tr>
</table>
</div>

## 주요 기능

- **시스템 전역 EQ**: 10밴드 그래픽 EQ + 6밴드 파라메트릭 EQ(다이내믹 EQ 지원) 및 직관적인 5대 매크로 노브(Bass, Warmth, Vocal, Clarity, Air).
- **스테레오 공간감 (Stereo Space)**: 중앙 보컬을 해치지 않고 사이드 음역대만 넓히거나 모아주는 Focus/Space 조절과 헤드폰 전용 Crossfeed, 그리고 가상 공간(Studio / Living / Hall) — 헤드폰에서는 공간 전체를, 스피커에서는 뒤따르는 잔향만 더합니다.
- **음량 보정 A/B 비교**: K-가중 라우드니스 미터로 양쪽 슬롯의 음량을 자동 일치시켜 음량 차이 없는 순수 음질 비교 지원.
- **Room Tune**: iPhone(Continuity Camera) 마이크로 방 안의 과도한 공진을 측정하고 컷 EQ 프리셋 자동 제안.
- **25개 기본 프리셋**: 스피커, 헤드폰, AirPods 등 출력 장치별 맞춤 프리셋 제공 및 커스텀 JSON 프리셋 저장/공유.
- **Now Playing & Inspect**: 메뉴 막대 실시간 재생 제어, [LRCLIB](https://lrclib.net) 싱크 가사 지원, Peak/RMS/상관도/지연 시간 정밀 계측.
- **다국어 지원**: 한국어, English, 日本語, Français, Deutsch.

## 설치

[Releases](https://github.com/habinsong/roomcut/releases)에서 최신 `Roomcut-1.1.0.pkg` 또는 `Roomcut-1.1.0.dmg`를 다운로드하세요.

> **ad-hoc 서명 실행 안내**<br>
> 설치 후 첫 실행이 차단되면 **시스템 설정 → 개인정보 보호 및 보안 → 그래도 열기**를 클릭하세요.

터미널을 통한 설치:
```sh
sudo installer -pkg Roomcut-1.1.0.pkg -target /
```

### 소스에서 빌드
```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

## 시스템 요구사항 및 안내

- **요구 환경**: Apple Silicon Mac, macOS 26 (Tahoe) 이상.
- **출력 형식**: 2채널(스테레오) 전용 (멀티채널 콘텐츠는 macOS에서 자동 다운믹스).
- **시스템 전역 적용**: 모든 오디오에 일괄 적용되며, 앱별 개별 On/Off는 지원하지 않습니다.
- **백그라운드 데몬 구동**: DSP 엔진은 백그라운드 데몬(`RoomcutAudioEngine`)으로 상주하여 앱 창을 닫아도 오디오 처리가 유지됩니다.
- **개인정보 보호**: 오디오 데이터는 외부로 전송되지 않으며, 가사 검색(LRCLIB)을 제외한 모든 처리는 로컬에서 완결됩니다.

## 제거

```sh
sudo bash scripts/uninstall-driver.sh
```
기본 오디오 출력을 원래 장치로 자동 복구하고 드라이버를 제거한 뒤 CoreAudio를 재시작합니다.

## 라이선스

Apache License 2.0입니다. [LICENSE](../LICENSE) 및 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)를 참조하세요.
