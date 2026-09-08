# 2026-09-08 Room Tune 취소와 정리 수명

기존 화면 구성과 측정 순서는 유지하면서 권한·우회 요청·오디오 정리의 소유권을 분리했습니다.
취소가 이미 시작한 네이티브 작업을 즉시 되돌린다고 가정하지 않고, 정리와 복구가 끝나기 전 새 측정을 시작하지 않습니다.

## 재현과 수정

기존 측정기의 준비/취소/복구 순서를 검사 가능한 코어로 옮긴 상태에서 3개 테스트의 7개 항목이 실패했습니다.
[수정 전 기록](raw/2026-09-08-roomtune-lifecycle/workflow-before.txt).
오디오 장치는 모의 구현이며 이 재현이 실제 마이크에 명령을 보냈다는 뜻은 아닙니다.

- 취소한 우회 요청의 응답이 남아 있는데 새 측정을 허용했습니다.
- 기존 복구가 끝나기 전에 완료 결과를 게시하고 새 작업을 허용했습니다.
- 우회 요청의 성공 응답이 없으면 엔진 상태가 바뀌지 않았다고 간주해 복구를 생략했습니다.

현재 워크플로는 진행 중 Task를 정리가 끝날 때까지 보유합니다. 즉시 취소는 권한 요청도 시작하지 않으며,
늦은 권한/우회/측정 결과는 다음 단계나 결과 게시로 이어지지 않습니다.
우회 요청을 시도했다면 성공 응답이 없어도 이전 상태 복구를 시도합니다.
복구는 별도 취소되지 않는 Task에서 기다리며, 실패하면 오류를 표시하고 결과를 완료로 게시하지 않습니다.

## 책임 구조

| 파일/담당 | 책임 |
|---|---|
| `RoomTuneWorkflow.swift` | 권한·우회·3회 측정·중간 대기·취소·복구와 UI 상태. 장치/파일 IO 없음 |
| `RoomTuneRound.swift` | 한 번의 녹음 준비·재생 완료·후행 캡처·정지·분석·임시 파일 정리 |
| `RoomTuneWorkQueue.swift` | 이미 제출된 동기 미디어 작업과 정리를 비 UI 직렬 큐에서 순서대로 실행 |
| `RoomTuneAudioSession.swift` | 실제 권한과 AVCapture/AVAudioEngine 어댑터. 매개한 장치 UID를 작업 큐에서 다시 확인 |
| `RoomTuneMeasurement.swift` | 앱에서 실제/평가용 오디오를 선택해 워크플로를 만드는 조립 코드 |
| `AppDelegate`→`MainWindow`→`RoomcutAppCanvas`→`RoomTuneTab` | 앱 수명의 측정 객체 하나를 전달. 탭 재생성·창 닫기·앱 종료에서 정리 소유권 유지 |

미디어 객체는 필요할 때 생성합니다. 세션 시작/중단, 재생 준비/시작/정지와 파일 IO는 직렬 큐에서 실행합니다.
재생 완료는 AsyncStream으로 기다려 취소 또는 늦은 콜백이 단일 회차의 수명을 깨지 않게 했습니다.
분석이 시작됐다면 끝날 때까지 기다린 뒤 파일을 지우며, 취소된 분석 결과는 게시하지 않습니다.
정지를 이미 수행한 뒤 분석이 실패해도 네이티브 정지를 반복하지 않습니다.

정리 중에는 기존 상태 위치에 `정리 중`을 표시하고 측정 시작을 비활성화합니다.
완료 결과는 복구 후 게시하며 같은 측정 객체가 탭 이동 중 유지됩니다.
앱 종료 요청도 진행 중 정리를 기다린 뒤 기존 엔진 종료 경로로 이어집니다.

## 실제 검사

| 명령/범위 | 결과 | 기록 |
|---|---|---|
| 초기 대상 검사 | 3개 테스트·7개 항목 실패 | [재현](raw/2026-09-08-roomtune-lifecycle/workflow-before.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release --jobs 4` | 201/201 통과 | [전체 Swift](raw/2026-09-08-roomtune-lifecycle/swift-tests.txt) |
| 별도 scratch 경로 ThreadSanitizer | 관련 17/17 통과 | [동시성 검사](raw/2026-09-08-roomtune-lifecycle/tsan.txt) |
| `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer bash scripts/build-app.sh release` | 최신 앱 조립 성공 | [앱 빌드](raw/2026-09-08-roomtune-lifecycle/app-build.txt) |
| `codesign --verify --deep --strict build/Roomcut.app` | 종료값 0 | 로컬 서명 무결성 검사이며 공증·배포 검증 아님 |
| 별도 번들 ID의 `ui-roomtune` 평가용 앱 | 탭 이동/재시작 제한·정상 완료·진행 중 종료 확인 | [UI 관찰](raw/2026-09-08-roomtune-lifecycle/ui-smoke.txt) |

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test \
  --scratch-path build/roomtune-lifecycle-audit/swift-tsan --sanitize=thread --jobs 4 \
  --filter 'RoomTuneWorkflowTests|RoomTuneRoundTests'
```

검사한 경계:

- 즉시 취소·늦은 권한 승인·권한 거부에서 불필요한 우회/오디오 호출 없음.
- 우회 요청/복구/물리 작업이 보류된 동안 다음 측정 진입 차단.
- 취소된 작업의 복구는 취소 상태를 상속하지 않고 정확히 한 번 수행.
- 정상 3회 완료와 반복 실행, 결과 게시 시점, 실패 상태, 미터 정리.
- 느린 네이티브 준비 동안 메인 액터 진행, 시작 뒤에 정리 작업이 순서대로 실행.
- 준비 중 취소 시 재생 없음, 재생 중 취소와 늦은 완료 무시, 후행 캡처 취소.
- 준비/재생/녹음/분석 실패에서 임시 파일 제거 및 중복 정지 없음.

회차 검사에서는 실제 Dispatch 큐와 임시 파일을 사용하지만 오디오 전송은 모의 구현입니다.
분석 자체의 PCM/WAV·신호 검사는 [직전 입력 검사](2026-09-08-roomtune-input.md)와 구분합니다.

평가용 UI는 `--ui-fixture ui-roomtune --ui-appearance dark`로 실행했습니다.
3초 지연된 메모리 엔진 응답과 모의 마이크를 사용하며 실제 권한/녹음/재생은 없습니다.
시작→Home→Tune에서 `정리 중`과 시작 비활성화를 확인하고 복구 후 다시 시작했습니다.
정상 3회 뒤 400Hz -5.0dB 평가 결과와 Settings의 `Roomcut 처리 켜기` 활성화를 확인했습니다.
후속 RT-04 검사에서 당시 `ui-roomtune` 상태 응답이 변경된 바이패스 값을 반영하지 않던 점을 확인했습니다.
따라서 이 Settings ON 표시는 복구 성공의 독립적인 증거로 사용할 수 없습니다. 위 워크플로/회차 검사 결과와 구분합니다.
평가용 상태를 수정한 뒤 값과 UI를 다시 확인하는 작업은 [RT-04 이어가기](../continuation.md)에서 추적합니다.
다시 Tune으로 돌아와 완료 결과가 유지되는 것도 확인했습니다. 평가 결과를 실제 방의 측정으로 취급하지 않습니다.
종료 검사는 같은 CUA 호출에서 준비 상태를 확인한 직후 종료 키를 보내고, 이후 앱 목록에서 종료를 확인했습니다.
관찰 사이에 도구 실행 간격이 있으므로 종료 소요 시간을 정밀 측정했다고 주장하지 않습니다.

UI 연결 중 마이크 식별자를 UID로 바꾸면서 요약 화면이 사용하던 장치 참조가 빠져 전체 빌드가 실패했습니다.
표시용 장치 객체는 유지하고 녹음 시작에는 UID를 전달하도록 수정했습니다.
[빌드 실패 기록](raw/2026-09-08-roomtune-lifecycle/build-before.txt)을 보존했습니다.

## 참고와 남은 범위

[Apple AVCaptureSession 시작](https://developer.apple.com/documentation/avfoundation/avcapturesession/startrunning()),
[Swift Task 취소](https://developer.apple.com/documentation/swift/task/cancel()),
[Swift 동시성 문서](https://docs.swift.org/swift-book/LanguageGuide/Concurrency.html)를 2026-09-08 확인했습니다.
동기 세션 작업과 협력적 취소를 구분하고 실제 큐/지연 검사로 적용 결과를 확인했습니다.

실제 마이크 권한 UI·AVCapture 콜백·장치 분리·절전/복귀·청취는 아직 검증하지 않았습니다.
Swift/앱 코드만 변경해 네이티브 C/C++의 ctest는 반복하지 않았습니다.
진행 중인 네이티브 함수가 반환하지 않으면 정리도 기다리게 됩니다. 강제 선점 종료를 구현했다고 주장하지 않습니다.
다음 관찰 대상은 측정 복구와 사용자의 수동 바이패스 변경 사이의 최신 의도 보존입니다.
전체 목표는 활성 상태이며 [이어가기 문서](../continuation.md)에서 현재 핸들과 다음 작업을 확인합니다.
