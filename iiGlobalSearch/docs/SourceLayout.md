# 소스 구조

`src/Answers/`는 공통 검색 답변, `src/Local/`은 선택한 Society 컨테이너 검색, `src/Web/`은 웹 검색과 전송, `src/CLI/`는 명령행 진입점을 소유한다. 구현과 관련 헤더를 함께 두며 구성 요소 간 의존은 기존 공개 인터페이스를 따른다.

빌드 중 생성한 연결 헤더와 설치된 원본 헤더는 기존 `<Answer.h>`, `<GlobalSearch.h>`, `<WebSearch.h>` 경로를 유지한다. `iiGlobalSearch.source_layout`은 각 책임의 소스 위치를 검사한다. 실제 인터넷·브라우저·사용자 컨테이너의 동작 여부는 구조 검사와 별도로 검증한다.

설치된 독립 소비자의 `iiGlobalSearch.installed_api`는 세 공개 헤더, 답변 값과 거부되는 파일 URL을 확인하며 외부 네트워크를 사용하지 않는다.
