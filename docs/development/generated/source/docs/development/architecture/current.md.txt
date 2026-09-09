# 현재 책임 구조

2026-09-09 작업 트리 기준입니다. 파일별 전체 원문과 최신 행 수는 [생성 목록](../generated/README.md)을 참조합니다.
행 수는 조사 대상을 찾는 지표이며 단독으로 God object 여부를 판정하지 않습니다.

## 실행 경계

```text
SwiftUI / AppKit
  → RoomcutViewModel → SoundEditor / ParameterWriter / PresetStore / AppPreferences
  → EngineClient → engine/client C shim → EngineConnection → Mach 제어 메시지
                                      → CoreAudioDevices → HAL 장치 조회·쓰기
  → 엔진 제어 루프 → EngineSoundState → RealtimeParams → RenderPipeline

시스템 PCM → RoomcutHAL IO → 공유 링 → RenderPipeline → OutputDevice / AUHAL
                                    ├─ SincResampler
                                    ├─ GainRamp / ComparisonProcessor / DSPChain
                                    └─ AnalysisTap → AnalysisWorker → 앱 분석 표시
```

드라이버의 실시간 IO는 공유 링에 쓰며, 연결 작업은 별도 작업 스레드가 담당합니다.
앱과 엔진의 명령 경로는 기존 Mach와 C shim을 유지합니다. 별도 IPC를 추가하지 않습니다.

## 모듈별 구현과 소유 책임

| 위치·주요 타입 | 현재 책임 | 검증 경계 |
|---|---|---|
| `RoomcutApp`, `MainWindow`, `WindowChrome`, `RoomcutAppCanvas` | 메뉴바와 창, 화면 구성, 창 조작 | Swift 빌드, 표시·지역화 검사; 실제 배치는 화면 검사 |
| `RoomcutTheme`, `RoomcutGlassStyle`, `RoomcutBackgroundLayer` | 기존 색·간격·유리 효과·배경 표현 | 디자인 보존 기준; 외부 제품의 UI를 이식하지 않음 |
| `HomeTab`, `SpaceTab`, `RoomTuneTab`, `InspectTab`, `SettingsTab` | 기능별 사용자 조작과 표시 | 상태별 UI와 키보드 흐름 |
| `SoundSnapshot`, `SoundEditor`, `SoundComparison` | 편집값, A/B, Undo/Redo, 비교 표시 상태 | `SoundEditorTests`, `SoundComparisonTests`, 통합 편집 검사 |
| `ParameterWriter` | 순서 보장과 최신 대기 쓰기 병합 | `SoundEditingIntegrationTests`의 늦은 응답·재연결 |
| `BypassWriter`, `BypassOverride` | 직접 바이패스 선택과 임시 측정 복구의 순서·소유권. 한 건 진행/최신 한 건 대기 | 취소/직접 선택·대기 값·이미 전송된 복구·오류·중복 검사, 평가용 요청 추적과 실제 앱 토글 |
| `EnginePoller`, `DeviceReadback` | 타이머·조회 세대·주기와 요청한 장치 정보의 읽기 결과 | `PollingLifecycleTests`; Live 전용 장치 큐와 기존 MainActor fixture 호환 |
| `DeviceReadCoordinator` | 진행 중인 장치 읽기와 최신 대체 요청, 공유 완료 대기·취소·컨텍스트 검증 | `DeviceReadCoordinatorTests`; 상태/미터 주기는 이 읽기를 기다리지 않음 |
| `DeviceCommandWriter` | 장치 쓰기 한 건의 진행·명령별 최신 대기값·취소 세대·사용자 피드백 소유권 | `DeviceCommandTests`; Live 하드웨어 쓰기 큐와 상태 RPC 큐 분리, 최신 의도·오류·대기값 검사 |
| `PresetStore`, `SavedPreset`, `PresetLibrary`, `AppPreferences` | 프리셋 모델·직렬화·설정 저장 | `PresetStoreTests`; 기존 키·파일 형식 보존 |
| `RoomcutMeters` | 고빈도 미터 게시 | 편집 모델의 불필요한 재게시 방지 검사 |
| `RoomTuneAnalysis`, `RoomTuneResponse`, `RoomTuneSignal`, `RoomTuneAudioFile` | 신호 응답·정렬·파일 디코딩 | `RoomTuneAnalysisTests`; 합성 신호와 실제 녹음 구분 |
| `RoomTunePCM` | 캡처 큐에서 직렬 사용하는 PCM→첫 채널 Float32 변환. 입력 형식에 따른 변환기 재사용 | 실제 CMSampleBuffer의 Float32/64·Int16/32·packed24, 채널 배치/엔디언·잘린/비정상 입력·반복 WAV 기록 검사 |
| `RoomTuneWorkflow` | 권한·우회·3회 측정·취소·복구와 결과 게시. 앱이 한 인스턴스를 소유 | `RoomTuneWorkflowTests`; 지연된 우회/복구·취소·재시작·오류·완료 검사 |
| `RoomTuneRound`, `RoomTuneWorkQueue` | 한 회차의 준비/재생 완료/정지/분석/파일 정리와 동기 작업의 직렬 실행 | `RoomTuneRoundTests`; 준비/재생/후행 캡처 취소·늦은 콜백·실패/임시 파일 제거, 실제 Dispatch 큐 |
| `RoomTuneMeasurement`, `RoomTuneAudioSession`, `RoomTuneRecorder`, `RoomTuneSweep`, `RoomTuneInputScanner` | 앱 조립·권한/미디어 어댑터·PCM 파일 기록·스윕·입력 검색 | 평가용 UI의 지연/취소·정상 완료·종료 확인. 실제 권한 UI·마이크/장치 변경은 별도 |
| `NowPlayingMonitor`, `NowPlayingPayload`, `NowPlayingView`, `LRCLIBClient`, `NowPlayingHelper` | 재생 정보·가사 조회·표시·보조 프로세스 | 파싱·페이로드·LRCLIB 단위 검사; 실서비스·프로세스 복구 별도 |
| `EngineClient`, `ClientShim.cpp`, `EngineConnection` | C ABI 변환·엔진 상태/증폭 조율·Mach 연결 권한 수명 | `engine_connection`, `control`, `control_validation`, `client_volume`, `client_devices`; 공개 C 헤더/심볼과 분리 전후 호출 결과 유지 |
| `CoreAudioDevices` | 엔진과 독립적인 장치 열거·형식·볼륨·밸런스·기본 출력의 HAL 접근 | C/Mach·모의 HAL 정상/실패 검사. 객체 파일에서 Mach/클라이언트 참조가 없고 C shim의 직접 HAL 호출이 사라졌음을 확인 |
| `EngineSoundState`, `ParameterCodec`, `ControlValidation` | DSP 명령 상태·메시지 변환·검증 | 비교 원자 적용·메시지 경계 검사 |
| `SoundCommands`, `EngineReplies`, `capturePersistentSound` | 정규화된 음색 요청 해석·수집된 상태의 응답 조립·저장 데이터 변환 | `engine_commands`, 1024회 기존/현재 응답 바이트 비교, 실제 Mach·메일박스·파일 저장 |
| `EngineState`, `EngineStateStore` | 영속 형식과 파일 I/O | 잘린 입력·원자 교체·동시 읽기 검사 |
| `RenderPipeline`, `RealtimeParams`, `RealtimeMailbox`, `PublishedRing` | 렌더 처리·설정 전달·링 읽기 수명 | 실제 처리 함수 대상 신호·동시성·할당 검사 |
| `OutputDevice`, `RenderCallbackGate`, `Lifecycle`, `DeviceSelection` | AUHAL·콜백 진입·상태 전환·선택 정책 | 모의 HAL과 상태 기계 검사; 실제 장치 별도 |
| `AudioPropertyListener`, `DeviceWatcher` | 정확한 알림 등록 식별 정보와 지연 콜백 수명·변경 플래그 | `device_services`; 실제 CoreAudio 등록/해제는 읽기 전용 확인 |
| `DeviceVolume`, `VolumeController` | 장치별 하드웨어 볼륨·밸런스와 엔진 내부 쓰기 순서·렌더용 게인 | `device_volume`, `device_services`; 실제 dispatch 큐와 모의 HAL, ASan/UBSan·TSan |
| `OutputRecovery`, `RenderProgressWatchdog` | 장치·입력 형식별 재시도 시각·실패 누적과 렌더 진행률 측정·복구 횟수 | `output_recovery`, 실제 `OutputDevice` 통합 검사, 주입 시간의 기존/현재 복구 코드 비교 |
| `Handshake`, `RingRegion`, `roomcut_mach_cleanup.h` | 검증된 HELLO 협상·정확한 매핑 범위·실패한 Mach 전송 권한 정리 | `handshake_validation`, `mach_cleanup`; 기존 샘플레이트·용량 정책 보존 |
| `AnalysisTap`, `AnalysisWorker` | 완성된 분석 구간 전달·비실시간 계산 | `analysis_tap` |
| `DSPChain`, `DSPPath`, `ComparisonProcessor` | 처리 경로·파라미터 전환·A/B 신호 처리 | `dsp_chain`, `dsp_transitions`, `comparison` |
| `Biquad`, `GraphicEQ`, `ParametricEQ`, `Spatial`, `Compressor`, `Limiter` | 개별 DSP 상태와 처리 | 각 DSP 검사와 실제 렌더 통합 검사 |
| `KWeightedLevel`, `ComparisonLevelMatch` | 비교용 가중 레벨 추정·보정 | 규격 인증 미터와 구분한 신호 검사 |
| `RoomcutIO.c`, `RoomcutTransport.c`, `RoomcutProperties.c`, `RoomcutPlugIn.c` | HAL IO·Mach 연결·장치 속성·플러그인 수명 | 링·핸드셰이크·생존 확인 호스트 검사 |
| `shared/protocol`, `shared/schemas` | C 메시지·공유 메모리 계약과 JSON 스키마 | 길이·버전·범위·호환성; 스키마 존재가 기능 구현을 뜻하지 않음 |
| `CMakeLists.txt`, `Package.swift`, `.github/workflows`, `scripts` | 빌드·검사·패키징·개발 도구 | 네이티브/Swift 빌드와 각 도구 검증 |

## 책임 집중과 이동 계획

| 현재 파일 | 관찰된 책임 | 다음 분리 단위 | 동작 보존 기준 |
|---|---|---|---|
| `engine/src/main.cpp` (1014행) | 초기화·복구/라우팅 IO 조율·메시지 전달 | 라우팅 IO 조율은 남음. 진단(`EngineDiagnostics`)·서비스 수명(`ServicePort`)·드라이버 급전 감시(`DriverFeedWatchdog`)는 분리됨 | 같은 장치 선택·복구 순서·현재 IPC·출력 유지 |
| `RoomcutViewModel.swift` (971행) | 편집 연결·UI 게시·명령/조회 결과 조율. 바이패스 순서/임시 복구는 별도 담당 | 남은 상태 적용 경계와 엔진 응답 지연의 영향 | 늦은 읽기·쓰기가 새 값/이름/오류를 덮어쓰지 않음; UI 토큰·동작 유지 |
| `ClientShim.cpp` (432행), `CoreAudioDevices.cpp` (458행) | C shim은 값 변환·엔진 상태/증폭 조율, 장치 모듈은 HAL IO. 연결 캐시는 기존 `EngineConnection` 하나 | ARC-04 분리 완료. 새 모듈에 UI·저장·엔진 수명을 추가하지 않음 | C 헤더 바이트·24개 공개 심볼 유지, 18개 이동 함수/19개 C 함수/5개 정책 함수 본문과 분리 전후 실행 결과 비교 |
| `NowPlayingMonitor.swift` (943행) | 보조 프로세스·폴링·소스 결정·가사 수명 | 기존 테스트와 실제 입력을 확인한 뒤 소스 수명주기별 분리 | 표시 우선순위·취소·재연결 일치 |
| `NowPlayingView.swift` (1443행) | 재생 정보와 가사의 여러 표시 구성 | 재사용되는 표시 요소의 경계부터 검토 | 큰 뷰 파일이라는 이유만으로 무조건 분할하지 않음 |

분리는 한 번에 한 책임씩 진행합니다. 이전 경로의 특성 검사를 확보하고 호출자를 옮긴 뒤 동일 검사를 실행합니다.
단순 확장 파일로 옮겨 같은 전역 상태를 계속 공유하는 방식은 책임 분리의 완료로 세지 않습니다.
파일 수 증가와 간접 호출 비용도 검토하며, 한 번만 쓰는 범용 서비스 계층을 만들지 않습니다.
