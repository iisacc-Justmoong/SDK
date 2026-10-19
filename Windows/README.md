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

[2026-10-20 전체 검증 기록](Verification-20261020.md)과 [구조화된 결과](Verification-20261020.json)에 36개 SDK의 실행 수, 구성, 설치 위치 및 원격에서 확인한 commit을 기록하였다.
