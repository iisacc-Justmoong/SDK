# GPU 명령·가속 계층의 계약

## 책임과 의존성

공통 GPU API와 구현은 `src/`, 플랫폼 백엔드는 `src/Backends/`, 완료 관측은
`src/Runtime/`, WebGPU 구성요소는 `src/WebGPUComponent/`에 둔다. 헤더는 구현 옆에
유지하며 기존 `iiGPU/`와 `iiWebGPU/` 공개 include 경로는 빌드와 설치에서
동일하다.
설치 소비자에 `WITH_RUNTIME=ON` 또는 `WITH_WEBGPU_MEMORY=ON`을 지정하면
실제 GPU 제출 없이 완료 관측과 WebGPU 헤더·메모리 계약을 확인할 수 있다.

iiGPU는 장치·메모리·컴퓨트 명령·동기화의 독립 모듈이다. 공개 네임스페이스는
`ii::gpu`이며 C++23과 표준 자료형을 사용한다. iiGUI·iiDecision·Qt·윈도 시스템에
의존하지 않는다. `iiGPU::iiGPU`와 선택적 `iiGPU::Runtime` 모두 다른 SDK를
찾거나 링크하지 않는다. 애플리케이션은 스케줄러 계약을 통해 이벤트 루프와
완료 관측을 조합하고 필요한 SDK를 각각 찾고 링크한다.

iiDecision은 Metal·Vulkan의 장치·버퍼·파이프라인·제출 코드를 iiGPU로 이전한다.
도메인 셰이더와 정확한 정수 마스크 해석은 iiDecision에 남는다. 공개 backend ID,
CPU 대체 실행 정책과 `long double` 확률 계산을 유지한다. 파이프라인을 한 번
만들고 입력·출력 버퍼를 크기가 충분한 동안 재사용한다. MLX·OpenCL·ONNX의
모델 실행 계층을 이 모듈이 대신하지 않는다.

iiGUI의 기존 Metal·Vulkan 렌더러는 현재 독립 장치 소유권을 유지한다. 이번
모듈화가 모든 기존 렌더러를 동일한 장치로 이전했다는 뜻은 아니다. 텍스처,
렌더 패스, draw, swapchain, ray tracing, tensor API와 CUDA·D3D12·WebGPU
전용 백엔드는 공통 API의 현재 구현 범위에 포함되지 않는다. 플랫폼별 기능은
수명 계약을 지킨 `native_access` 어댑터로 연결할 수 있다.

## 저수준 명령의 의미

여기서 저수준 명령은 네이티브 GPU API의 버퍼 전송·채우기·파이프라인 바인딩·
dispatch·메모리 장벽·큐 제출·완료 대기이다. GPU 제조사 고유 ISA나 커널 모드
드라이버를 직접 생성하지 않는다. Metal MSL 또는 metallib, Vulkan SPIR-V를
네이티브 드라이버가 실행한다. SPIR-V·metallib를 공급하면 셰이더 소스 파싱을
앱 런타임에 요구하지 않는다. MSL 소스 공급도 선택할 수 있다.

`Device`는 장치와 큐를 공유하는 값 핸들이다. `Buffer`, `ComputePipeline`,
`Submission`도 공유 수명을 가진다. `CommandList`는 이동 전용이며 하나의
기록 실행 영역에서 사용한다. 서로 다른 명령 목록은 병렬로 기록할 수 있다.
동일 장치의 네이티브 제출과 호스트 버퍼 접근은 내부적으로 직렬화한다.
암묵적인 작업별 스레드나 전역 GPU 장치·큐는 생성하지 않는다.

`commands()` → `copy/fill/dispatch/barrier` → `finish()` → `submit()`의 순서를
따른다. 제출은 비동기이며 성공한 목록을 다시 제출하거나 수정할 수 없다.
제출 전에 검증 또는 취소가 실패하면 목록은 실행 가능한 상태로 남는다.
명령 수, 메모리, 실행 중 제출 수의 예산을 넘기면 명시적인 오류를 반환한다.

## 메모리와 동기화

`Memory::shared`는 호스트 업로드·읽기를 지원한다. `Memory::device`에는 직접
접근할 수 없으며 shared staging 버퍼와 전송 명령을 사용한다. Vulkan의
비일관 호스트 메모리에서는 flush/invalidate를 수행한다. 전송·채우기의 범위는
4바이트 정렬이고, 저장소 바인딩 오프셋은 `Limits`의 장치 정렬을 따른다.
소스와 목적 범위가 겹치는 복사는 거절한다.

각 파이프라인은 연속된 storage buffer 슬롯, read/write 접근, 상수 크기,
workgroup 크기를 선언한다. Metal은 바인딩 다음 슬롯을 상수에 사용하고,
Vulkan은 push constants를 사용한다. SPIR-V는 고정 `LocalSize`를 선언해야
하며 공개 선언과 다르면 거절한다. 완전한 셰이더 검증기나 리플렉션 엔진을
포함하지 않는다. 호출자가 셰이더의 entry point·바인딩·상수 ABI·범위 검사와
실제 GPU 접근이 선언된 Access와 일치하도록 보장해야 한다.

같은 명령 목록에서 버퍼에 대한 write 이후 read/write 또는 read 이후 write는
명시적 `barrier()`가 필요하다. 누락은 제출 전 검증에서 거절한다. 현재 장벽은
보수적인 전체 메모리 장벽이며 자원 범위별 최적 장벽을 자동 추측하지 않는다.
Metal은 tracked resource와 encoder 경계를 사용하고, Vulkan은 명령 장벽을
기록한다. 제출 경계와 호스트 읽기 가시성은 백엔드가 처리한다.

제출은 참조한 모든 버퍼와 파이프라인을 네이티브 완료까지 소유한다. 실행 중
버퍼의 upload/read는 `busy`이다. `poll`, 성공한 `wait`, 또는 `Device::collect`
호출이 완료를 관측하면 소유권과 실행 슬롯을 반환한다. GPU가 실제로 끝나도
관측 전에는 호스트 접근을 허용하지 않는다. 마지막 Submission 핸들의 파괴는
완료를 기다린다. UI 스레드에서 마지막 핸들을 버려 대기를 유발하지 않도록
앱이 제출 핸들을 유지하고 비차단 완료 관측을 사용한다.

완료 관측 시 네이티브 command/descriptor pool과 장치 내부 추적 항목도 즉시
반환한다. 앱이 완료된 Submission 핸들을 보관해도 그 자원을 계속 보유하지 않는다.
완료·실패 결과만 해당 핸들에서 반복 조회할 수 있다.

메모리 예산은 버퍼가 보고하는 네이티브 할당 크기를 집계한다. 파이프라인,
드라이버 캐시, 셰이더 컴파일, descriptor/command pool 및 외부 native_access
할당까지 포함하는 GPU 전체 사용량 계측은 아니다.

## 완료·취소·오류

`Result<T>`는 `std::expected<T,Error>`이다. 장치 부재, 미지원 기능, 잘못된
장치의 자원, 잘못된 상태, 예산 초과, 타임아웃과 네이티브 오류를 구분한다.
기본 `require_hardware=true`는 CPU·가상·알 수 없는 Vulkan 장치를 거절한다.
명시적으로 false를 선택한 경우에도 `Adapter::hardware`는 사실대로 유지한다.
CPU 알고리즘으로의 자동 대체 실행은 없다. backend 자동 선택은 컴파일된
Metal·Vulkan 중 장치를 선택하는 정책이다.

`submit`의 stop token은 네이티브 제출 이전에만 검사한다. 제출 후 GPU 작업의
강제 취소·선점을 제공하지 않는다. `wait(0ns)`는 준비 상태 검사이고, 시간
제한 초과는 작업이나 자원의 해제가 아니다. 오류가 있으면 성공한 완료로
집계하지 않는다. Stats의 completed는 관측된 제출 완료이며 FPS나 가속 배율
측정값이 아니다.

`CompletionWatch::create`는 호출자가 제공한 `CompletionScheduler`의 반복 작업으로
제출의 `poll()`을 호출한다. `observe`는 별도의 완료 검사 함수를 받는다. 반복
작업의 등록은 양의 주기와 콜백을 받고, 스레드 안전한 `pending`과 멱등적인
`cancel`을 제공하는 토큰이나 오류를 돌려준다. 호출자는 스케줄러와 실행 영역의
수명을 관리한다. `every`와 복사 가능한 토큰을 제공하는 호환 루프는 템플릿으로
직접 연결할 수 있으며 기존 iiSDKCore EventLoop도 애플리케이션에서 이 방법으로
조합할 수 있다. 등록 중 동기 실행도 허용된다. 등록 거부·잘못된 토큰과 검사
오류는 오류로 전파된다. 콜백은 호출자가 선택한 스케줄러의 실행 영역에서
한 번 전달하며 전용 워커를 만들지 않는다.
관측 취소는 GPU 작업을 취소하지 않는다. 취소와 동시에 이미 전달되기 시작한
콜백을 되돌리거나 강제로 중단하지 않는다. 장치·버퍼·GPU 작업은 창이나
이벤트 루프의 수명과 독립적이다. 관측을 취소하거나 파괴할 때도 원래 Submission
핸들을 완료까지 유지한다. 관측 객체가 마지막 제출 핸들을 소유하면 그 파괴에
일반 Submission의 완료 대기 계약이 적용된다.

## 네이티브 확장

`native_access`는 명시된 버퍼와 장치·큐의 네이티브 핸들을 동기 콜백에 빌려준다.
관측되지 않은 제출이 있으면 `busy`이다. 콜백이 돌아오기 전에 직접 제출한
모든 작업을 완료하고 호스트 가시성을 보장해야 한다. 핸들을 저장하거나
소유권을 이전·해제하지 않는다. 콜백 안에서 같은 Device의 메서드를 재호출하지
않는다. 외부 API 헤더는 확장 어댑터에서만 포함한다.

이 기능은 GPU 공급업체별 기능을 사용할 수 있는 변경 경계이다. 상위 모듈이
빌린 핸들을 독립적으로 보관하거나 iiGPU 추적을 우회해 비동기 제출하는 방식은
지원 계약을 위반한다.

## 기술 근거

- [Apple Metal 명령 구조](https://developer.apple.com/documentation/metal/setting-up-a-command-structure)
- [Apple Metal 자원 동기화](https://developer.apple.com/documentation/metal/resource-synchronization)
- [Khronos Vulkan 동기화 예제](https://docs.vulkan.org/guide/latest/synchronization_examples.html)

각 네이티브 API의 명령·수명·메모리 가시성 계약을 참고한다. 외부 렌더러나
게임 엔진의 런타임에 의존하지 않는다.
