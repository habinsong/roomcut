# 2026-09-08 출력 복구·재시도 정책

이번 변경은 기존 UI와 음색 처리 설정을 유지하면서 엔진 제어 경계를 분리합니다.
실제 출력 장치의 볼륨·기본 출력·설치를 변경하지 않았습니다.

## 재현한 결함

기존 `main.cpp`의 `recoverOutput` 람다를 추출해 실제 `OutputDevice.cpp`와 기존 모의 HAL에 연결했습니다.
시간을 주입해 세 가지 실패 경로를 실행했습니다.

1. HAL 시작이 실패해도 유닛의 설정·하드웨어 샘플레이트는 같을 수 있습니다.
   기존 복구는 이 두 값만 비교해 정상으로 판정하고, 정지한 출력을 다시 시작하지 않았습니다.
2. 초기화 실패 시 재개방 횟수·시간 제한이 갱신되지 않았습니다.
   50ms 간격으로 들어온 제어 요청 10개가 주입 시간 500ms 안에서 실패한 열기를 10회 반복했습니다.
3. 장치를 바꿔도 이전 장치의 실패/변동 상태가 남았습니다.
   새로운 출력도 처음부터 링의 요청 샘플레이트를 포기했습니다.

같은 검사에서 수정 후 세 실패 모두 해결됐고, 동일 500ms 구간의 실패한 열기는 1회였습니다.
이 시간은 결정적으로 제어한 테스트 시간이며 실제 하드웨어 벤치마크 시간이 아닙니다.

## 변경 내용과 책임

- `engine/include/OutputRecovery.hpp`: 실제 출력 상태와 monotonic 시각을 입력으로 받아 재개방 여부를 결정합니다.
  실패한 시도도 누적하고, 시도 완료 시점부터 최소 500ms를 기다립니다. 세 번째 시도부터 2초,
  다섯 번째부터 5초로 제한합니다. 장치 또는 입력 형식이 바뀌면 이전 제한을 초기화합니다.
- 안정 판정에는 실제 `running()`이 필요합니다. 두 번의 링 샘플레이트 시도 뒤에는 장치의 현재 레이트를 사용합니다.
  안정된 관측이 2초 이어지면 누적 상태와 예약된 재시도를 해제합니다.
- `engine/include/RenderProgressWatchdog.hpp`: 시작/재개방 유예, 관측 구간, 연속 초과 횟수와 복구 횟수를 소유합니다.
  기존 1초 구간·1.5배 초과 2회·최대 3회 복구 기준을 유지합니다. 다른 장치는 별도 복구 기회를 받습니다.
  무효 샘플레이트와 프레임 카운터 재시작은 연속 측정 구간을 끊습니다.
- `engine/src/main.cpp`: 정책 결과에 따라 기존 수명주기·HAL·렌더 준비·장치 기록을 조율합니다.
  실제 장치 변경과 예약된 재시도를 분리하고, 실패한 출력 인스턴스는 닫습니다. 진입부는 1331→1253행입니다.
- `engine/tests/test_output_recovery.cpp`: 시간 경계·재시도·형식 변경·안정 판정·속도 감시를 검사합니다.
- `engine/tests/test_output_device.cpp`: 실제 구성된 유닛의 시작 실패 → 재시작 → 샘플 출력 회귀를 추가했습니다.
- `core/tests/CMakeLists.txt`: 새 검사를 네이티브 전체 검사에 연결합니다.

정책 객체는 하드웨어·파일·DSP 상태에 접근하지 않습니다. IO와 현재 장치 선택 정책은 기존 경로에 남겨
기존 호출 순서를 유지합니다. 전체 God object 제거가 완료됐다는 뜻은 아닙니다.

## 검증

| 명령·검사 | 결과 | 자료 |
|---|---|---|
| 기존 복구 람다 + 실제 OutputDevice + 모의 HAL | 3개 실패 재현, 500ms 구간 열기 10회 | [수정 전](raw/2026-09-08-output-recovery/before.txt) |
| 현재 복구 람다 + 같은 경계 | 통과, 동일 구간 열기 1회 | [수정 후](raw/2026-09-08-output-recovery/after.txt) |
| 대상 출력·복구 검사 | 2/2 통과 | [대상 검사](raw/2026-09-08-output-recovery/targeted-tests.txt) |
| `cmake --build build -j4` | 성공, 경고 없음 | [빌드](raw/2026-09-08-output-recovery/native-build.txt) |
| `ctest --test-dir build --output-on-failure` | 34/34 통과 | [전체 네이티브](raw/2026-09-08-output-recovery/native-tests.txt) |
| 정책/속도 감시 `-fsanitize=address,undefined` | 통과 | [정책 계측](raw/2026-09-08-output-recovery/policy-sanitized.txt) |
| 현재 복구 람다·OutputDevice `-fsanitize=address,undefined` | 통과 | [복구 계측](raw/2026-09-08-output-recovery/recovery-sanitized.txt) |

대상 검사 명령:

```bash
ctest --test-dir build -R '^(output_recovery|output_device)$' --output-on-failure
```

기존/현재 람다 검사에서는 원래 시간 읽기를 주입 시각으로 바꾸고, 장치 선택·상태 저장·감시 연결을 테스트 입력으로 대체했습니다.
HAL API는 기존 출력 테스트의 모의 구현이며 `OutputDevice`의 실제 open/start/close 코드는 그대로 실행했습니다.
렌더·분석 준비를 포함한 전체 엔진 실행 검사는 아닙니다. 재현 소스는 `build/recovery-audit/legacy-recovery.cpp`,
`current-recovery.cpp`에 보존했습니다.

추가 회귀 검사는 다음을 확인했습니다.

- 느린 열기 완료 시점부터의 지연, deadline 직전/정각, 빈번한 메시지에 의한 재시도 가속 방지.
- 세 번째 시도의 네이티브 레이트 사용, 2초·5초 간격, 새 장치·새 입력 형식의 즉시 초기 시도.
- 정지한 유닛의 재시도, 실제 시작 성공 후 production 콜백에서 샘플 출력.
- 정상 출력 유지, 안정 확인 뒤 재시도 취소, 장치 없음/입력 없음의 처리.
- 시작 5초·재개방 3초 유예, 두 번 연속 초과, 복구 상한, 새 장치의 별도 상한,
  정확히 1.5배인 경계, 카운터 재시작·무효 레이트 사이의 잘못된 연속 판단 방지.

## 근거와 한계

[Apple의 시작 API](https://developer.apple.com/documentation/audiotoolbox/audiooutputunitstart(_:))와
[AUHAL 형식 설명](https://developer.apple.com/library/archive/technotes/tn2091/_index.html)을 확인했습니다.
장치 형식 비교와 실제 시작 성공을 구분하며, 500ms/2초/5초와 안정 관측 기간은 Roomcut의 구현 정책입니다.

실기기 재개방 시간·장시간 재생·장치 연결/절전·청취는 미검증입니다. 실제 제어 루프는 스케줄링과 HAL 호출 시간의
영향을 받으므로 테스트 deadline에 정확히 실행된다고 보장하지 않습니다. 속도 감시는 과도하게 빠른 콜백을 대상으로 하며
모든 종류의 무음·렌더 정지를 감지하는 도구라고 주장하지 않습니다.

이번 변경은 Swift 앱과 공유 IPC를 수정하지 않아 Swift 검사·앱 번들 빌드는 반복하지 않았습니다.
새 동시성 경로도 추가하지 않아 ThreadSanitizer 대신 실제 출력 경로의 ASan/UBSan과 시간 정책 검사를 거쳤습니다.
다음은 명령 분기·응답 조립·진단 책임입니다. 전체 개발 목표는 계속 활성 상태입니다.
