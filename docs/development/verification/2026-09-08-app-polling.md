# 2026-09-08 앱 폴링·장치 읽기

기존 화면 구성·디자인 토큰과 편집 동작을 유지하면서 조회 작업의 수명을 분리했습니다.
검사에서는 실제 출력 설정·볼륨을 변경하지 않았습니다.

## 재현과 변경

기존 모델에서 조회를 기다리는 동안 `stopPolling`을 호출해도 늦은 결과가 상태와 오류를 게시하고,
기본 출력 변경을 요청했습니다. 숨긴 분석 화면에도 늦은 결과가 다시 들어왔습니다.
4개 테스트에서 6개 실패를 재현했습니다.

장치 조회를 비동기로 옮긴 뒤에는 느린 조회 사이에 완료된 새 EQ 편집을 이전 상태의 프리셋 이름으로
다시 표시하는 경합도 재현했습니다. 조회 시작 시점의 편집/쓰기 리비전을 확인해 값과 이름을 함께 보호합니다.
이 추가 검사는 수정 전 2개 항목이 실패했고, 최종 코드에서 통과했습니다.

| 파일 | 소유하는 책임 |
|---|---|
| `EnginePoller.swift` | 타이머·중복 조회 방지·조회 세대·읽기 주기·장치 조회 무효화 |
| `DeviceReadback.swift` | 요청한 장치 목록/컨트롤의 읽기 결과. 요청하지 않은 부분과 없는 장치/기능을 구분 |
| `EngineClient.swift` | 실제 Live 장치 읽기를 별도 큐로 실행. Mach 명령 큐와 분리 |
| `RoomcutViewModel.swift` | 최신 조회만 UI에 적용하고 편집/쓰기 리비전을 확인. 기존 공개 게시 속성 유지 |
| `PollingLifecycleTests.swift` | 중단·재시작·취소·늦은 분석/장치 결과·편집 보호·큐 분리 검사 |
| `Package.swift` | 새 구현을 RoomcutCore에 연결하고 다른 타깃에서는 제외 |

`EngineClientProtocol`에는 기본 구현이 있는 비동기 읽기 경로를 추가했습니다.
기존 actor 격리 fixture와 사용자 클라이언트는 MainActor 기본 구현을 유지하며, Live 구현은 명시적으로
전용 큐에서 동기 CoreAudio/Mach 읽기를 실행합니다. 이미 시작한 시스템 호출이 취소됐다고 가정하지 않고 결과를 무효화합니다.

## 실제 검사

| 검사·명령 | 결과 | 자료 |
|---|---|---|
| 기존 폴링 수명 회귀 | 4개 테스트에서 6개 실패 | [수정 전](raw/2026-09-08-app-polling/before.txt) |
| 느린 조회와 새 EQ 편집 | 수정 전 이름 관련 2개 실패 | [편집 경합](raw/2026-09-08-app-polling/edit-before.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release --jobs 4` | 154/154 통과 | [전체 Swift](raw/2026-09-08-app-polling/swift-tests.txt) |
| 별도 scratch 경로에서 `--sanitize=thread --filter PollingLifecycleTests` | 11/11 통과 | [ThreadSanitizer](raw/2026-09-08-app-polling/tsan.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer bash scripts/build-app.sh release` | 앱 번들 조립 성공 | [앱 빌드](raw/2026-09-08-app-polling/app-build.txt) |
| `codesign --verify --deep --strict build/Roomcut.app` | 로컬 서명 검증 통과 | 공증·배포와 구분 |
| 실제 Live 장치 읽기 | 장치 5개와 컨트롤 조회 완료, 읽는 동안 메인 액터 진행 | [읽기 전용 확인](raw/2026-09-08-app-polling/live-readback.txt) |
| 별도 평가용 앱 | Home → Inspect → Home 전환과 표시 확인, 종료 확인 | [UI 관찰](raw/2026-09-08-app-polling/ui-smoke.txt) |

추가로 확인한 동작:

- 시작 직후 중단하면 아직 실행되지 않은 첫 폴링도 폐기합니다.
- 중단 전 조회가 끝나지 않아도 새 조회를 시작할 수 있고, 이전 완료가 새 조회의 상태를 해제하지 않습니다.
- 수동 Task 취소·늦은 연결/파라미터 오류는 화면을 바꾸지 않습니다.
- 볼륨 드래그가 끝난 뒤에도 그 전에 시작한 장치 읽기가 새 값을 덮어쓰지 않습니다.
- 실제 Live 읽기 큐를 막아 둔 동안에도 메인 액터가 실행됩니다.
- 장치·컨트롤·분석·비교 조회의 주기는 완료 시점을 기준으로 계산하며, 이 주기들은 시계가 뒤로 바뀌었을 때 무기한 미뤄지지 않습니다.

실제 읽기 전용 확인은 빌드된 RoomcutCore/CRoomcutClient를 연결한 별도 실행 파일에서 확인했습니다.
최종 관찰은 약 151ms 동안 메인 액터 작업 64회였습니다. 앞선 관찰은 약 236ms/74회였으며,
환경과 초기화 비용이 달라 고정된 지연이나 개선율로 해석하지 않습니다. 디스플레이 프레임 수를 측정한 것도 아닙니다.
재현 소스는 `build/polling-audit/live-readback.swift`입니다.

평가용 앱은 `com.roomcut.polling-qa` 식별자와 메모리 fixture를 사용했습니다.
Inspect의 48kHz·32-bit·60% 등은 fixture 값이며 실제 재생·장치 상태로 주장하지 않습니다.
스크린샷과 접근성 트리를 도구에서 확인했으며 원본 스크린샷 파일은 로컬 문서에 저장하지 않았습니다.

## 참고 자료와 남은 범위

[Apple 응답성 지침](https://developer.apple.com/documentation/xcode/improving-app-responsiveness)의 공식 Markdown 본문을 확인했습니다.
단순히 MainActor에서 Task를 만드는 것과 실제 동기 작업을 다른 큐로 옮기는 것을 구분했습니다.

현재 모델의 장치 쓰기와 기본 출력 변경은 여전히 동기 경로를 포함합니다. 다음 단계에서 쓰기 순서·오류 소유권과 함께 다룹니다.
장치 읽기가 메인 스레드를 막지는 않지만, 하나의 폴링 주기가 그 읽기의 완료를 기다리므로 느린 장치에서
미터 갱신 주기까지 유지된다고 보장하지 않습니다. 읽기/미터 독립 처리도 남아 있습니다.
기존 외부 볼륨/증폭 변경 처리의 의미도 후속 검사 대상입니다.

네이티브 C/C++ 소스는 수정하지 않아 ctest를 반복하지 않았습니다. 마지막 네이티브 결과는 직전 기록의 35개 통과입니다.
이번 단계가 전체 God object 제거·실기기 청취·새 EQ 기능 구현의 완료를 뜻하지 않습니다. 전체 목표는 계속 활성 상태입니다.
