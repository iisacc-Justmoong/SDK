# SDK 모듈 통합과 경계

## 구성요소 소유권

| 기존 모듈 | 소유 모듈 | 새 대상 | 이전 패키지와 헤더 |
| --- | --- | --- | --- |
| iiWebGPU | iiGPU | `iiGPU::WebGPU` | `iiWebGPU::iiWebGPU`, `<iiWebGPU/...>` 호환 |
| iiHtmlBlock | iiXml | `iiXml::HtmlBlock` | `iiHtmlBlock::iiHtmlBlock`, `<iiHtmlBlock>`, `<Src/...>` 호환 |
| iiBrushTools | iiPaintEngine | `iiPaintEngine::BrushTools` | `iiBrushTools::iiBrushTools`, `<iiBrushTools/...>` 호환 |

세 구성요소는 각각 선택할 수 있다. 기본 GPU 대상은 Dawn을, XML Native는 Qt를,
그림 엔진의 기본 스트리밍 계층은 전체 획 작성 계층을 요구하지 않는다. 기존
설치 위치가 컴파일러 `CPATH`나 CMake 캐시에 명시된 경우 새 패키지를 우선하도록
소비자의 경로를 갱신해야 한다. 기존 기능의 이전 이름은 제거하지 않았다.

## 구분해야 할 계층과 추가 중복

- iiNetwork는 소켓·TLS·HTTP 전송, iiWebServer는 라우팅·파일 공유·릴레이와
  세션 정책을 소유한다. iiWebServer의 HTTP 객체도 iiNetwork 서버를 조합한다.
- iiCSMIDI는 MIDI 파일 문서이고 iiDeviceMultimedia의 MIDI UMP는 실제 장치 통신이다.
- iiGraphicAdjust의 이미지 연산과 iiPaintEngine의 입력·획·편집 이력은 독립적으로
  소비되므로 서로에게 의존시키지 않는다.
- iiGeneralDocument에는 iiXml과 iiHtmlBlock에서 이식한 Qt 없는 별도 마크업 파서가
  여전히 있다. 문서 메타데이터·수정 범위·오류 계약을 유지하는 공통 네이티브
  파서 추출과 소비자 전환은 아직 완료되지 않았다.
- iiGlobalSearch는 Society 컨테이너의 제공자 정보 인덱싱과 별도의 웹 검색을
  제공한다. 컨테이너 검색과 겹치는 부분은 있지만 전체를 컨테이너 모듈에 합치면
  웹 검색의 책임까지 끌어들이게 된다. 동시 진행 중인 검색 기능 변경을 보호하며
  공유할 수 있는 인덱싱 경계를 별도로 검토해야 한다.

각 구성요소의 빌드·설치 소비자 검증과 운영체제별 범위는 소유 모듈의 문서와
`build/` 테스트 로그를 따른다. macOS 검사는 Windows·Linux 실기기 실행을 대신하지 않는다.
