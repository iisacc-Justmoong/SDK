# Windows SDK 검증

컴파일러는 개발 셸에서 확인한 `cl.exe` 절대 경로로 지정한다. 구성 후에는 실제 cache의 Release·컴파일러·설치 prefix·toolchain·include 설정을 검사한다. CMake가 컴파일러 변경 과정에서 요청 인수를 초기화했으면 같은 `build/`에서 `--fresh`로 다시 구성하며, 불일치가 남으면 검증을 중단한다.

`projects.json`의 활성 SDK를 의존성 순서로 빌드하고, 등록된 실행 검사를 모두 수행한 뒤 사용자 SDK prefix에 설치한다. deprecated와 utility는 대상에서 제외한다. 소스에 빌드 정의가 없는 디렉터리는 실행 성공으로 계산하지 않는다.

## 사용법

Workspace 루트의 PowerShell에서 실행한다. C++ 빌드 도구와 Qt/vcpkg, Python/Node 및 프로젝트별 외부 도구는 각 저장소의 Windows 지침에 따라 준비한다.

```powershell
. ./SDK/Windows/Enter-DevEnvironment.ps1
$python = Join-Path (Get-Location) 'Tools/Python314/tools/python.exe'
& $python ./SDK/Windows/Verify.py --report-dir ./build/maintenance/windows-sdk-verification
if ($LASTEXITCODE -ne 0) { throw '전체 SDK 검증 미완료' }
```

실패 원인을 고친 뒤 같은 명령에 `--resume`을 추가하면 소스·의존성·검증 도구가 동일한 성공 기록을 재사용한다. `--only <SDK 이름...>`은 부분 점검이며 전체 커밋·푸시 조건을 충족하지 않는다.

일괄 실행은 실패한 SDK의 소비자를 `dependency-failed`로 기록하고 독립 SDK를 계속
검증한다. 실패한 선행 SDK의 예전 설치본으로 소비자를 검증하지 않는다. 하나라도
실패하거나 차단되면 전체 실행은 실패이며, `--only`도 선택한 SDK가 모두 통과해야
성공 종료한다. 원인을 고친 뒤 재개하면 차단된 SDK도 실제로 다시 실행한다.

개별 SDK의 전체 단계를 실행하려면 `BuildProject.py check SDK/<이름>`을 사용한다. 모든 CMake 출력은 해당 프로젝트의 `build/` 아래에 생성한다. Release MSVC x64 구성을 사용하고 Ninja의 지역화된 include 출력 및 UTF-8 manifest를 적용한다.

## 검증 범위와 결과

- CMake SDK는 구성·빌드·전체 CTest·설치가 모두 성공해야 한다. 테스트 0개, 등록되지 않은 실행, 실패한 설치는 성공으로 처리하지 않는다.
- LVRS-Svelte는 의존성 설치·Node 검사·Svelte 검사·Vite 빌드·실제 preview HTTP 응답·로컬 패키지 설치를 확인한다.
- iiDecision의 실제 Windows CPU Laya/ONNX 검사는 실행한다. 사용 불가능한 Metal/MLX/OpenCL/ONNX 가속 장치 열거 검사는 별도 skip으로 기록하며 성공한 장치 실행으로 계산하지 않는다. 허용 목록 밖의 skip은 검증 실패이다.
- 소스나 선행 SDK가 검증 도중 변경되면 기존 실행 결과는 무효이다. `verification.json`의 `all_projects_executed_successfully`와 `commit_and_push_allowed`는 전체 범위가 유효한 경우에만 true이다.
- 이 도구는 Git commit, push, checkout, pull을 수행하지 않는다. 검증된 변경의 Git 반영은 별도 작업이다.

개인 계정·유료 서비스·물리 장치·대형 모델 가중치의 별도 실연동은 저장소의 fixture/소비자 실행 검사와 구분한다. 각 프로젝트 지침에 실행 범위를 기록한다.

## 의존성 및 Windows 조치

현재 활성 SDK는 C++ module file set을 사용하지 않는다. 공통 CMake include는 Windows MSVC에서 `CMAKE_CXX_SCAN_FOR_MODULES=OFF`로 불필요한 module dyndep 생성을 끈다. 이 머신에서 모듈 스캔 중 새 object가 생성되기 전에 link가 시작되어 LNK1104가 반복된 구성을 피하며 C++23과 `/showIncludes` header 의존성 추적은 유지한다. 실제 module file set은 CMake의 필수 스캔 규칙을 따른다. [CMake의 스캔 규칙](https://cmake.org/cmake/help/latest/manual/cmake-cxxmodules.7.html)을 참고할 수 있다.

`Enter-DevEnvironment.ps1`은 Workspace의 `Tools`와 사용자 `~/.local/SDK`를 사용한다. 일반 셸에서 `/usr/bin` 전체를 추가하면 Git의 동명 도구와 MSVC linker가 충돌할 수 있으므로 추가하지 않는다. Python 3.14는 빌드 재현용이며 시스템 기본 Python과 분리한다.

`vcpkg-overlays/mdnsresponder`는 MFC를 사용하지 않는 리소스 header를 Windows SDK의 header로 치환한다. 네트워크 서비스 설치나 물리 장치 사용 가능 여부는 개별 프로젝트의 실행 범위에 기록한다.

## 이번 머신의 완료 기록

2026-10-24 동기화 소스의 활성 빌드 목록은 37개이다. `LVUI`와 `iiImageCodec`을
검증 목록에 포함하며, 네이티브 UI는 iiSDKCore → LVUI → iiGUI → LVRS 순서로
구성한다. iiSharedCanvas의 네이티브 코덱 선행 조건도 목록에 기록한다.
현재 실행의 판정은 새 `verification.json`과 로그를 기준으로 하며 이전 날짜의
완료 수치를 현재 소스의 성공 증거로 재사용하지 않는다.
저장소 경로가 변경된 경우(`iiServerHost` → `iiWebServer` 등) 기존 CMake cache의
소스 경로를 확인하여 동일한 `build/`를 `--fresh`로 다시 구성한다. 기존 출력과
사용자 파일을 삭제하여 경로 불일치를 숨기지 않는다.
빌드 하위 프로세스에는 검색 순서를 유지하면서 중복을 제거한 PATH를 전달한다.
개발 셸과 SDK prefix의 반복 경로에 npm의 중첩 `.bin` 경로가 더해져
`node` 검색이 실패했던 Windows 재현을 수정한다. 시스템 PATH는 변경하지 않는다.
관련 플랫폼 제약은 [Microsoft의 cmd 환경 변수 길이 설명](https://learn.microsoft.com/en-us/troubleshoot/windows-client/shell-experience/command-line-string-limitation)을 따른다.

`check`는 실제 소비자가 사용하는 추가 구성도 연속 검증한다. iiFileProvider의
Compatibility 패키지는 기본 설치 prefix에 함께 설치한다. LVRS의 기본 네이티브
구성과 기존 Qt 호환 구성은 각각 독립 빌드·전체 테스트·설치를 수행하며, Qt 구성은
`~/.local/SDK/LVRS/qt`에 설치한다. 기존 Qt 소비자에는 이 prefix와 LVRS_DIR을
명시하여 네이티브 구성과 혼합하지 않는다. Qt 상태 캡처 검사도 활성화하여 실행한다.
iiSharedCanvas의 Qt 호환 구성과 Qt 없는 네이티브 구성은 각각 검증하며 네이티브
설치는 `~/.local/SDK/iiSharedCanvas/native`에 둔다. 별도 설치 소비자가 이 패키지를
찾아 실제 파일 왕복을 수행하고 Qt target이 유입되지 않음을 확인한다.
검증 판정은 모든 구성의 테스트 기록을 검사하며 첫 구성의 성공만으로 통과하지 않는다.
실행 중 이미 검증한 선행 SDK가 변경되면 그 기록을 무효화하고 소비자를 차단한다.
완료 시에도 실패·무효 상태를 전이 소비자까지 전파하여, 새 소스 지문만 기록하고
이전 설치본을 사용한 실행이 재개 시 성공 기록으로 재사용되지 않도록 한다.
런타임 SDK의 bin도 의존성 순서로 검색한다. 상위 SDK가 과거에 복사한
iiSDKCore.dll을 최신 Core 설치본보다 먼저 로드하여 프로세스 진입 전에 멈추던
재현을 수정하였다. 현재 프로젝트는 새 build/ 산출물을 우선하며, 자체 이전 설치본은
빌드 의존 DLL보다 앞에 두지 않는다. CTest에는 기본 300초 제한을 적용하고 프로젝트별
명시적 제한은 유지한다.
검색 순서는 매니페스트에 선언된 전이 의존 SDK의 설치본, 현재 SDK의 build/ 및
하위 Native 출력, 나머지 SDK 순서이다. 의존하지 않는 상위 SDK가 복사한 DLL이
현재 SDK의 새 Native DLL을 가리지 않는 회귀 검사도 유지한다.

[2026-10-24 전체 검증 기록](Verification-20261024.md)과 [구조화된 결과](Verification-20261024.json)에 활성 SDK 37개의 구성별 실제 실행 수, 설치 감사, 수정 내용과 제외 범위를 기록하였다.

[2026-10-20 전체 검증 기록](Verification-20261020.md)과 [구조화된 결과](Verification-20261020.json)에 36개 SDK의 실행 수, 구성, 설치 위치 및 원격에서 확인한 commit을 기록하였다.
