# Windows 빌드의 폐기 런타임 차단

ONNX·ONNX Runtime은 더 이상 Windows 빌드의 의존성이 아니다. 빌드 환경을 만들 때
상속된 ONNX 환경 변수와 PATH 항목, 이전 iiOnnxRuntime SDK 설치 접두사를 제외한다.
iiDecision 설정에서는 기존 CMake 캐시의 ONNX·Laya 활성화 옵션을 제거한다.

검증 도구는 폐기한 ONNX 가속 검사 이름을 선택적 건너뛰기 목록으로 인정하지 않는다.
과거 검증 보고서는 당시 실행 기록이며 현재의 지원 선언이 아니다.

macOS 등 다른 호스트에서는 다음 명령으로 환경 구성·설정 인자·증거 판정 규칙을
확인할 수 있다. 이 결과는 Windows 컴파일이나 장치 실행을 대신하지 않는다.

```sh
python3 -B -m unittest discover -s SDK/Windows/tests -p 'test_*.py'
PYTHONPYCACHEPREFIX=SDK/Windows/build/pycache python3 -m compileall -q SDK/Windows
```

실제 Windows에서는 기존 `BuildProject.py check`와 `Verify.py`를 사용하여 해당 SDK의
설정·빌드·CTest·설치를 새 소스로 수행해야 한다.
