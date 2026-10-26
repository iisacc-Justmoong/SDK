# SDK 모듈의 명명 계약

모듈 디렉터리, CMake 프로젝트·패키지·타깃, 공개 헤더 접두사, C++ 모듈
네임스페이스 및 워크스페이스 등록 이름을 같은 철자로 유지한다.
카메라 모듈은 `iiCameraIntegration`, 계정 모듈은 `iiAccountManager`이다.

기술 약어와 고유명은 철자 오류와 구분한다. `iiGUI`, `iiCSMIDI`, `iiHtmlBlock`,
`LVUI`, `LVRS`와 브랜드 명칭은 기존 계약을 유지한다. 기능별 별도 패키지와
컴포넌트 타깃도 모듈 디렉터리와 다른 이름을 사용할 수 있다.

네임스페이스를 변경하면 C++ 링크 심볼이 달라지므로 관련 라이브러리와
소비자를 다시 빌드한다. 과거 검증 로그·Git 이력·원본 증거 스냅샷은 보존한다.

`tests/test_module_names.py`와 `tests/test_account_manager_name.py`는 모듈의
소스·패키지·Windows 목록·동기화 URL 및 주요 제품 참조를 검사한다.
