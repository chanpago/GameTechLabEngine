# GameTechLabEngine 클라이언트·엔진 프로그래머 네트워크 포트폴리오 정리

## 1. 프로젝트 목표

이 프로젝트의 네트워크 구현 목표는 대규모 온라인 서버를 만드는 것이 아니라, DirectX 11 기반 자체 엔진에 실제 클라이언트·서버 구조를 통합하고 실시간 게임에서 필요한 네트워크 개념을 코드와 실행 영상으로 증명하는 것이다.

현재 구현은 클라이언트·엔진 프로그래머가 알아야 할 기본 범위를 충족하며, 다음 내용을 직접 설명할 수 있는 수준을 목표로 한다.

- TCP와 UDP의 특성 및 역할 분리
- 서버 권위 이동 동기화
- 네트워크 스레드와 게임 스레드 분리
- UDP 패킷 손실과 순서 역전 대응
- 원격 플레이어 보간
- 로컬 입력 예측과 서버 보정
- 지연·패킷 손실 환경에서의 동작 검증

## 2. 한 문장 설명

> DirectX 11 기반 자체 엔진에 C++/Winsock 네트워크 모듈과 Dedicated Server를 통합하고, TCP 제어 채널과 UDP 이동 채널을 분리했으며, 서버 권위 이동·원격 보간·Client-side Prediction·Server Reconciliation·인위적 지연 및 패킷 손실 시뮬레이션을 구현했습니다.

## 3. 전체 구조

```text
┌──────────────────────── Client A ────────────────────────┐
│ Input → Local Prediction → NetworkManager → TCP / UDP   │
│                           ↑                              │
│ Render ← Actor State ← Main Thread Packet Processing    │
└───────────────────────────┬──────────────────────────────┘
                            │
             TCP: 접속, 생성/퇴장, Ping/Pong
             UDP: 이동 입력, Player State Snapshot
                            │
┌───────────────────────────▼──────────────────────────────┐
│                    Dedicated Server                     │
│ Socket Thread → Thread-safe Queue → Fixed 30Hz Tick     │
│              → Input Validation → Authoritative State   │
└───────────────────────────┬──────────────────────────────┘
                            │
┌───────────────────────────▼──────────────────────────────┐
│                       Client B                          │
│ Network Thread → Queue → Remote Interpolation → Render  │
└──────────────────────────────────────────────────────────┘
```

프로젝트는 다음 세 부분으로 나뉜다.

| 프로젝트 | 역할 |
|---|---|
| `Mundi` | 자체 엔진과 네트워크 클라이언트 |
| `NetworkShared` | 패킷 타입, 직렬화, TCP 버퍼, UDP 프로토콜 |
| `GameTechLabServer` | 렌더링에 의존하지 않는 Dedicated Server |

## 4. 구현된 핵심 기능

### 4.1 TCP 제어 채널

TCP는 반드시 도착하고 순서가 보장되어야 하는 정보에 사용한다.

- 접속 handshake와 프로토콜 버전 검사
- 서버 발급 Player ID
- Player Spawn/Despawn
- Ping/Pong과 RTT 측정
- UDP 사용 여부 협상
- UDP를 끈 경우 이동 입력과 상태 동기화 fallback

TCP는 메시지 단위가 아닌 byte stream이므로 별도 패킷 헤더와 Receive Buffer를 사용한다.

```cpp
struct FPacketHeader
{
    uint16_t Size;
    uint16_t Type;
};
```

이를 통해 다음 상황을 처리한다.

- 패킷 하나가 여러 번의 `recv()`로 나뉘어 도착하는 Partial Receive
- 여러 패킷이 한 번의 `recv()`로 합쳐지는 경우
- 한 번의 `send()`로 전체 데이터가 전송되지 않는 Partial Send

### 4.2 UDP 이동 채널

UDP는 일부 데이터가 유실되더라도 최신 상태를 빠르게 받는 것이 중요한 이동 정보에 사용한다.

- 클라이언트 → 서버: Player ID, 세션 토큰, 입력 Sequence, 이동 축
- 서버 → 클라이언트: Player ID, Server Tick, 위치, 회전, 마지막 처리 입력 Sequence
- TCP handshake에서 발급한 64비트 UDP 세션 토큰 검증
- 오래되거나 순서가 뒤바뀐 입력 Sequence 폐기
- 오래된 Server Tick Snapshot 폐기
- 250ms 동안 새 입력이 없으면 서버 이동 정지

UDP 데이터그램은 Magic, 프로토콜 버전, 타입, 전체 크기를 검증한다. 손실된 데이터그램은 재전송하지 않고 다음 최신 상태를 사용한다.

### 4.3 서버 권위 이동

클라이언트가 자신의 최종 위치를 서버에 통보하지 않는다. 클라이언트는 이동 축만 보내고 서버가 고정 30Hz Tick에서 위치를 계산한다.

```text
Client: W 입력 전송
           ↓
Server: 입력 범위와 Sequence 검증
           ↓
Server: Fixed Delta로 위치 계산
           ↓
Client: 서버가 계산한 상태 수신
```

이 구조의 장점은 다음과 같다.

- 클라이언트 FPS에 따라 이동 속도가 달라지지 않는다.
- 클라이언트가 임의 위치를 전송하는 방식보다 서버 검증이 쉽다.
- 모든 클라이언트가 동일한 권위 상태를 기준으로 동기화된다.

### 4.4 네트워크 스레드와 게임 스레드 분리

소켓 I/O는 별도 네트워크 스레드에서 수행한다. 네트워크 스레드는 `UWorld`나 `AActor`에 직접 접근하지 않는다.

```text
Network Thread
  recv / send / packet parsing
             ↓
     Thread-safe Queue
             ↓
Game Main Thread
  Actor Spawn / Despawn / Transform 반영
```

이 구조는 다음 문제를 방지한다.

- Blocking 소켓 때문에 렌더링 프레임이 멈추는 문제
- 네트워크 스레드와 게임 스레드가 동시에 Actor를 수정하는 Data Race
- 삭제된 UObject를 네트워크 스레드가 참조하는 Lifetime 문제

### 4.5 원격 플레이어 보간

원격 플레이어는 서버 Snapshot이 도착한 순간마다 위치를 즉시 순간이동시키지 않고 목표 위치와 회전으로 부드럽게 수렴시킨다.

```text
Server Snapshot A ───── Server Snapshot B
        └─ 렌더 프레임에서 위치·회전 보간 ─┘
```

서버는 30Hz, 렌더링은 그보다 높은 빈도로 실행될 수 있으므로 보간이 없으면 원격 플레이어가 계단식으로 움직여 보인다.

### 4.6 Client-side Prediction

로컬 플레이어는 서버 응답을 기다리지 않고 입력을 즉시 화면에 반영한다.

```text
키 입력 → 로컬 위치 즉시 변경 → 입력 패킷 전송
```

따라서 지연이 있더라도 자기 캐릭터의 조작 반응은 즉각적으로 보인다. 상대 클라이언트에서 보이는 움직임은 실제 네트워크 지연 후 반영된다.

### 4.7 Server Reconciliation과 입력 재현

단순 보정만 사용하면 다음 현상이 발생한다.

```text
로컬 예측 위치 3.0
→ 지연된 서버 위치 2.5 수신
→ 로컬 캐릭터가 2.5 방향으로 뒤로 끌림
```

이를 줄이기 위해 전송에 성공한 입력의 Sequence와 예측 이동량을 Pending Input Buffer에 저장한다.

```text
1. 서버 Snapshot과 LastProcessedInput 수신
2. 서버가 처리한 Sequence 이하의 입력 제거
3. 서버 권위 위치를 기준으로 설정
4. 아직 처리되지 않은 입력을 순서대로 재적용
5. 전송 주기 사이에 발생한 현재 프레임 이동도 재적용
6. 결과 위치로 부드럽게 수렴
```

이 방식은 서버 권위를 유지하면서 지연된 과거 위치 때문에 로컬 플레이어가 크게 후퇴하는 현상을 억제한다. 큰 위치 오차는 즉시 보정하고 작은 오차는 부드럽게 수렴시킨다.

## 5. 지연 및 패킷 손실 시뮬레이션

실제 인터넷 환경을 재현하기 위해 서버 UDP 송수신 경로에 다음 기능을 구현했다.

- 단방향 인위적 지연: `0~300ms`
- UDP 패킷 손실: `0~30%`
- 고정 Random Seed를 이용한 재현 가능한 테스트
- 실제 지연·폐기 데이터그램 누적 서버 로그
- 클라이언트 HUD에 적용된 설정 표시

예를 들어 단방향 지연이 `150ms`이면 다음과 같이 동작한다.

```text
Client A 입력
→ 150ms
→ Server 처리
→ 150ms
→ Client B Snapshot 수신
```

따라서 B에서는 약 `300ms + 서버 Tick 및 보간 시간` 뒤에 A의 움직임이 보이는 것이 정상이다. 패킷까지 유실되면 다음 최신 Snapshot을 기다리므로 일시적으로 더 늦거나 끊겨 보일 수 있다.

시뮬레이션은 UDP 이동에만 적용한다. TCP 이동 비교 모드에서는 우회된다.

## 6. 영상 비교용 On/Off 기능

UDP/TCP 이동과 Reconciliation은 클라이언트 접속 시 선택할 수 있다. HUD의 `Movement`와 `Reconciliation` 항목으로 실제 상태를 확인한다.

### UDP 이동 + Reconciliation On

권장 기본 설정이다.

```powershell
.\Mundi.exe -net -server=127.0.0.1 -port=7777 -udp-movement=on -reconciliation=on
```

예상 화면:

```text
Movement       UDP
Reconciliation On
```

### UDP 이동 + Reconciliation Off

지연된 서버 위치로 단순 보정될 때 로컬 플레이어가 뒤로 끌리는 현상을 비교한다.

```powershell
.\Mundi.exe -net -server=127.0.0.1 -port=7777 -udp-movement=on -reconciliation=off
```

### TCP 이동 + Reconciliation On

이동 transport를 TCP로 바꿔 UDP와 비교한다.

```powershell
.\Mundi.exe -net -server=127.0.0.1 -port=7777 -udp-movement=off -reconciliation=on
```

한 서버에 UDP 이동 클라이언트와 TCP 이동 클라이언트가 동시에 접속할 수 있다. 서버는 각 클라이언트가 선택한 transport로 모든 플레이어의 상태를 전송한다.

이 기능은 접속 handshake에서 결정되므로 실행 중 변경하지 않고 클라이언트를 다시 실행해 비교한다.

## 7. 실행 방법

### 정상 환경

```powershell
cd C:\Users\USER\Desktop\MakeGame\GameTechLabEngine\Binaries\Release_StandAlone

$server = Start-Process .\GameTechLabServer.exe -ArgumentList '-no-netsim' -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 1

Start-Process .\Mundi.exe -ArgumentList '-net','-server=127.0.0.1','-port=7777','-udp-movement=on','-reconciliation=on'
Start-Process .\Mundi.exe -ArgumentList '-net','-server=127.0.0.1','-port=7777','-udp-movement=on','-reconciliation=on'
```

### 지연 150ms 및 손실 10%

```powershell
cd C:\Users\USER\Desktop\MakeGame\GameTechLabEngine\Binaries\Release_StandAlone

$server = Start-Process .\GameTechLabServer.exe -ArgumentList '-latency=150','-loss=10' -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 1

Start-Process .\Mundi.exe -ArgumentList '-net','-server=127.0.0.1','-port=7777','-udp-movement=on','-reconciliation=on'
Start-Process .\Mundi.exe -ArgumentList '-net','-server=127.0.0.1','-port=7777','-udp-movement=on','-reconciliation=on'
```

### 종료

```powershell
Stop-Process -Name Mundi,GameTechLabServer -Force -ErrorAction SilentlyContinue
```

## 8. Notion 영상 구성 제안

### 영상 1: 기본 다중 클라이언트 동기화

1. 서버 실행
2. 클라이언트 A와 B 접속
3. 서로 다른 Player ID 확인
4. A를 WASD로 이동
5. B에서 A의 위치와 회전 동기화 확인
6. 한 클라이언트 종료 후 다른 화면에서 Actor 제거 확인

강조할 내용:

- 서버 발급 ID
- 서버 권위 이동
- TCP 연결과 UDP 이동의 역할 분리
- Spawn/Despawn 처리

### 영상 2: UDP와 TCP 이동 비교

1. 같은 서버에 UDP 이동 클라이언트와 TCP 이동 클라이언트 실행
2. HUD의 `Movement UDP/TCP` 표시
3. 이동 반응과 패킷 통계 비교
4. 이동 데이터가 선택한 transport로 전달된다는 점 설명

강조할 내용:

- UDP가 무조건 전송 자체가 빠르다는 뜻은 아님
- 이동에서는 유실된 과거 패킷보다 최신 상태가 중요함
- TCP는 손실 복구와 순서 보장 때문에 뒤 패킷이 대기할 수 있음

### 영상 3: Reconciliation Off/On 비교

1. 서버를 `150ms` 지연으로 실행
2. Reconciliation Off 클라이언트에서 이동
3. 지연된 서버 위치 때문에 뒤로 끌리는 현상 촬영
4. Reconciliation On으로 다시 실행
5. 로컬 즉시 반응과 미확인 입력 재현 비교

강조할 내용:

- 문제가 발생한 이유
- `LastProcessedInput`의 역할
- 서버 권위와 로컬 반응성을 동시에 유지한 방법

### 영상 4: 패킷 손실 환경

1. `150ms / 10% loss` 서버 실행
2. HUD의 `UDP Net Sim` 확인
3. A는 로컬 예측으로 즉시 움직이는 모습 촬영
4. B에서는 네트워크 지연 후 움직이는 모습 촬영
5. `UDP Dropped` 증가와 서버 로그 확인

강조할 내용:

- UDP 손실 시 재전송을 기다리지 않고 다음 최신 Snapshot 사용
- Sequence로 오래된 패킷 폐기
- 보간으로 원격 이동의 시각적 끊김 완화

## 9. 면접에서 설명할 수 있어야 하는 질문

### 이동에 UDP를 사용한 이유는 무엇인가?

이동 상태는 과거 패킷을 반드시 복구하는 것보다 최신 상태를 빠르게 받는 것이 중요하다. UDP는 순서와 도착을 보장하지 않지만 애플리케이션에서 Sequence를 검사하고 오래된 상태를 폐기할 수 있다.

### 접속과 Spawn/Despawn에는 TCP를 사용한 이유는 무엇인가?

접속 결과와 객체 생성·삭제는 한 번 누락되면 게임 상태 자체가 달라질 수 있으므로 신뢰성과 순서 보장이 필요하다.

### TCP에서 패킷 경계는 어떻게 처리했는가?

TCP는 byte stream이므로 `Size + Type` 헤더와 누적 Receive Buffer를 사용했다. 완전한 패킷만 추출하고 남은 byte는 다음 `recv()`까지 보관한다.

### 네트워크 스레드에서 Actor를 직접 수정하지 않은 이유는 무엇인가?

게임 객체는 메인 스레드에서 생성·삭제·Tick되므로 다른 스레드에서 동시에 접근하면 Data Race와 Lifetime 문제가 생긴다. 네트워크 스레드는 값 타입 패킷만 Queue로 전달하고 메인 스레드가 Actor에 적용한다.

### UDP 패킷 순서가 바뀌면 어떻게 하는가?

클라이언트 입력은 Input Sequence, 서버 상태는 Server Tick을 비교한다. 마지막 적용 값보다 오래된 데이터그램은 폐기한다.

### 로컬 캐릭터가 서버 위치 때문에 뒤로 끌린 이유는 무엇인가?

클라이언트가 즉시 예측한 현재 위치에 지연된 과거 서버 위치를 단순 적용했기 때문이다. 서버가 처리한 입력까지만 버리고 남은 입력을 권위 위치 위에 재적용하는 Reconciliation으로 완화했다.

### A는 즉시 움직이는데 B에서는 늦게 보이는 이유는 무엇인가?

A는 Client-side Prediction으로 서버 응답 전에 움직인다. B는 서버가 처리해 전송한 Snapshot만 볼 수 있으므로 네트워크 왕복 경로만큼 늦게 보이는 것이 정상이다.

## 10. 현재 범위와 의도적으로 제외한 기능

현재 구현은 클라이언트·엔진 프로그래머 포트폴리오에 필요한 네트워크 기본 범위를 충족한다.

구현 완료:

- TCP/UDP 역할 분리와 이동 transport 전환
- 다중 세션 및 서버 발급 Entity ID
- 직렬화와 TCP Stream Framing
- Partial Send/Receive
- Network Thread와 Main Thread 분리
- 서버 권위 이동
- UDP Sequence 처리
- 원격 보간
- 로컬 예측과 서버 보정 및 입력 재현
- 지연·손실 시뮬레이션
- Ping/RTT와 Debug HUD

의도적으로 제외:

- IOCP 서버
- DB와 계정 로그인
- Matchmaking
- NAT Punch Through
- 암호화와 Anti-cheat
- Reliable UDP 전체 구현
- 대규모 MMO Interest Management

IOCP는 Windows 고성능 서버 또는 대규모 동시 접속을 강조하는 서버 프로그래머 포트폴리오에서는 의미가 있다. 현재 프로젝트의 목표인 자체 엔진 통합과 실시간 클라이언트 네트워크 구조를 설명하는 데에는 필수 사항이 아니다.

## 11. 최종 평가

이 프로젝트는 단순히 소켓으로 위치를 주고받는 수준을 넘어 다음 문제를 실제 코드로 다룬다.

- 신뢰성이 필요한 데이터와 최신성이 중요한 데이터의 transport 분리
- 렌더 프레임과 서버 Tick 차이
- 멀티스레드 게임 객체 수명 보호
- UDP 손실과 순서 역전
- 서버 권위와 로컬 입력 반응성의 충돌
- 지연된 서버 상태로 인한 위치 후퇴와 입력 재현 해결

따라서 클라이언트·엔진 프로그래머가 네트워크를 이해하고 있음을 보여주는 포트폴리오 범위로 충분하다. 이후에는 기능을 무리하게 늘리기보다 아키텍처 그림, 비교 영상, 문제 발생 원인과 해결 과정을 명확히 설명하는 것이 중요하다.
