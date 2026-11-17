# 데이터 병렬 컴퓨팅 자원 관리자

`ii::gpu::ComputeManager`는 호출자가 선택한 하나 이상의 iiGPU `Device`를 소유하고
독립적인 배열 원소 연산을 장치별 연속 구간으로 분할한다. 각 장치는 자신의 버퍼,
파이프라인과 큐를 사용하고, 완료를 관측한 뒤 결과를 호출자의 CPU 메모리로 모은다.
기존 Metal 또는 Vulkan 백엔드를 사용하며 RTX에는 해당 드라이버와 Vulkan이 필요하다.
이 관리자는 CUDA 백엔드나 CPU 대체 실행을 추가하지 않는다.
기존 별도 `iiGPU::WebGPU` 장치는 이 관리자의 대상이 아니다. WASM에서 코드를
컴파일할 수 있다는 사실도 브라우저 GPU 실행을 뜻하지 않는다.

`discover`는 지정한 백엔드에서 물리 GPU를 찾는다. `automatic`은 빌드된 백엔드
순서에서 물리 GPU가 있는 첫 백엔드만 선택해 같은 장치를 Metal과 Vulkan으로 중복
등록하지 않는다. 선택된 장치의 생성에 실패하면 오류를 돌려준다. 이미 선택한 장치를
`from_devices`로 전달할 수도 있다. 같은 백엔드와 어댑터 인덱스의 중복은 거절한다.
다른 백엔드로 나타나는 같은 물리 장치를 함께 넘기지 않는 책임은 호출자에게 있다.
기본 장치 수 상한은 8개이고 `max_devices`로 조절한다. 직접 전달한 장치가 상한을
넘으면 거절한다. `memory_budget_per_device`는 `discover`가 새로 여는 장치에만
적용하며, 직접 전달한 장치의 기존 예산은 변경하지 않는다.

## 연산과 분할 계약

입력은 같은 바이트 크기의 원소가 연속된 배열이다. 출력도 고정 크기 원소 배열이다.
호출자는 각 백엔드의 셰이더를 `ComputeDesc`에 제공한다. 바인딩 0은 읽기 전용 입력,
바인딩 1은 쓰기 전용 출력이다. 셰이더는 **청크 안의** 스레드 인덱스로 버퍼에 접근하고
원소 수보다 큰 스레드를 무시하고 각 유효 원소의 출력 바이트를 모두 기록해야 한다.
서로 다른 원소 또는 청크 사이에 실행 순서나 동기화가 필요한 알고리즘은 대상이 아니다.

상수의 바이트 배치는 32비트 `count`, 호출자의 상수 바이트, 32비트 전체 배열 시작
인덱스의 하위 워드와 상위 워드 순서이다. 호출자 상수는 4바이트 배수여야 한다.
셰이더는 자기 상수 형식에 맞는 위치에서 시작 인덱스를 선택적으로 읽는다. `count`는
해당 청크의 유효 원소 수이며 반올림된 스레드 수가 아니다. `ComputeDesc.constants_bytes`는
12와 호출자 상수 크기의 합이어야 한다. 셰이더와 전달 바이트의 표현과 정렬은 호출자가
맞추며, 현재 범위 상수 워드는 little-endian 플랫폼을 대상으로 한다.

관리자는 장치 수만큼 연속된 구간을 균등 분배하고 각 구간을 설정된 상한, 장치 버퍼
제한, 작업 그룹 제한과 남은 iiGPU 버퍼 예산에 맞춰 나눈다. 장치마다 최대 한 작업을
진행하며 서로 다른 장치는 병렬로 실행한다. 구간의 결과는 원래 배열 순서로 합친다.
호출마다 작업이 있는 장치별로 CPU 스레드 하나를 생성하고 작업 종료 후 반환한다.
GPU 버퍼는 청크 크기로 제한되지만 CPU에는 전체 출력 크기의 저장 공간이 필요하다.
Python과 C ABI에서는 결과 전달을 위해 추가적인 호스트 메모리가 필요할 수 있다.
실행 중 실패하거나 취소되면 부분 결과를 반환하지 않는다. 이미 네이티브에 제출한 GPU
작업은 강제 중단하지 않고 완료를 기다린다. 장치의 실제 할당 정렬이나 외부 할당 때문에
계획한 청크가 예산에 맞지 않으면 더 작은 청크를 제한된 횟수로 재시도한다. 한 원소도
할당할 수 없거나 다른 장치 오류가 생기면 명시적인 오류를 반환한다.

스레드 그룹은 GPU 드라이버가 물리 실행 장치에 배정한다. 이 API는 특정 GPU 코어를
직접 선택하거나 임의의 C++·Python 함수를 셰이더로 자동 변환하지 않는다. GPU가 없는
경우와 미지원 셰이더·예산 부족은 오류이며 CPU 계산으로 자동 전환하지 않는다.
호출은 동기식이며 한 관리자의 동시 실행은 직렬화된다. GUI나 이벤트 루프를 멈추지
않으려면 앱이 자신이 소유한 작업 실행 영역에서 호출해야 한다.
호출자는 반환할 때까지 입력 바이트를 유지하고 병렬 수정하지 않는다. 다른 소유자가
장치 예산이나 버퍼를 동시에 변경하면 할당이 실패할 수 있다.

## C++와 Python

`from_devices` 또는 `discover`로 관리자를 만든 다음 `execute`에 `ComputeProgram`과
입력 바이트를 넘긴다. 성공 결과는 호스트 바이트와 장치별 원소·청크 수를 제공한다.
`example/ManagedCompute`는 기존 Metal·SPIR-V 정수 변환 셰이더를 사용한다. 예제는
결과와 장치별 분할 수를 확인하지만 빌드만으로 물리 GPU에서 실행하지 않는다.

```cpp
auto manager = ii::gpu::ComputeManager::discover(ii::gpu::Backend::automatic);
if (!manager) return std::unexpected(manager.error());
ii::gpu::ComputeProgram program;
program.kernel = application_shader; // Metal과 Vulkan에 해당하는 셰이더 및 바인딩
program.input_stride = sizeof(Input);
program.output_stride = sizeof(Output);
program.constants = application_constants; // 4바이트 단위
program.kernel.constants_bytes = 12 + static_cast<std::uint32_t>(program.constants.size());
auto result = (*manager)->execute(program, std::as_bytes(std::span(input)));
if (!result) return std::unexpected(result.error());
// result->bytes는 원래 입력 순서의 CPU 메모리이고 result->devices는 처리 내역이다.
```

데스크톱 `iiGPUResourceC`와 `iigpu_resources.py`도 동일한 관리자를 호출한다.
호출자가 셰이더 소스 또는 바이너리와 입력을 제공하면 Python `bytes`를 반환한다.

```python
with library.compute_manager(backend="automatic", max_devices=4) as manager:
    output = manager.execute(input_bytes, input_stride=4, output_stride=4,
                             metal_source=metal_shader, spirv=spirv_bytes,
                             constants=parameter_bytes, group_size=64)
```

Python은 라이브러리 경로로 `Library`를 생성하며, Metal 셰이더 문자열과 SPIR-V
바이트는 각각 해당 백엔드에 필요하다. 상수의 `count`와 전역 시작 워드는 관리자가
붙이므로 `constants`에는 호출자 데이터만 전달한다. Python 실행에는 동기 호출과
호스트 결과만 제공하며 C++의 stop token은 노출하지 않는다. C ABI 호출자는 실행 중
관리자를 파괴하지 않고 입력·출력 메모리를 유효하게 유지해야 한다. 출력은 모든
청크가 완료된 성공 시에만 기록한다.
이전 버전의 C 브리지에는 관리자 진입점이 없으므로 Python은 `unsupported`를 반환하고
기존 장치·버퍼 API는 계속 사용할 수 있다.

실제 GPU 사용은 별도의 장치·드라이버 검증이 필요하다. 자동 테스트는 가짜 네이티브
장치에서 구간 분할, 상수 ABI, 동시 실행, 제한과 오류를 확인하며 물리 GPU 실행이나
성능 측정을 대신하지 않는다.

작업 그룹의 분배와 범위 검사는 [Apple Metal 문서](https://developer.apple.com/documentation/metal/mtlcomputecommandencoder/dispatchthreadgroups%28_%3Athreadsperthreadgroup%3A%29)와
[Vulkan 컴퓨트 셰이더 문서](https://docs.vulkan.org/guide/latest/compute_shaders.html)를
참고한다. NVIDIA 장치의 드라이버 조건은 [NVIDIA Vulkan 드라이버 안내](https://developer.nvidia.com/vulkan-driver)를 따른다.

## CPU 호스트가 소유하는 태스크 수명

애플리케이션 태스크는 운영체제의 프로세스 안에서 시작하고 끝난다. 호출자가 CPU
스레드에서 입력과 실행 정책을 준비한 뒤 `execute`를 호출한다. 관리자의 호스트
스레드는 입력을 나누고 GPU 명령을 제출하며, 실제 데이터 병렬 커널만 GPU에서
실행된다. 새 OS 프로세스나 GPU 소유의 독립 태스크 수명을 만들지 않는다.

각 제출의 네이티브 완료를 관측하고 CPU 메모리로 결과를 읽은 다음 장치별 호스트
스레드를 모두 합류시킨다. 성공·오류·취소 결과는 `execute`를 호출한 CPU 스레드의
기존 호출 흐름으로 돌아간다. 그 뒤의 결과 처리와 애플리케이션 태스크 완료는
호출자가 담당한다. 이미 제출된 GPU 작업은 취소 요청 후에도 완료와 자원 회수를
기다린다. GUI 이벤트 스레드를 계속 사용해야 한다면 앱이 소유한 CPU 실행기에
태스크를 맡긴다.
앱은 iiSDKCore의 `Runtime`과 `TaskScope`를 별도로 조합해 호스트 태스크를 관리할 수
있으며, iiGPU는 iiSDKCore를 직접 찾거나 링크하지 않는다.

CPU의 작업 진행과 GPU의 하드웨어 클럭을 같은 주기로 가정하지 않는다. GPU 완료는
CPU 클럭 횟수의 추정이나 바쁜 반복이 아니라 네이티브 완료 신호를 통해 관측한다.
이 계약은 CPU 사이클 측정이나 CPU·GPU 클럭의 동기화를 제공하지 않는다.

## CPU 런타임과 장치 청크의 조합

동기식 `ComputeManager`와 별도로 애플리케이션은 iiSDKCore의 `ChunkQueue`에 CPU 콜백과
iiGPU 장치 제출 콜백을 제공할 수 있다. `example/CooperativeChunks`는 같은 배열 변환을
CPU와 iiGPU의 Metal 또는 Vulkan 셰이더로 구현한다. 장치 레인의 논리 그룹 크기와 최대
그룹 수는 `Device::limits()`와 셰이더 요구 사항을 기준으로 정한다. 큐가 전달한 범위의
입력만 업로드하고 `Submission`을 완료까지 보유한 뒤 해당 구간을 CPU 출력에 회수한다.
호출자는 전체 작업 동안 입력·출력과 캡처한 자원의 수명 및 동시 접근을 보장한다.

`ChunkTicket`은 CPU 활성 청크, 장치에 제출된 건수, 장치 완료 대기와 CPU 결과 회수를
구분해 관측한다. 장치에 이미 제출한 명령은 취소 요청 후에도 완료까지 보유한다. 논리
스레드 그룹 배치는 물리 GPU 코어 선택이 아니며 드라이버가 실제 물리 배치를 결정한다.
Python의 기존 `ComputeManager.execute`는 동기식이므로 제출 시점과 완료 시점을 따로
관찰하는 비동기 iiGPU Python API가 아니다. 자세한 큐 계약은 iiSDKCore의
`docs/chunk-queue.md`에 있다.

두 SDK를 사용하는 응용 예제와 모의 통합 테스트는 iiGPU 라이브러리에 iiSDKCore 의존성을
추가하지 않는다. 검증할 때 `IIGPU_CORE_INCLUDE_DIR`에 Core의 빌드 또는 설치 include,
`IIGPU_CORE_LIBRARY`에 같은 Core의 라이브러리를 지정한 뒤 `build/`에서
`iiGPUCooperativeChunksTests`와 `iiGPUCooperativeChunksExample`을 빌드한다.
`iiGPU.cooperative_chunks`는 가짜 GPU 제출로 CPU와 GPU 청크의 결과 및 동시 상태를
확인한다. 예제 빌드나 가짜 장치 테스트는 실제 GPU에서의 실행 검증이 아니다.
