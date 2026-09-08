# 2026-09-08 엔진 명령·응답·저장 형식

기존 UI/UX, Mach 메시지 구조와 상태 리비전 규칙을 유지했습니다.
진입부에서 음색 명령 해석, 응답 조립, 저장용 데이터 변환을 분리했습니다.

## 변경한 책임

| 파일 | 역할 |
|---|---|
| `engine/include/SoundCommands.hpp`, `engine/src/SoundCommands.cpp` | 정규화된 프리셋·파라미터·비교 요청을 `EngineSoundState`에 적용. 알 수 없는 프리셋은 양쪽 상태 모두 보존 |
| `engine/include/EngineReplies.hpp`, `engine/src/EngineReplies.cpp` | 수집된 런타임 값과 음색 모델에서 상태·파라미터·비교·분석 응답 조립. HAL·파일·전송 접근 없음 |
| `engine/include/EngineState.hpp`, `engine/src/EngineState.cpp` | 현재 음색을 기존 저장 필드로 변환하고 부동소수점 저장 정밀도 보완 |
| `engine/src/main.cpp` | 관측값 수집·실시간 설정 게시·실제 저장·전송 순서 유지. 중복 라우팅 값은 기존 영속 상태를 참조 |
| `engine/tests/test_engine_commands.cpp` | 실제 Mach·실시간 메일박스·임시 상태 파일을 연결한 통합 검사와 응답 필드 검사 |
| `engine/tests/test_engine_state.cpp` | 정밀한 DSP 값의 저장 왕복 회귀 |
| `engine/CMakeLists.txt`, `core/tests/CMakeLists.txt` | 엔진 전용 구현과 새 검사 연결 |

진입부는 1253→1126행입니다. 새 해석기·변환기는 상태나 IO를 몰아넣은 새 객체가 아니라 좁은 함수 경계입니다.
공통 순서는 유효 요청 확인 → 음색 적용 → 설정 전체 게시 → 저장 → 기존 ACK입니다.
저장이 실패해도 현재 세션에 적용하는 기존 계약을 유지하며, 그 실패를 로그에 남깁니다.

## 저장 정밀도 문제

기존 직렬화는 `fixed`와 소수점 네 자리로 DSP 숫자를 반올림했습니다.
정밀한 프리앰프·EQ 값·필터 주파수·게인·Q를 저장 후 읽으면 원래 값과 달라지는 회귀 실패를 재현했습니다.
`double`의 `max_digits10`을 사용해 구별 가능한 값을 텍스트에 기록하도록 수정했습니다.
기존 필드 순서·키·파서는 유지하며, 종전 저장 파일도 계속 읽습니다.

새 검사는 `1e-12`와 `std::nextafter(2.5, 3.0)`을 포함해 복원된 DSP 모델의 수치가 정확히 일치하는지 확인합니다.
이는 설정값 보존 검사이며, 그 차이가 실제 청취에서 들렸다는 주장이 아닙니다.

## 실행 결과와 근거

| 검사 | 결과 | 원시 자료 |
|---|---|---|
| 기존/현재 응답 4종 × 256가지 상태 | 1024회 바이트 비교, 차이 0 | [응답 동일성](raw/2026-09-08-engine-commands/reply-equivalence.txt) |
| 실제 Mach·메일박스·상태 저장 통합 | 통과 | [통합 검사](raw/2026-09-08-engine-commands/test.txt) |
| 정밀도 회귀의 수정 전 실행 | 정확한 수치 왕복 실패 | [정밀도 수정 전](raw/2026-09-08-engine-commands/precision-before.txt) |
| 정밀도 수정 후 상태·명령 검사 | 2/2 통과 | [정밀도 수정 후](raw/2026-09-08-engine-commands/precision-after.txt) |
| `cmake --build build -j4` | 성공, 경고 없음 | [빌드](raw/2026-09-08-engine-commands/native-build.txt) |
| `ctest --test-dir build --output-on-failure` | 35/35 통과 | [전체 네이티브](raw/2026-09-08-engine-commands/native-tests.txt) |
| 통합 경로 ASan/UBSan | 통과 | [메모리 계측](raw/2026-09-08-engine-commands/commands-sanitized.txt) |
| 통합 경로 ThreadSanitizer | 통과 | [동시성 계측](raw/2026-09-08-engine-commands/commands-tsan.txt) |

응답 동일성 검사는 이전 `main.cpp`의 조립 코드를 추출해 같은 모델·미터·런타임 값을 넣었습니다.
상태 종류, 우회·출력 활성·비교 활성 조합, 긴 장치 UID, 64비트 카운터와 스펙트럼 값을 비교했습니다.
재현 소스는 `build/commands-audit/reply-equivalence.cpp`에 있습니다.

통합 검사가 실행한 경계:

- 현재 파라미터와 리비전 전체 왕복, 정확히 한 번의 리비전 증가, 완성된 실시간 설정 게시.
- 실제 임시 파일의 저장·복원, 음색 변경 시 라우팅 정보 보존, builtin 적용 시 이전 custom 행 제거.
- A/B 양쪽 설정의 원자 적용, 알 수 없는 비교 프리셋의 상태·리비전 보존.
- 구버전 짧은 파라미터 메시지, 종료 문자가 없는 프리셋 거부, 이후 정상 요청 처리.
- 비교 측정 중·맞춤·신호 없음·우회의 기존 우선순위와 분석 응답의 모든 필드·24개 빈.
- 저장 대상이 디렉터리인 실패 상황에서 이전 대상을 보존하고 현재 세션 적용은 유지하는 동작.

출력의 `state: cannot save .../not-a-file`은 의도적으로 주입한 저장 실패입니다.
초기에는 새 테스트 대상의 `core/dsp` 포함 경로와 `mkdtemp` 선언 헤더가 누락돼 빌드가 실패했습니다.
SDK의 실제 선언 위치인 `<unistd.h>`를 확인해 수정했고, 최종 검사들은 모두 실행해 통과했습니다.
[초기 포함 경로 실패](raw/2026-09-08-engine-commands/test-build-initial.txt),
[선언 헤더 실패](raw/2026-09-08-engine-commands/native-build-initial.txt)를 보존합니다.

## 참고 자료와 한계

[Apple libsyscall](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/libsyscall/mach/mach_msg.c)의
수신·분기·응답 전송 구분을 참고했으며 기존 Mach 전송 구현을 유지했습니다.
[C++ 작업 초안](https://eel.is/c++draft/numeric.limits.members)의 `max_digits10` 정의를 확인하고 실제 파서 왕복으로 검증했습니다.

설치된 엔진과 실제 오디오 출력의 검사는 아니며, 저장 정밀도가 청취 품질이나 CPU 속도를 개선했다고 주장하지 않습니다.
디스크 저장이 실패하면 재시작 후까지 상태가 보존되는 것은 아닙니다. 현재 세션 적용과 저장 성공은 별개입니다.
Swift 앱과 공유 IPC는 수정하지 않아 Swift 검사·앱 번들 빌드는 반복하지 않았습니다.

엔진의 진단·서비스 수명 경계는 ARC-01에 남겨 두고, 다음 작업은 ARC-02의 앱 폴링·장치 상태 분리입니다.
Room Tune, 선택적 다이내믹 EQ와 실기기 검증도 계속 남아 있으며 전체 목표는 활성 상태입니다.
