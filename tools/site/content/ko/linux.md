---
status: translated
title: Linux
nav: Linux
description: NVIDIA 카드가 있는 Linux에서 Proton으로 GeForce 에디션을 실행합니다. 사진과 동영상을 쓸 수 있고, 실시간 카메라는 없습니다.
---
Linux 패키지는 Steam으로 잘 알려진 호환성 계층인 Proton에서 Windows 앱을 실행합니다. 실행 스크립트가 모든 것을 알아서 준비합니다. DLSS 5 런타임이 Windows용으로만 있기 때문에 Linux 전용 빌드는 없습니다.

## 필요한 것 {#requirements}

- 데스크톱 세션(X11 또는 Wayland)이 있는 64비트 Linux.
- NVIDIA GeForce RTX 카드(20, 30, 40, 50 시리즈)와 NVIDIA 드라이버 5xx 이상. Linux에서는 Radeon 카드를 지원하지 않습니다.
- `python3`(3.10 이상)와 `curl` 또는 `wget`.
- 처음 실행할 때 Proton이 쓸 약 3 GB의 디스크 공간. Steam은 필요 없습니다.

## 설치와 실행 {#install}

1. [다운로드 섹션](index.html#download)에서 `VRChatDLSS5Cam-linux-x86_64.tar.gz`를 내려받습니다.
2. 압축을 풀고 실행 스크립트를 실행합니다.
   ```bash
   tar xzf VRChatDLSS5Cam-linux-x86_64.tar.gz
   cd VRChatDLSS5Cam
   ./vrchat-dlss5-cam
   ```
   => 처음 실행할 때 Proton을 내려받으며, 몇 분 걸립니다. 그다음 창이 열립니다.
3. 선택 사항: `./install-linux.sh`를 실행하면 애플리케이션 메뉴에 앱이 추가되고 `vrchat-dlss5-cam` 명령을 쓸 수 있습니다. `./install-linux.sh --remove`로 둘 다 다시 없앨 수 있습니다.

설정, 프리셋, 로그, 라이브러리는 `~/.local/share/VRChatDLSS5Cam/data`에 저장됩니다. 업데이트는 Windows와 같은 방식으로 동작합니다.

## 되는 것과 안 되는 것 {#support}

| | Proton에서 |
|---|---|
| 이미지: DLSS 5, 깊이, 초해상도, 프리셋, 실행 취소 | 동작합니다. |
| 입력으로 쓰는 동영상과 애니메이션 이미지 | 동작합니다. |
| 동영상 출력 | WebP, GIF, APNG, PNG 시퀀스. MP4와 소리는 없습니다. Proton에는 H.264나 HEVC 인코더가 없기 때문입니다. MP4로 저장될 동영상은 WebP로 저장됩니다. |
| 실시간 카메라(Spout) | 쓸 수 없습니다. Spout은 Windows에만 있습니다. |
| 모션 벡터 | FSR 옵티컬 플로우. Proton에서는 NVIDIA Optical Flow를 쓸 수 없습니다. |
| MCP, 업데이트, 미러 사이트, 언어 | 동작합니다. |
| Radeon 에디션 | 지원하지 않습니다. |

## 명령줄 {#command-line}

실행 스크립트는 앱의 옵션을 그대로 받고, Linux 경로를 앱이 기대하는 형식으로 바꿔 줍니다.

```bash
./vrchat-dlss5-cam --headless --add ~/Pictures/shot.png --process ~/Pictures/out
./vrchat-dlss5-cam --open ./clip.mp4 --lang zh
```

`--linux-info`는 사용 중인 폴더와 버전을 출력하고, `--linux-reset`은 Proton 환경을 다시 만듭니다(설정과 라이브러리는 유지됩니다). [모든 옵션](command-line.html)

## 문제가 생기면 {#troubleshooting}

- **처음 실행이 몇 분 걸립니다.** Proton과 그 런타임(약 2.5 GB)을 한 번만 내려받는 과정입니다.
- **창은 열리지만 반응하지 않습니다.** 잠긴 화면이나 화면 잠금 프로그램이 디스플레이를 붙잡고 있습니다. 세션의 잠금을 해제하세요.
- **이미지 영역에 회색 체커보드만 보입니다.** 앱이 Proton 자체의 셰이더 컴파일러로 실행되고 있습니다. Windows용 zip 대신 Linux 패키지를 쓰세요. Linux 패키지에는 올바른 컴파일러가 들어 있습니다.
- **DLSS 5가 계속 비활성입니다.** NVIDIA 드라이버의 Windows 프로그램용 브리지가 없습니다. `./vrchat-dlss5-cam --linux-info`로 `nvngx.dll`을 찾았는지 확인할 수 있습니다. NVIDIA 드라이버 5xx 이상을 설치하세요.

Linux 전체 안내는 GitHub의 [LINUX.md](https://github.com/AlanBacker/VRChat-DLSS5-Cam/blob/main/docs/LINUX.md)에 있습니다.
