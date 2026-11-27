# iiFileProvider 문서 작업 경계

`iiFileProvider::Native`는 앱이 선택한 문서와 파일의 접근, 저장 대상, 스트림 쓰기,
원자적 게시, 링크 자산 확인 및 프리셋 저장을 각각 독립적으로 제공한다. 각 SDK가
파일 제공자를 직접 참조하는 대신 앱이 필요한 인터페이스와 자원, 실행 순서를 연결한다.
자세한 계약과 형식은 [모듈 문서](../../iiFileProvider/docs/DOCUMENT_WORKFLOWS.md)에 있다.

`ScopedDocumentAccessLease`는 명시된 문서 접근과 별도 잠금 파일의 공유·배타 잠금을
묶는다. macOS와 iOS 보안 범위 북마크, Android 문서 URI, Windows FutureAccessList
토큰, Linux 포털 등에서 받은 기술자를 입력받는다. 앱이 소유한 영구 URI 권한과 Windows
토큰은 리스 종료 시 제거하지 않는다. 잠금은 같은 잠금 파일을 사용하는 참여자 사이에서만
성립한다.

`NamedSaveDestination`과 `NamedExportDestination`은 이름과 경로를 확인한다.
`StreamedFilePublication`은 호출자가 소유한 열린 기술자에 청크를 쓰고,
`AtomicOutputCommit`은 호출자가 완성한 임시 파일을 원자적으로 게시한다.
`LinkedAssetFileProbe`는 제한된 입력의 SHA-256을 계산한다.
`PresetFileStore`는 원본 바이트와 버전 1 공통 봉투를 모두 지원한다.

C++ 공개 API와 명시적 네이티브 라이브러리 경로를 요구하는 Python API를 제공한다.
실제 앱의 문서 선택 UI와 최초 플랫폼 권한 발급, 보안 범위 북마크 생성 및 갱신은 앱의
책임이다. 실기기·다른 운영체제 검증 여부는 모듈 문서와 테스트 결과를 확인한다.
