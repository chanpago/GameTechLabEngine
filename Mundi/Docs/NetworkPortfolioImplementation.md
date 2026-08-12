# GameTechLabEngine 네트워크 포트폴리오 구현 안내

## 구현 범위

네트워크 구현 명세서의 1차 완료 범위인 Phase 1~10, Phase 11의 UDP 이동, Phase 12의 Latency/Packet Loss Simulation을 구현했다.

- Winsock2 기반 전용 서버 `GameTechLabServer.exe`
- 복수 클라이언트 세션과 서버 발급 Entity ID
- 명시적 little-endian 직렬화와 TCP 스트림 프레이밍
- Partial Send/Receive 및 한 번에 합쳐진 여러 패킷 처리
- 클라이언트와 서버의 네트워크 스레드
- 크기가 제한된 mutex 기반 thread-safe queue
- `UWorld` 메인 스레드에서만 Actor Spawn/Despawn/상태 반영
- 서버 권위 입력 기반 이동과 30Hz 상태 브로드캐스트
- TCP 제어 채널과 UDP 이동 채널 분리
- TCP 핸드셰이크로 발급한 UDP 세션 토큰 검증
- UDP 입력 sequence와 서버 tick sequence를 이용한 오래된 데이터그램 폐기
- 250ms 동안 새 UDP 입력이 없을 때 서버 이동축을 정지시키는 입력 타임아웃
- 실행 플래그로 UDP/TCP 이동 transport를 선택하는 혼합 클라이언트 지원
- 서버가 확인한 입력 sequence까지 제거하고 미확인 입력을 재현하는 client-side prediction/reconciliation
- UDP 양방향 인위적 지연(0~300ms)과 패킷 손실(0~30%) 시뮬레이션
- 고정 seed 기반 재현 가능한 손실 테스트와 지연/폐기 누적 서버 로그
- 로컬 예측/보정과 원격 플레이어 보간
- Ping/RTT, 트래픽 및 패킷 통계 HUD
- 설정 파일과 서버 로그

IOCP 서버 전환은 별도의 선택 심화 범위로 남겨 두었다.

## 프로젝트 구성

```text
Mundi.sln
├─ Mundi                 기존 DX11 엔진 + 네트워크 클라이언트
├─ NetworkShared         클라이언트/서버 공용 정적 라이브러리
└─ GameTechLabServer     렌더러에 의존하지 않는 콘솔 전용 서버
```

주요 소스 위치는 다음과 같다.

```text
NetworkShared/
├─ Protocol/             패킷 타입, 직렬화, 역직렬화
├─ Buffer/               ReceiveBuffer, SendBuffer
└─ Common/               네트워크 타입, bounded thread-safe queue

GameTechLabServer/
├─ DedicatedServer.*     WSAPoll I/O, 세션, 30Hz 서버 로직
├─ ServerConfig.*        Server.ini 입력
└─ ServerLogger.*        콘솔 및 파일 로그

Mundi/Source/Runtime/Network/
├─ NetworkManager.*      클라이언트 소켓 스레드와 메인 스레드 packet pump
└─ NetworkPlayerActor.*  로컬 입력/예측 및 원격 보간
```

## 스레드와 데이터 흐름

```text
Client Network Thread
  TCP recv -> ReceiveBuffer -> Packet -> IncomingQueue
  UDP recv -> Datagram validation -> UdpStateQueue
                                      |
---------------- thread boundary -----|----------------
                                      v
UWorld::Tick (Main Thread)
  queue drain -> spawn/despawn/state -> Actor Tick -> Render

Local Player Actor
  WASD -> UdpMoveInput -> UdpOutgoingQueue -> Client Network Thread -> UDP send
```

네트워크 스레드는 소켓, 바이트 버퍼, 패킷 값만 다룬다. `UWorld`, `AActor`, 컴포넌트의 생성·삭제·Transform 변경은 `UWorld::Tick`의 Actor tick 목록 복사 전에 메인 스레드에서 수행한다.

서버도 소켓 I/O와 게임 로직을 분리한다. 네트워크 스레드가 TCP/UDP 이벤트 큐를 만들고, 서버 메인 스레드는 고정 30Hz로 입력을 검증·적용한 뒤 등록된 UDP endpoint에 snapshot을 전송한다. 이동 적분에는 서버의 fixed delta만 사용한다. 네트워크 시뮬레이션을 켜면 네트워크 스레드가 UDP 입력과 snapshot 각각에 손실 확률을 적용하고, 살아남은 데이터그램을 delivery-time queue에 넣어 설정한 단방향 지연 후 전달한다. TCP 제어 채널에는 적용하지 않는다.

## TCP 패킷

모든 패킷은 아래 4바이트 헤더로 시작한다. `Size`에는 헤더가 포함된다.

```cpp
struct FPacketHeader
{
    uint16_t Size;
    uint16_t Type;
};
```

지원 타입:

| 방향 | 패킷 | 역할 |
|---|---|---|
| C→S | `C2S_Hello` | Magic/버전 검증 |
| S→C | `S2C_Connected` | 로컬 Player ID와 서버 tick rate 전달 |
| S→C | `S2C_PlayerSpawn` | 플레이어 생성 |
| S→C | `S2C_PlayerDespawn` | 플레이어 제거 |
| C→S | `C2S_Ping` | 클라이언트 timestamp 전달 |
| S→C | `S2C_Pong` | timestamp echo로 RTT 계산 |

`C2S_Hello`의 client option으로 UDP 또는 TCP 이동을 요청한다. `S2C_Connected`에는 서버가 승인한 이동 transport, UDP 포트, 세션별 64비트 토큰, 서버에 설정된 UDP 시뮬레이션 지연/손실 값이 포함된다. UDP 이동을 선택하면 이동 데이터는 다음 데이터그램으로 분리한다.

| 방향 | UDP 데이터그램 | 역할 |
|---|---|---|
| C→S | `FUdpMoveInput` | Player ID, 세션 토큰, 입력 sequence, 이동 축 전달 |
| S→C | `FUdpPlayerState` | Player ID, server tick, 위치, 회전, 처리한 입력 sequence 전달 |

UDP 데이터그램은 Magic, 프로토콜 버전, 타입, 전체 크기를 검증한다. 서버는 TCP로 발급한 토큰이 일치하는 입력만 적용한다. 입력 sequence가 마지막 처리 값보다 오래됐거나, 클라이언트가 받은 server tick이 해당 Actor의 마지막 tick보다 오래되면 폐기한다.

TCP 이동을 선택한 클라이언트는 기존 `C2S_MoveInput`과 `S2C_PlayerState` 패킷을 사용한다. 한 서버에 UDP와 TCP 이동 클라이언트가 동시에 접속할 수 있으며, 서버는 각 수신자에게 선택한 transport로 모든 플레이어 상태를 보낸다.

로컬 플레이어는 입력을 즉시 적용하고 전송에 성공한 입력의 sequence와 예측 이동량을 보관한다. 서버 snapshot을 받으면 `LastProcessedInput` 이하를 제거한 뒤 서버 권위 위치 위에 남은 입력과 아직 전송 전인 이동량을 재적용한다. 작은 차이는 부드럽게 수렴시키고 큰 오차만 즉시 보정한다.

## 빌드와 실행

Visual Studio에서 `Mundi.sln`을 열어 `Release_StandAlone|x64`를 빌드하거나 다음 명령을 사용한다.

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  Mundi.sln /m /p:Configuration=Release_StandAlone /p:Platform=x64
```

결과물은 `Binaries/Release_StandAlone`에 생성된다. 먼저 해당 디렉터리에서 서버를 실행한다.

```powershell
cd Binaries\Release_StandAlone
.\GameTechLabServer.exe
```

별도 터미널에서 클라이언트를 실행한다. 같은 명령을 세 번 실행하면 3클라이언트 동작을 확인할 수 있다.

```powershell
.\Mundi.exe -net -server=127.0.0.1 -port=7777
```

공백 형식도 지원한다.

```powershell
.\Mundi.exe -net -scene NetworkSample -server 127.0.0.1 -port 7777
```

영상 비교용 이동 기능 플래그:

```powershell
# 기본값: UDP 이동 + 입력 재현 보정
.\Mundi.exe -net -udp-movement=on -reconciliation=on

# TCP 이동으로 비교
.\Mundi.exe -net -udp-movement=off -reconciliation=on

# 지연된 서버 위치로 단순 보정되는 기존 현상 비교
.\Mundi.exe -net -udp-movement=on -reconciliation=off
```

동일한 별칭으로 `-udp-movement`, `-tcp-movement`, `-reconciliation`, `-no-reconciliation`도 지원한다. transport는 접속 handshake에서 정해지므로 실행 중 변경하지 않고 클라이언트를 다시 시작해 비교한다. HUD의 `Movement`와 `Reconciliation` 줄에서 실제 적용 상태를 확인할 수 있다.

로컬 플레이어는 청록색, 원격 플레이어는 주황색이며 WASD로 이동한다. 좌측 상단 `NETWORK` HUD에 연결 상태, ID, RTT, 서버 tick, 송수신량, 패킷 수, 추정 UDP 누락 수, 시뮬레이션 설정과 원격 플레이어 수가 표시된다. 첫 실행은 엔진 자산과 셰이더 초기화 때문에 연결까지 시간이 걸릴 수 있다.

## 설정과 로그

서버 설정은 `Config/Server.ini`에서 읽는다.

```ini
[Server]
Port=7777
UdpPort=7777
MaxClients=32
TickRate=30
MoveSpeed=5.0

[NetworkSimulation]
Enabled=false
LatencyMs=0
PacketLossPercent=0.0
RandomSeed=1337
```

`LatencyMs`는 각 UDP 방향에 독립적으로 적용되는 단방향 지연이다. 예를 들어 `150ms`이면 A의 입력이 서버까지 150ms, 서버 snapshot이 B까지 다시 150ms 지연되므로 원격 화면에서는 약 300ms에 처리 주기가 더해져 보일 수 있다. `PacketLossPercent`는 각 UDP 데이터그램을 독립 확률로 폐기한다. TCP 연결/생성/퇴장/Ping 패킷과 TCP 이동 모드에는 적용하지 않는다.

설정 파일을 수정하지 않고 다음처럼 실행 인자로 즉시 켤 수도 있다.

```powershell
.\GameTechLabServer.exe -latency=150 -loss=10
```

`-latency`는 0~300, `-loss`는 0~30으로 제한된다. `-netsim`, `-no-netsim`으로 설정 파일의 활성화 상태를 덮어쓸 수 있다. 다른 설정 파일은 `GameTechLabServer.exe -config=경로`로 지정한다. 실행 로그는 서버 작업 디렉터리의 `Logs/Server_YYYYMMDD_HHMMSS.log`에 기록되며, 시뮬레이션이 켜져 있으면 실제 지연·폐기 데이터그램 누적 수도 출력한다.

## 검증 결과

- `Debug|x64`, `Debug_StandAlone|x64`, `Release_StandAlone|x64` 솔루션/클라이언트 빌드 통과
- 분할된 Hello 패킷과 Hello+Ping 결합 수신 처리 통과
- TCP 테스트 클라이언트 3개에 ID `1001`, `1002`, `1003` 발급 확인
- 3개 UDP endpoint 등록과 30Hz 서버 권위 이동 snapshot 확인
- `seq=10` 뒤에 도착한 `seq=9` 입력 폐기 확인
- 이동 중 TCP `PlayerState` 패킷이 발생하지 않는 TCP/UDP 역할 분리 확인
- UDP 이동 클라이언트와 TCP 이동 클라이언트의 동시 접속 및 수신자별 상태 transport 확인
- 서버 `LastProcessedInput` 기반 확인 입력 제거와 미확인 로컬 예측 입력 재현 확인
- `100ms` 단방향 지연과 `30%` 손실에서 UDP 입력 ack 약 `338ms`, server-tick 누락 `20`회 확인
- 같은 시뮬레이션 서버의 TCP 이동 fallback은 ack 약 `25ms`, server-tick 누락 `0`회로 UDP 시뮬레이션 우회 확인
- Ping/Pong timestamp echo 및 Despawn broadcast 확인
- 실제 `Mundi.exe` 3개 TCP 접속, UDP movement 활성화와 정상 `WM_CLOSE` 종료 확인
