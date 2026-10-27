# Windows GPU 모듈 검증

iiGPU는 Windows에서 Vulkan 1.1의 네이티브 큐·버퍼·파이프라인·전송·컴퓨트·
장벽·fence를 사용한다. D3D12 전용 백엔드를 제공한다고 선언하지 않는다.
Vulkan SDK 헤더·로더와 GPU 드라이버를 준비한다.

projects.json은 iiSDKCore → iiGPU → iiDecision 순서를 가진다.
BuildProject.py는 iiGPU를 IIGPU_METAL=OFF, IIGPU_VULKAN=ON으로 구성한다.
iiGPU의 실제 설치 패키지를 사용하는 iiDecision도 동일한 Vulkan 의존성을
발견할 수 있어야 한다. SDK나 드라이버 부재를 CPU 가속 성공으로 바꾸지 않는다.

Windows에서는 Metal의 6개 iiGPU 검사와 iiDecision의 Metal 재사용 검사를
지원 대상 외 skip으로 기록한다. Vulkan 커널·버퍼 재사용 검사가 skip되면
전체 실행 검증은 성공이 아니다. 메모리·커널·동기화 오류도 실패로 유지한다.

이번 macOS 작업의 Windows 증거는 빌드 계약·의존 순서·skip 정책에 대한
소스 수준의 회귀 검사이다. Windows 실기기 실행 증거로 해석하지 않는다.
[전체 GPU 계약](../../iiGPU/docs/Contracts.md)을 참고한다.
