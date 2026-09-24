# SDK 일괄 재설치

`./install.sh --qt-prefix /Volumes/Storage/Qt/6.8.3/macos`로 이 디렉터리의 SDK를
순회하여 호스트 설치본을 갱신한다. Python 3 표준 라이브러리와 기존 SDK 설치기,
CMake/CTest를 재사용하며 SDK 이름이나 설치 순서 배열을 코드에 두지 않는다.

바로 아래 디렉터리의 `install.sh` 또는 `CMakeLists.txt`를 자동 발견한다.
빈 디렉터리·숨김 디렉터리·빌드 산출물은 설치 대상이 아니다. CMake의 `project`,
`find_package`, `find_dependency`, 네임스페이스 대상에서 로컬 의존성을 읽고
위상 정렬한다. 변수로 계산되는 의존성이나 CMake가 없는 프로젝트는 각 프로젝트의
`install-dependencies.txt`에 추가 의존 디렉터리를 한 줄에 하나씩 선언할 수 있다.
알 수 없는 선언과 순환 의존성은 설치 전에 오류로 처리한다.

기존 `install.sh`가 있으면 그 설치·패키징·검증 규칙을 실행한다. 없으면 CMake
구성 → 빌드 → CTest → 설치 순서로 처리한다. 기존 설치기에서 테스트를 생략하는
경우도 최종 호스트 전체 테스트를 실행한다. 설치기가 이미 전체 테스트를 실행했다면
중복 실행하지 않는다. 기존 `platforms/<host>` 설치 경로도 같은 빌드로 갱신하며
다른 ABI 설치본은 유지한다. 기존 빌드의 기능 옵션은 유지하며,
발견한 의존성의 CMake 경로는 새 설치본으로 갱신한다. 실패한 SDK의 소비자는
실행하지 않고 독립적인 SDK는 계속 설치한다. 하나라도 실패·차단되면 종료 코드는 0이 아니다.
삭제된 IDE 내부의 Ninja/Make를 가리키는 캐시는 현재 PATH의 동일 빌드 도구로
복구한다. 생성기나 기존 기능 옵션은 바꾸지 않으며 대체 도구가 없으면 실패를 기록한다.
이미 검증한 의존성을 그대로 사용하는 실패 복구는 `--only SDK --no-dependencies`로
실행한다. 이 옵션은 선택한 프로젝트만 처리하며, 설치된 의존성의 실제 가용성은
CMake 구성과 해당 프로젝트 테스트로 확인한다. 선택한 SDK 사이의 의존 순서는 유지한다.

```sh
./install.sh --dry-run
./install.sh --jobs 2 --qt-prefix /Volumes/Storage/Qt/6.8.3/macos
./install.sh --only iiSocietyHelper --qt-prefix /Volumes/Storage/Qt/6.8.3/macos
python3 -B -m unittest discover -s tests -p 'test_install_all.py' -v
```

`--cmake SDK`는 특정 SDK에 기존 설치기 대신 표준 CMake 절차를 적용한다.
빌드 동시성은 `--jobs`(기본 2), CTest 동시성은 `--test-jobs`(기본 1)로 지정한다.
GUI·로컬 네트워크 테스트의 자원 경합을 피하도록 테스트는 기본적으로 직렬 실행한다.
자체 `ctest --parallel`을 지정한 개별 설치기는 해당 설정을 우선한다.
예를 들어 LVRS의 기존 CLI는 모든 플랫폼 설치본과 `build/`를 지우므로, 기존
플랫폼·작업 산출물을 보존하는 호스트 갱신에는 `--cmake LVRS`를 사용한다.
추가 CMake 인자는 `--define SDK=-DKEY=VALUE`, 설치기 인자는
`--installer-arg SDK=ARGUMENT`로 반복 지정한다. 기본 설치 경로는
`~/.local/SDK/<디렉터리명>`이다. 기존 설치기는 자신의 prefix 환경 변수와
플랫폼 선택 정책을 유지하므로 해당 SDK의 문서를 따른다. `--prefix-root`는
CMake 직접 설치 및 의존 경로에 적용하며 기존 설치기의 전용 설정을 덮어쓰지 않는다.

호스트 재설치에서 기존 다중 플랫폼 설치기를 사용한다면 해당 SDK의
`*_INSTALL_PLATFORMS=macos` 등의 환경 변수를 설정한다. 다른 ABI는 해당 플랫폼의
툴체인으로 별도 빌드해야 하며, 호스트 검증 성공이 모바일·Wasm 검증을 의미하지 않는다.

로그와 매 실행 결과는 `build/install-all/<실행시각>/report.json`에 남는다.
SDK별 상태, 실행 로그, 설치 manifest와 파일 존재 검사를 구분해서 기록한다.
테스트는 가상 SDK 추가·의존 순서·실패 전파·공백 경로·실제 CMake 설치를 검증하며
임시 파일도 `build/` 아래에 둔다. 앱과 사용자 모델·저장소 데이터는 삭제하지 않는다.
