# GPU 공통 모듈과 설치 순서

iiGPU 0.1은 SDK 공통의 네이티브 GPU 명령·컴퓨트·동기화 모듈이다.
현재 Metal·Vulkan 백엔드를 제공하며 상위 모듈의 도메인 계산과 렌더러를
내부로 역수입하지 않는다. iiSDKCore는 iiGPU를 참조하지 않는다.

기본 타깃 iiGPU::iiGPU와 선택적인 iiGPU::Runtime은 다른 SDK 없이 사용할 수 있다.
애플리케이션이 Core EventLoop 등 스케줄러를 공급해 GPU 완료를 비차단 관측한다.
앱은 iiDecision의 증거 백엔드 인터페이스와 iiGPU를 조합해 Metal·Vulkan을 사용한다.
어댑터는 정확한 uint32 증거 커널·마스크를, iiDecision은 long double 확률 계산을 소유한다.
iiGUI의 기존 렌더러 전체를 이전한 상태는 아니다.

iiGPU와 iiDecision은 독립적으로 빌드된다. Windows에서 Metal 검사 6개와 iiDecision Metal 재사용
검사는 지원 대상 외 skip이다. Vulkan 실행 실패를 정상 skip으로 허용하지 않는다.

Windows에서는 Vulkan SDK와 드라이버를 준비하고 IIGPU_VULKAN=ON으로
빌드한다. 전체 검증 도구가 GPU 실행을 요구하는 환경에서 Vulkan 장치 부재를
소스 빌드 성공으로 대체하지 않는다. macOS의 현재 실행 증거와 다른 운영체제의
소스·교차 빌드 증거는 분리한다.

[모듈 API와 수명 계약](../iiGPU/docs/Contracts.md),
[플랫폼·재현·검증 기록](../iiGPU/docs/Verification.md)을 따른다.

목록 완전성 검사에서 기존 CMake 모듈 iiBrushTools·iiOnnxRuntime·iiVirtualizer의
Windows 목록 누락도 확인하여 실제 의존성과 함께 등록했다. 이 등록은 세
모듈의 Windows 실행 성공을 의미하지 않는다. 일괄 설치 도구 검사와 Windows
검증 정책의 호스트 회귀 검사는 통과했다.
