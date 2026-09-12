# 2026-09-08 장치 쓰기·순서·피드백

기존 화면 구성과 UI 토큰을 유지했습니다. 볼륨·밸런스·장치·포맷·출력 전환의 실제 쓰기를
UI 즉시 반영과 분리하고, 쓰기 순서를 명시적으로 관리합니다.

## 재현한 문제

- ON의 응답을 지연시킨 뒤 OFF를 요청하면 늦은 ON이 마지막에 적용됐습니다.
- 이전 장치 선택을 실패시킨 뒤 새 장치 선택을 성공시켜도 이전 오류가 나중에 표시됐습니다.
- 실제 볼륨이 180%에서 100%로 바뀌었는데 예전 보정 코드가 그 읽기를 무시했습니다.
  최초 세 테스트에서 5개 실패를 재현했습니다.
- 자동 초기 출력 설정이 사용자가 직접 한 볼륨 변경의 실패를 지우는 경우도 별도 검사에서 재현했습니다.
- 느린 하드웨어 쓰기가 상태 RPC와 같은 큐를 점유해 상태 요청 진행이 멈추는 검사도 실패했습니다.

## 구현 경계

| 파일 | 변경 |
|---|---|
| `DeviceCommandWriter.swift` | 진행 중인 명령은 한 개, 대기 명령은 종류별 최신 한 개만 보관. 완료·오류 리비전과 취소 세대 소유 |
| `EngineClient.swift` | 비동기 볼륨·밸런스·기본 출력 API, Live 하드웨어 쓰기 전용 큐, 기존 RPC/읽기 큐와 분리 |
| `RoomcutViewModel.swift` | UI 값은 즉시 반영하고 실제 완료 전 읽기 덮어쓰기 방지. 최근 사용자 의도와 오류만 표시 |
| `DeviceCommandTests.swift` | 지연된 ACK·오류·연속 조작·취소·큐 정체 회귀 |
| `RoomcutViewModelTests.swift` | 기존 동기 완료 가정을 UI 즉시 반영과 비동기 쓰기 완료 확인으로 나눔 |
| `Package.swift` | 새 쓰기 담당을 RoomcutCore에 연결 |

명령 종류별 대기값은 교체하되 다른 종류의 명령은 순서를 유지합니다. 현재 종류 수에 따라 최대 6개가 대기합니다.
현재 시도는 중간에 다른 요청이 들어와도 진행 상태를 유지하므로 `await` 사이에 두 복합 명령이 뒤섞이지 않습니다.
취소는 아직 보내지 않은 작업과 후속 단계를 무효화합니다. 이미 ACK된 네이티브 변경이 되돌아갔다고 가정하지 않습니다.

자동 초기 설정은 사용자 조작의 오류를 지우지 못하게 피드백 소유권을 구분합니다.
대기열 자체는 ObservableObject가 아니며, 화면에 필요한 값/오류만 모델이 게시합니다.
이 변경으로 초기 쓰기 완료가 모델 전체를 불필요하게 다시 게시하는 회귀도 해결했습니다.
기존 미터 전용 갱신 테스트의 기준은 완화하지 않았습니다.

## 검증 결과

| 검사·명령 | 결과 | 자료 |
|---|---|---|
| 기존 순서·오류·증폭 읽기 | 3개 테스트, 5개 실패 재현 | [수정 전](raw/2026-09-08-device-writes/before.txt) |
| 자동 초기 설정과 사용자 오류 | 수정 전 실패 재현 | [피드백 실패](raw/2026-09-08-device-writes/feedback-before.txt) |
| 쓰기 중 상태 요청 진행 | 수정 전 0.5초 대기 실패 | [큐 정체](raw/2026-09-08-device-writes/queue-before.txt) |
| 초기 전체 검사 | 불필요한 편집 모델 게시 1개 실패 | [초기 전체 결과](raw/2026-09-08-device-writes/swift-tests-initial.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release --jobs 4` | 165/165 통과 | [최종 전체 검사](raw/2026-09-08-device-writes/swift-tests.txt) |
| 별도 scratch 경로 ThreadSanitizer | 쓰기·폴링·미터 관련 23/23 통과 | [동시성 검사](raw/2026-09-08-device-writes/tsan.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer bash scripts/build-app.sh release` | 최신 앱 번들 조립 성공 | [앱 빌드](raw/2026-09-08-device-writes/app-build.txt) |
| `codesign --verify --deep --strict build/Roomcut.app` | 로컬 서명 검증 통과 | 공증·배포와 구분 |
| 평가용 UI 조작 | 볼륨 60→140%, 밸런스 C→R20, 복원 확인 | [UI 관찰](raw/2026-09-08-device-writes/ui-smoke.txt) |

검사한 세부 동작:

- 지연된 ON 뒤의 OFF가 최종 상태가 되며, 동시에 진행 중인 keep-default 쓰기는 최대 1개입니다.
- 진행 중 볼륨 쓰기를 막고 8개의 후속 값을 입력해도 실제 값 전달은 첫 값·최신 값 두 개이며, 사이의 밸런스 요청은 보존합니다.
- 쓰기 완료 전 폴링이 UI의 새 값을 되돌리지 않습니다. 현재 오류는 정상 폴링만으로 사라지지 않고 새 성공으로 해제됩니다.
- 명시적 OFF가 초기 자동 ON보다 우선하며, 자동 설정은 사용자 오류를 숨기지 않습니다.
- 취소 후 아직 보내지 않은 볼륨/복합 명령 후속 단계는 실행하지 않습니다. 이미 완료된 첫 단계는 그대로임을 확인합니다.
- 포맷 대기값은 최신 것으로 합쳐지고, 장치 변경과의 순서는 유지합니다.
- Live 쓰기 큐를 막아 둔 동안 메인 액터와 상태 RPC 큐가 각각 진행합니다. 실제 하드웨어 쓰기를 실행한 검사는 아닙니다.

## UI 확인

별도 식별자 `com.roomcut.device-writes-qa`와 메모리 fixture를 사용했습니다.
Settings에서 볼륨을 60→80→140%로 올리고 다른 탭을 왕복한 뒤 140%가 유지됨을 확인했습니다.
Space에서 밸런스 C→R20을 확인한 뒤 C로 복원했고, 볼륨도 60%로 복원했습니다. 평가용 앱 종료를 확인했습니다.
슬라이더의 직접 AX 값 설정은 지원되지 않아 노출된 Increment/Decrement 접근성 동작을 사용했습니다.
실제 시스템 볼륨·기본 출력·청취 상태를 변경한 확인이 아닙니다.

## 참고와 남은 범위

[Swift actor 제안서](https://raw.githubusercontent.com/swiftlang/swift-evolution/main/proposals/0306-actors.md)의
재진입 설명과 [Apple 응답성 지침](https://developer.apple.com/documentation/xcode/improving-app-responsiveness)을 확인했습니다.
MainActor는 데이터 접근을 직렬화하지만 여러 `await`로 나뉜 명령 전체의 순서까지 대신 보장하지 않습니다.

기존 동기 클라이언트 API는 호환을 위해 남아 있습니다. 앱은 새 비동기 경로를 사용하며,
legacy/fixture의 기본 구현은 기존 actor 격리를 유지합니다. 실제 네이티브 반환 오류를 상위에 전달하지만
C shim 내부 복합 작업의 모든 부분 실패를 원자적으로 되돌리는 구현은 아닙니다(IO-01).

느린 장치 읽기 자체를 상태/미터 주기와 독립적으로 완료하는 작업이 남아 있습니다.
이번에 보장한 것은 앱 내부 큐 분리와 명령 순서이며, 실제 엔진·HAL까지 포함한 응답 시간 보장이 아닙니다.
네이티브 C/C++ 코드는 수정하지 않아 ctest를 반복하지 않았습니다. 다음 작업과 전체 목표는 계속 활성 상태입니다.
