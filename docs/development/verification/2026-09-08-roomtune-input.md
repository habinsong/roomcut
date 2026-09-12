# 2026-09-08 Room Tune 입력 형식과 신호 행렬

기존 UI/UX와 첫 번째 마이크 채널을 기록하는 방식을 유지했습니다.
녹음 버퍼를 잘못 해석하는 문제를 실제 CMSampleBuffer로 재현했고, PCM 변환을 독립된 담당으로 옮겨 수정했습니다.

## 확인된 PCM 결함

기존 코드는 float 플래그가 있으면 Float32로, 그렇지 않으면 Int16으로 읽었습니다.
고정 크기 AudioBufferList 하나만 받아 채널별 버퍼가 분리된 스테레오도 처리하지 못했습니다.
Float32/Float64/Int16/Int32 × mono/stereo × interleaved/non-interleaved의 16조합 중
10조합에서 입력 폐기 또는 잘못된 샘플이 재현됐습니다.
Float64의 일부 패턴에서는 샘플 절대 오차가 2.5였습니다.
[수정 전 검사](raw/2026-09-08-roomtune-input/pcm-before.txt).

`RoomTunePCM`은 CoreMedia의 PCM 복사 함수를 써서 형식과 채널 배치를 보존하고,
AVAudioConverter의 채널 맵 0으로 첫 채널만 Float32로 변환합니다.
같은 형식에서는 변환기를 재사용하며 캡처 큐가 직렬로 소유합니다.
샘플레이트 변환은 이 단계에서 하지 않고 기존 `RoomTuneAudioFile`의 48kHz 변환에 남겨 두었습니다.

메타데이터의 프레임/바이트 수와 실제 데이터 길이를 확인한 뒤 버퍼를 할당합니다.
준비되지 않았거나 무효화된 버퍼, 잘린 데이터, 비정상 숫자는 거절합니다.
1.25 같은 클리핑 레벨은 보존해 뒤의 측정 검사에서 숨겨지지 않게 했습니다.
녹음기에는 프레임이 있는 버퍼의 변환 실패를 건너뛰지 않고 해당 녹음을 실패로 취급하도록 연결했습니다.
프레임이 없는 버퍼는 기존처럼 무시합니다.
실제 캡처 세션 콜백의 수명 검증은 다음 작업으로 남아 있습니다.

## 수정 파일

| 파일 | 역할 |
|---|---|
| `RoomTunePCM.swift` | 형식 변환·채널 선택·입력 검증과 변환기 수명. 41행 |
| `RoomTuneRecorder.swift` | 캡처 세션·파일 기록·피크 게시. 기존 PCM 해석을 분리하고 실패를 기록 상태에 연결. 88행 |
| `Package.swift` | 변환기를 RoomcutCore에 연결 |
| `RoomTunePCMTests.swift` | 실제 CoreMedia 버퍼·배치·packed24·잘린 입력·반복 WAV 기록 검사 |
| `RoomTuneAudioFileTests.swift` | 실제 WAV 저장/디코딩·SRC의 샘플레이트/비트 깊이 행렬 |
| `RoomTuneAnalysisTests.swift` | 알려진 클록 편차·잡음과 평탄 경로의 불필요한 보정 방지 |

## 실제 검사 범위

PCM은 기본 16조합과 packed 24-bit × mono/stereo × interleaved/non-interleaved × little/big endian의
8조합을 검사했습니다. 사용한 6샘플 패턴에서 모든 조합의 최대 오차가 0이었습니다.
일반적인 모든 수치에 무손실 변환을 주장하는 것이 아니라 해당 입력 벡터의 결과입니다.
같은 Float64 스테레오 버퍼를 32번 변환해 WAV로 기록하고 다시 읽었을 때 샘플 순서와 값이 정확히 일치했습니다.
8/16/44.1/48/96/192kHz 입력을 같은 변환기에 차례로 전달해 샘플레이트·프레임 수·첫 채널 값 보존도 확인했습니다.

WAV는 8/16/44.1/48/96/192kHz와 Int16/24/32·Float32의 24조합입니다.
진폭 0.1, 1kHz, 0.25초 입력이 모두 48kHz·12000프레임으로 읽혔습니다.
경계 구간을 뺀 RMS는 0.0706846~0.0706898이며 검사 기준 `0.1 / sqrt(2) ±0.001`을 만족했습니다.

클록 행렬은 스윕을 다른 실제 샘플 간격으로 생성하고 48kHz라고 보고하는 합성 녹음입니다.
앞 0.2초 지연과 뒤 0.8초 여유를 포함하며 전체 신호 에너지 기준 SNR의 결정적 백색 잡음을 추가했습니다.
60Hz~4kHz에서 알려진 평탄 이득과 비교했습니다.

| 클록 편차 | SNR | 최대 오차 dB | 생성된 보정 밴드 |
|---:|---:|---:|---:|
| -1000ppm | 20dB | 0.044922 | 0 |
| -1000ppm | 40dB | 0.006315 | 0 |
| 0ppm | 20dB | 0.043082 | 0 |
| 0ppm | 40dB | 0.002373 | 0 |
| +1000ppm | 20dB | 0.065671 | 0 |
| +1000ppm | 40dB | 0.006633 | 0 |

검사 허용 오차는 0.5dB이며 위 수치는 실제 관찰값입니다. 실제 마이크 클록·AGC 측정이나 클록 보정 구현이 아닙니다.
관련 없는 낮은 레벨의 백색 잡음은 테스트음 없음으로 거절됐습니다.

## 검증 명령과 결과

| 명령·검사 | 결과 | 자료 |
|---|---|---|
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release --jobs 4` | 184/184 통과 | [전체 Swift](raw/2026-09-08-roomtune-input/swift-tests.txt) |
| 별도 scratch 경로 AddressSanitizer | Room Tune 대상 16/16 통과 | [주소 오류 검사](raw/2026-09-08-roomtune-input/asan.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer bash scripts/build-app.sh release` | 최신 앱 조립 성공 | [앱 빌드](raw/2026-09-08-roomtune-input/app-build.txt) |
| `codesign --verify --deep --strict build/Roomcut.app` | 종료값 0 | 로컬 서명 무결성 검사이며 공증·배포 검증 아님 |

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer ASAN_OPTIONS=detect_leaks=0 \
  swift test --scratch-path build/roomtune-input-audit/swift-asan --sanitize=address --jobs 4 \
  --filter 'RoomTunePCMTests|RoomTuneAudioFileTests|RoomTuneAnalysisTests'
```

실패 기록도 보존했습니다:

- 첫 확장 검사에서 잘린 입력을 AudioBufferList로 붙이는 단계가 `-12731`을 반환했습니다.
  프레임 수와 실제 데이터 길이를 따로 갖는 CMBlockBuffer 연결 방식으로 시험 버퍼를 구성한 뒤
  디코더의 거부를 확인했습니다. [생성 단계 실패](raw/2026-09-08-roomtune-input/pcm-expanded.txt).
- 위 API의 Swift 인자 이름을 수정해야 했던 컴파일 오류도 남겼습니다.
  [컴파일 기록](raw/2026-09-08-roomtune-input/pcm-truncated.txt).
- 첫 잡음 입력은 최대 레벨에 가까워 테스트음 판별 전에 클리핑으로 거절됐습니다.
  테스트음 판별만 검사하도록 잡음 진폭을 0.05로 낮췄으며 제품의 클리핑 검사는 유지했습니다.
  [잡음 입력 검사](raw/2026-09-08-roomtune-input/roomtune-targeted.txt).

## 참고와 남은 검증

[Apple PCM 복사](https://developer.apple.com/documentation/coremedia/cmsamplebuffercopypcmdataintoaudiobufferlist(_:at:framecount:into:)),
[채널 맵](https://developer.apple.com/documentation/avfaudio/avaudioconverter/channelmap),
[AVAudioConverter TN3136](https://developer.apple.com/documentation/technotes/tn3136-avaudioconverter-performing-sample-rate-conversions),
[REW 측정 지침](https://www.roomeqwizard.com/betahelp/help/html/makingmeasurements.html),
[REW 클록 조정](https://www.roomeqwizard.com/betahelp/help/html/analysis.html)을 2026-09-08 확인했습니다.

실제 마이크 권한 요청·녹음·재생·청취는 하지 않았습니다.
캡처 세션의 시작/중단 지연, 권한 대기·취소·우회 복구, AGC·클록 변동·장치 소실은 남아 있습니다.
변환기와 파일 검사는 메모리/임시 WAV를 사용하는 실제 프레임워크 검사이며 실제 입력 장치 검증과 구분합니다.
이 플랫폼의 ASan은 누수 감지를 지원하지 않으므로 주소 오류 검사와 누수 검증을 구분합니다.
UI 배치와 네이티브 C/C++ 코드는 이번 작업에서 수정하지 않았고 ctest는 반복하지 않았습니다.
전체 지속 개발 목표는 활성 상태입니다.
