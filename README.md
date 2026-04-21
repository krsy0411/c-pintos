# 📘 Docker기반 Pintos 개발 환경 구축 가이드 

이 문서는 **Windows**와 **macOS** 사용자가 Docker와 VSCode DevContainer 기능을 활용하여 Pintos OS 프로젝트를 빠르게 구축할 수 있도록 도와줍니다.

> [!WARNING]
> ubunbu:22.04 버전은 충분한 테스트와 검증이 되지 않았습니다. 이 점을 주의해서 사용하시기 바랍니다.

> [!NOTE]
> * pintos 도커 환경은 `64비트 기반 X86-64` 기반의 `ubuntu:22.04` 버전을 사용합니다.
>   * kaist-pintos는 오리지널 pintos와 달리 64비트 환경을 지원합니다.
>   * 이번 도커 환경은 ubuntu 22.04를 지원하여 vscode의 최신 버전에서 원격 연결이 안되는 문제를 해결하였습니다.
> * pintos 도커 환경은 kaist-pintos에서 추천하는 qemu 에뮬레이터를 설치하고 사용합니다. 
> * pintos 도커 환경은 9주차부터 13주차까지 같은 환경을 사용합니다. 이 기간동안 별도의 개발 환경을 제공하지 않습니다.
> * 기존 도커 환경과 달리 `vscode`와 통합된 디버깅 환경(F5로 시작하는)을 제공하지 않습니다. 디버깅이 필요한 경우 `gdb`를 사용하세요. 
> * vscode에서 터미널을 오픈하면 자동으로 `source /workspaces/pintos_22.04_lab_docker/pintos/activate`를 실행합니다.

---

## 📂 폴더 구조 설명

```
pintos_22.04_lab_docker/
├── .devcontainer/
│   ├── devcontainer.json      # VSCode에서 컨테이너 환경 설정
│   └── Dockerfile             # pintos 개발 환경 도커 이미지 정의
│
├── pintos
│   ├── threads                # 9주차 threads 프로젝트 폴더
│   ├── userprog               # 10-11주차 user program 프로젝트 폴더
│   └── vm                     # 12-13주차 virtual memory 프로젝트 폴더
│
└── README.md                  # 현재 문서
```

---

## pintos-util 사용법

테스트/디버깅 헬퍼 ([jacti/pintos-util](https://github.com/jacti/pintos-util))가 세팅되어 있습니다. 각 프로젝트 디렉터리(`pintos/threads`, `pintos/userprog`, `pintos/vm`, `pintos/filesys`)에서 실행합니다.

```bash
# 테스트 리스트 출력 후 번호 선택 (ex. "1-5 9 11")
./select_test.sh -q          # 결과만 빠르게 확인
./select_test.sh -g          # VSCode 디버거 연동 (gdb)
./select_test.sh -q -r       # 재빌드 후 실행
./select_test.sh -g -r       # 재빌드 후 디버그

# 통과 이력 리셋
rm .test_status
```

VSCode 디버깅: `-g` 옵션으로 실행 → QEMU가 gdbserver 대기 → VSCode에서 F5 (Threads / User Program / VM 구성 선택).

---

## 🐳 Docker란 무엇인가요?

**Docker**는 애플리케이션을 어떤 컴퓨터에서든 **동일한 환경에서 실행**할 수 있게 도와주는 **가상화 플랫폼**입니다.  

Docker는 다음 구성요소로 이루어져 있습니다:

- **Docker Engine**: 컨테이너를 실행하는 핵심 서비스
- **Docker Image**: 컨테이너 생성에 사용되는 템플릿 (레시피 📃)
- **Docker Container**: 이미지를 기반으로 생성된 실제 실행 환경 (요리 🍜)

### AWS EC2와의 차이점

| 구분 | EC2 같은 VM | Docker 컨테이너 |
|------|-------------|-----------------|
| 실행 단위 | OS 포함 전체 | 애플리케이션 단위 |
| 실행 속도 | 느림 (수십 초 이상) | 매우 빠름 (거의 즉시) |
| 리소스 사용 | 무거움 | 가벼움 |

---

## VSCode DevContainer란 무엇인가요?

**DevContainer**는 VSCode에서 Docker 컨테이너를 **개발 환경**처럼 사용할 수 있게 해주는 기능입니다.

- 코드를 실행하거나 디버깅할 때 **컨테이너 내부 환경에서 동작**
- 팀원 간 **환경 차이 없이 동일한 개발 환경 구성** 가능
- `.devcontainer` 폴더에 정의된 설정을 VSCode가 읽어 자동 구성

---

## 개발 컨테이너: 컨테이너에서 열기

1. VSCode에서 `Ctrl+Shift+P` (Windows/Linux) 또는 `Cmd+Shift+P` (macOS)를 누릅니다.
2. 명령어 팔레트에서 `Dev Containers: Reopen in Container`를 선택합니다.
3. 이후 컨테이너가 자동으로 실행되고 빌드됩니다. 처음 컨테이너를 열면 빌드하는 시간이 오래걸릴 수 있습니다. 빌드 후, 프로젝트가 **컨테이너 안에서 실행됨**.