# 계정 SDK의 명명 계약

계정 SDK의 모듈 이름은 `iiAccountManager`이다. `SDK/iiAccountManager`와
Git 저장소 등록 이름, Windows 빌드 목록, 설치 경로 및 CMake 소비자는 같은
표기를 사용한다. Qt API, Quick 인증 뷰와 Qt 없는 Native API를 모두 포함한다.

표준 설치 경로는 `~/.local/SDK/iiAccountManager`이다. 네이티브 소비자는
`iiAccountManagerNative` 패키지를 찾고 `iiAccountManager::Native`를 링크한다.
Qt 소비자는 `iiAccountManager` 패키지와 `iiAccountManager::iiAccountManager`를
사용한다. 모든 공개 헤더 접두사는 `iiAccountManager/`이다.

`python3 -B -m unittest discover -s tests -p test_account_manager_name.py`가
소스 경로와 패키지 설정, SDK·제품의 주요 소비자 참조의 일치를 검사한다.
Society의 계정 소비자 경로는 `Product/Society/App/Society/Account/`이다.
Windows 검증 및 헤더 배치 검사도
새 이름을 기준으로 수행한다. 계정·세션 데이터나 이미 설치된 제품은 이름
교정의 대상으로 사용하지 않는다.
