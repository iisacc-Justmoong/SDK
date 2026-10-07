# GitHub를 통한 Workspace 동기화

`sync_workspace.ps1`과 `sync_workspace.py`는 GitHub 저장소를 `~/Workspace/SDK`, `Product`, `Service`의
정확한 상대 경로에 클론한다. 기존 저장소는 현재 브랜치를 검증한 뒤
`fetch`와 `merge --ff-only`로 갱신한다. 소스 디렉터리, 빌드 출력, 모델,
설치본과 자격 증명은 직접 복사하지 않는다.

윈도우용 `sync_workspace.ps1`은 Windows PowerShell 5.1 호환 문법을 사용하며
PowerShell과 Git for Windows로 실행한다. Python 설치는 필요하지 않다.
기존 비공개 GitHub 저장소에 접근할 수 있는 인증이 필요하며,
LFS 저장소는 Git LFS도 필요하다. 다음 명령은 윈도우 PowerShell에서 실행한다.

```powershell
$sdk = Join-Path $HOME 'Workspace/SDK'
New-Item -ItemType Directory -Force (Join-Path $HOME 'Workspace') | Out-Null
if (Test-Path -LiteralPath (Join-Path $sdk '.git')) {
    git -C $sdk pull --ff-only
} else {
    git clone --config core.longpaths=true --config core.autocrlf=false --branch main https://github.com/iisacc-Justmoong/SDK.git $sdk
}
if ($LASTEXITCODE -ne 0) { throw 'SDK Git synchronization failed' }
& ([scriptblock]::Create((Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $sdk 'sync_workspace.ps1'))))
```

이미 SDK가 클론되어 있으면 위 명령이 SDK 저장소를 갱신한 뒤 동기화를 실행한다.
`Get-Content`로 Git에서 받은 도구를 현재 PowerShell에서 실행하므로
별도의 프로세스나 실행 정책 변경이 필요하지 않다. 스크립트는 실패 시
오류를 발생시키며 현재 PowerShell 창을 종료하지 않는다. 일반적인
Git for Windows 인증은 Git Credential Manager가 처리한다. 비공개 저장소
인증 실패는 실패로 기록하며 공개 범위나 자격 증명을 변경하지 않는다.

이전 Python 명령에서 `Python`만 출력되고 `VERIFIED`·`FAILED`·Python traceback이
나오지 않았다면 동기화 도구가 실행되었다고 볼 수 없다. Windows의 Python 앱
실행 별칭이나 사용할 수 없는 인터프리터가 원인일 수 있다. 이 경우 위 명령으로
SDK를 갱신하고 PowerShell 도구를 실행한다. 실제 도구 실행 전에는 보고서가
생성되지 않으므로 보고서 경로만 안내하는 이전 오류는 진단 근거가 되지 않는다.

`workspace-repositories.json`은 로컬 디렉터리명과 GitHub 저장소명이 다른
경우를 명시한다. `Product/.Deprecated/AISNS`는 `WeUs`를,
`Product/Thinking Space`는 `Thinking-Space`를 클론한다.
루트 SDK와 Product 저장소를 먼저 처리하고 각 독립 저장소를 그 아래에 둔다.
매니페스트에 기록된 빈 프로젝트 디렉터리도 생성한다.

동기화 결과는 `~/Workspace/build/workspace-sync/report.json`에 저장한다.
각 저장소를 처리하기 전에 `SYNC`, 성공 시 `VERIFIED`, 실패 시 Git 오류와
함께 `FAILED`를 표시한다. 보고서의 `complete: true`, `verified: 42`, `total: 42`가
현재 매니페스트의 전체 동기화 성공 조건이다. 재실행하면 완료한 클론을 재사용한다.
모든 저장소의 `HEAD`가 해당 GitHub 브랜치와 일치하고 작업 트리가 깨끗한
경우에만 성공한다. 기록한 맥 커밋이 GitHub의 최신 커밋에 포함되는지도
검증하므로, 매니페스트 이후 추가된 정상적인 커밋은 함께 받을 수 있다.
SDK 루트 저장소는 자기 커밋을 매니페스트에 순환 참조하지 않고 실행 시점의
`origin/main`과 대조한다.

다른 origin, 다른 브랜치, 미커밋 변경, 로컬 전용 커밋, 경로 충돌이 있으면
해당 저장소를 실패로 남기고 기존 작업을 보존한다. 다른 독립 저장소는
계속 처리한다. reset, clean, 강제 푸시와 기존 파일 삭제를 사용하지 않는다.
Windows의 긴 경로를 지원하고 새 클론에서는 자동 줄바꿈 변환을 끈다.
기존 체크아웃의 줄바꿈 설정은 유지하여 정상적인 CRLF 파일이 로컬 수정으로
잘못 판별되는 것을 방지한다. Git LFS 객체와
등록된 서브모듈도 동기화한다. 실제 윈도우의 클론 성공은 해당 머신의
결과 보고서로 확인해야 한다. 소스 동기화 성공은 윈도우 빌드·실행 성공과
별도로 판단한다.

```sh
# Python 도구는 Python 3.10 이상인 맥·리눅스·윈도우에서 사용할 수 있다.
# 이미 존재하는 체크아웃의 Git 일치 상태만 검사
python sync_workspace.py --root /Volumes/Storage/Workspace --audit-only

# 실제 임시 Git 원격을 이용한 클론, 갱신, 충돌, 기존 작업 보존 검증
python -B -m unittest discover -s tests -p 'test_sync_workspace.py' -v

# 실제 PowerShell 프로세스와 임시 Git 원격을 이용한 윈도우 진입점 검증
# PATH에 pwsh/powershell이 없으면 POWERSHELL_BINARY에 실행 파일 경로를 지정한다.
python -B -m unittest discover -s tests -p 'test_sync_workspace_powershell.py' -v
```

PowerShell 도구의 경로 및 보고서 옵션은 `-WorkspaceRoot`, `-Manifest`,
`-AuditOnly`이다. macOS의 PowerShell 7 테스트 결과는 실제 윈도우의
Windows PowerShell 5.1 실행이나 GitHub 인증·클론 완료를 증명하지 않는다.
