# 2026-09-08 장치 감시·볼륨 책임 분리

기존 UI/UX·디자인 토큰과 Mach IPC를 유지했습니다. 이번 변경은 엔진 전용입니다.
장치 선택 정책과 DSP 필터는 바꾸지 않았습니다. 이전의 미커밋 변경도 보존했습니다.

## 재현한 문제

- 기존 볼륨 함수 본문을 그대로 추출해 같은 모의 HAL 검사에 연결했을 때 8개 항목이 실패했습니다.
  비정상 숫자·짧은 읽기·읽기 실패, 채널 밸런스 초기화, 음소거 후 밸런스 상실,
  부분 쓰기의 성공 오판과 변경 채널 복원 누락, 쓰기 불가능한 채널의 사전 확인 부족입니다.
- 잘못된 채널 목록을 전달하면 `outputChannelCount`에서 ASan이 힙 범위 초과 읽기를 보고했습니다.
  최종 구현은 반환 크기와 `mNumberBuffers`를 검사한 뒤 각 `AudioBuffer`를 읽습니다.
- 추가 검사에서 입력 장치 소실 시 하드웨어 볼륨을 1로 바꾸는 경로를 확인했습니다.
  마지막 유효 값을 유지하도록 수정했습니다. 음소거는 하드웨어 외에도 렌더 게인을 0으로 유지합니다.

기존 함수의 추출본은 새 클래스의 테스트 경계에 연결하는 한 줄 외에는 함수 본문을 바꾸지 않았습니다.
원본 진입부와 추출본은 `build/device-audit/main-before.cpp`, `DeviceVolume-before.cpp`에 있습니다.

## 책임과 소유권

| 파일 | 소유하는 책임 |
|---|---|
| `engine/include/AudioPropertyListener.hpp`, `engine/src/AudioPropertyListener.cpp` | 알림의 object/address/queue/block 식별 정보, 등록 성공 여부, 비활성화 토큰, 해제 재시도 |
| `engine/include/DeviceWatcher.hpp`, `engine/src/DeviceWatcher.cpp` | 기본 출력·장치 목록·현재 출력의 샘플레이트 알림과 변경 플래그 |
| `engine/include/DeviceVolume.hpp`, `engine/src/DeviceVolume.cpp` | 하드웨어 볼륨 IO·검증·현재 장치의 채널 비율 |
| `engine/include/VolumeController.hpp`, `engine/src/VolumeController.cpp` | 전용 직렬 큐, 현재 소스·하드웨어 대상·증폭·마지막 레벨, 렌더용 atomic 게인 |
| `engine/src/main.cpp` | 위 담당 연결과 기존 출력 복구·서비스 조율 |
| `engine/tests/test_device_volume.cpp`, `engine/tests/test_device_services.cpp` | 실제 생산 코드에 연결한 HAL 경계·지연 콜백·동시성 회귀 |
| `engine/CMakeLists.txt`, `core/tests/CMakeLists.txt` | 엔진 전용 소스·blocks 컴파일과 검사 연결 |

진입부는 1497→1331행입니다. 파일 길이 감소만으로 God object 제거 완료를 주장하지 않습니다.
장치 감시가 엔진 전체 컨텍스트 포인터를 보유하지 않으며, 볼륨 콜백도 독립 상태만 보유합니다.
하드웨어 쓰기는 엔진 내부의 전용 큐에서 직렬화합니다. 렌더는 게인 하나를 atomic으로 읽습니다.
등록 해제가 실패하면 비활성화한 기존 식별 정보를 유지해 재시도합니다. 해제된 소유자의 지연 콜백은 작업하지 않습니다.

기존 master 볼륨 우선 정책과 하드웨어 제어가 없는 장치의 디지털 대체를 유지합니다.
채널별 제어는 현재 채널 비율을 반영하며, 같은 장치의 음소거 중에도 기존 비율을 보관합니다.
쓰기 전 전체 채널을 확인하고, 도중에 실패하면 이미 변경한 채널을 원래 값으로 되돌리는 시도를 합니다.
장치 데이터가 비정상적으로 많은 작업을 유발하지 않도록 256채널을 넘으면 하드웨어 채널 경로를 사용하지 않습니다.

## 검증 결과

| 검사·명령 | 결과 | 원시 자료 |
|---|---|---|
| 기존 함수의 볼륨 회귀 검사 | 8개 항목 실패 재현 | [이전 볼륨](raw/2026-09-08-device-services/volume-before.txt) |
| 기존 채널 목록 ASan 검사 | `heap-buffer-overflow` 재현 | [이전 범위 초과](raw/2026-09-08-device-services/volume-before-sanitized.txt) |
| 입력 소실·음소거 추가 검사 | 수정 전 3개 항목 실패 | [추가 실패](raw/2026-09-08-device-services/disconnect-before.txt) |
| `cmake --build build -j4` | 성공, 빌드 경고 없음 | [네이티브 빌드](raw/2026-09-08-device-services/native-build.txt) |
| `ctest --test-dir build --output-on-failure` | 33/33 통과 | [전체 검사](raw/2026-09-08-device-services/native-tests.txt) |
| `test_device_volume` ASan/UBSan | 통과 | [볼륨 계측](raw/2026-09-08-device-services/volume-sanitized.txt) |
| `test_device_services` ASan/UBSan | 통과 | [서비스 계측](raw/2026-09-08-device-services/services-sanitized.txt) |
| `test_device_services` ThreadSanitizer | 통과 | [동시성 계측](raw/2026-09-08-device-services/services-tsan.txt) |
| 실제 CoreAudio 등록·해제 | 3종 알림 등록/해제 모두 `noErr` | [실제 HAL](raw/2026-09-08-device-services/real-listener.txt) |

모의 HAL 검사는 다음 동작을 실행했습니다.

- 알림으로 볼륨 변경을 처리하며 제어 루프 폴링을 기다리지 않습니다.
- 놓친 알림은 폴링으로 복구하고, 변경 없는 10회 폴링에서는 하드웨어 쓰기가 증가하지 않습니다.
- 타깃 장치와 입력 장치를 바꾼 뒤 이전 알림을 전달해도 새 상태를 덮어쓰지 않습니다.
- 소유자 파괴 뒤 보관한 콜백을 호출해도 HAL 읽기·쓰기가 발생하지 않습니다.
- 등록 실패·해제 실패·재등록과 동일 장치 중복 등록 방지를 검사했습니다.
- 느린 모의 쓰기 중 알림 100회와 증폭 변경 100회를 함께 실행해 최대 동시 하드웨어 쓰기 수 1을 확인했습니다.
  별도 렌더 스레드의 10000회 읽기에서도 유효한 게인만 관찰했습니다.

검사 출력의 `property listener registration/removal failed: 2003329396`은 의도적으로 주입한 실패입니다.
이 메시지를 숨기지 않으며, 테스트 실행의 실패와 구분합니다.

실제 HAL 확인은 현재 기본 출력 ID를 읽고 기본 출력·장치 목록·그 출력의 샘플레이트 알림을 등록했다가 해제했습니다.
`AudioObjectSetPropertyData`를 호출하거나 소리를 재생하지 않았습니다. 실제 장치 변화 알림을 강제로 발생시킨 검사는 아닙니다.

## 레퍼런스와 비용

Apple의 [등록 API](https://developer.apple.com/documentation/coreaudio/audioobjectaddpropertylistenerblock(_:_:_:_:))와
[해제 API](https://developer.apple.com/documentation/coreaudio/audioobjectremovepropertylistenerblock(_:_:_:_:))를 확인했습니다.
공식 Markdown 본문과 설치된 macOS SDK의 `AudioHardware.h`를 대조해 block/queue 보유와 IO 문맥 예외를 확인했습니다.
새 외부 의존성 없이 기존 SDK의 CoreAudio·blocks·dispatch를 사용합니다.

직렬 큐와 상태·알림 토큰이 추가되며, 하드웨어 제어 실패 시 폴링에서 재시도합니다.
현재 단계는 쓰기 순서·수명·입력 검증을 개선한 것이며 실제 HAL 처리 지연이나 CPU 개선률을 측정한 것은 아닙니다.

## 한계와 다음 단계

실제 하드웨어의 볼륨 변화·연결 해제·절전/복귀·장시간 청취는 수행하지 않았습니다.
장치가 복원 쓰기까지 거부하면 이전 하드웨어 레벨을 강제로 보장할 수 없습니다.
앱이나 다른 프로세스가 직접 수행하는 하드웨어 쓰기는 엔진 내부 직렬화와 별개입니다.
HAL이 계속 등록 해제를 거부하며 블록을 유지하는 상황에서는 그 보유 수명까지 강제로 끝내지 않습니다.

이번 변경은 Swift 앱·공유 IPC·DSP 필터를 수정하지 않았으므로 Swift 검사와 앱 번들 빌드는 반복하지 않았습니다.
이전 Swift 143개 통과는 [직전 기록](2026-09-08-hello.md)의 결과이며 이번 실행 결과로 세지 않습니다.

다음 작업은 출력 복구와 재개방 제한 상태를 진입부에서 분리하는 것입니다. 전체 목표는 미완료이며 계속 활성 상태입니다.
