# GameTechLabEngine 네트워크 포트폴리오 구현 안내

## 구현 범위

네트워크 구현 명세서의 1차 완료 범위인 Phase 1~10, Phase 11의 UDP 이동, Phase 12의 Latency/Packet Loss Simulation을 구현했다.

- Winsock2 기반 전용 서버 `GameTechLabServer.exe`
- 복수 클라이언트 세션과 서버 발급 Entity ID
- 명시적 little-endian 직렬화와 TCP 스트림 프레이밍
- Partial Send/Receive 및 한 번에 합쳐진 여러 패킷 처리
- 서버 I/O를 `WSAPoll` 또는 IOCP로 선택하는 비교 실행 모드
- IOCP `AcceptEx`, Overlapped TCP/UDP 송수신, 완료 큐 워커 풀
- 공통 서버 CPU·스레드·처리량·틱 percentile·큐 깊이 통계와 CSV 기록
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

IOCP는 전용 서버에만 적용했다. 클라이언트는 서버 연결 하나만 관리하므로 기존 `WSAPoll` 네트워크 스레드를 유지한다.

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
├─ DedicatedServer.*     I/O 모드 선택, WSAPoll, 세션, 30Hz 서버 로직
├─ DedicatedServerIocp.cpp  AcceptEx와 TCP/UDP Overlapped I/O 워커 풀
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

IOCP 모드에서는 `AcceptEx`, `WSARecv`/`WSASend`, `WSARecvFrom`/`WSASendTo`를 Overlapped로 제출한다. 완료된 작업만 IOCP 큐에서 워커가 꺼내며, 워커는 패킷 조립과 검증까지만 수행하고 `IncomingEvents`에 값을 전달한다. 실제 플레이어와 월드 상태는 계속 서버 메인 스레드만 변경한다. 연결은 `shared_ptr`로 pending I/O 수명까지 유지하고, 연결별 TCP 수신 1개와 송신 1개만 동시에 제출해 패킷 순서와 부분 송신을 보존한다.

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
NetworkIoMode=IOCP
IocpWorkerThreads=4

[Performance]
Enabled=true
LogIntervalSeconds=1
CsvEnabled=true
VerboseConnectionLogs=true

[NetworkSimulation]
Enabled=false
LatencyMs=0
PacketLossPercent=0.0
RandomSeed=1337
```

`NetworkIoMode`는 기본 `IOCP`이며 `WSAPoll`로 바꾸면 기존 구현과 비교할 수 있다. `IocpWorkerThreads`는 실제 생성할 워커 수와 IOCP 동시 실행 제한이다. 기본값은 4이고, `0`이면 `논리 프로세서 수 - 1`로 자동 결정한다. 워커는 완료 작업이 없을 때 잠들기 때문에 생성된 수만큼 CPU를 계속 점유하지 않는다.

성능 통계는 기본적으로 1초마다 콘솔에 출력되고 `Logs/ServerPerformance_YYYYMMDD_HHMMSS_IOCP.csv` 또는 `..._WSAPoll.csv`에 같은 구간의 원본 수치가 기록된다. 주요 항목은 프로세스 전체 CPU 사용률, 실제 프로세스 스레드 수, 활성 연결/플레이어 수, 게임 틱 평균·p95·p99·최대 작업 시간과 예산 초과율, TCP/UDP 패킷·바이트 처리율, 큐 최대 깊이와 overflow, IOCP 완료율·pending I/O 또는 WSAPoll 호출·ready 이벤트 비율이다. CPU는 모든 논리 프로세서의 총 처리 능력을 100%로 환산하므로 현재 16 논리 프로세서에서는 CPU 코어 하나를 계속 사용하면 약 6.25%로 표시된다.

`VerboseConnectionLogs=false` 또는 `-quiet-connections`는 대량 접속 중 동기식 콘솔·파일 로그가 측정 결과를 왜곡하지 않도록 개별 접속/핸드셰이크/종료 로그를 끈다. 성능 통계와 오류 로그는 계속 출력된다. 서버 CSV에는 서버 처리 시간만 있으므로 네트워크 RTT의 평균·p95·p99는 부하 클라이언트에서 별도로 측정해야 한다.

`LatencyMs`는 각 UDP 방향에 독립적으로 적용되는 단방향 지연이다. 예를 들어 `150ms`이면 A의 입력이 서버까지 150ms, 서버 snapshot이 B까지 다시 150ms 지연되므로 원격 화면에서는 약 300ms에 처리 주기가 더해져 보일 수 있다. `PacketLossPercent`는 각 UDP 데이터그램을 독립 확률로 폐기한다. TCP 연결/생성/퇴장/Ping 패킷과 TCP 이동 모드에는 적용하지 않는다.

설정 파일을 수정하지 않고 다음처럼 실행 인자로 즉시 켤 수도 있다.

```powershell
.\GameTechLabServer.exe -latency=150 -loss=10

# I/O 방식 비교
.\GameTechLabServer.exe -io=iocp -iocp-workers=4 -quiet-connections
.\GameTechLabServer.exe -io=wsapoll -quiet-connections

# 통계 주기 변경 또는 비활성화
.\GameTechLabServer.exe -perf-interval=2
.\GameTechLabServer.exe -no-perf-stats
```

`-latency`는 0~300, `-loss`는 0~30으로 제한된다. `-netsim`, `-no-netsim`으로 설정 파일의 활성화 상태를 덮어쓸 수 있다. `-io=iocp`, `-io=wsapoll`, `-iocp-workers=0..64`로 I/O 설정을 덮어쓸 수 있다. `-port=1..65535`, `-udp-port=1..65535`, `-max-clients=1..1024`로 부하 테스트용 listen 설정도 즉시 덮어쓸 수 있다. 통계는 `-perf-stats`, `-no-perf-stats`, `-perf-csv`, `-no-perf-csv`, `-perf-interval=1..60`, `-quiet-connections`로 덮어쓸 수 있다. 다른 설정 파일은 `GameTechLabServer.exe -config=경로`로 지정한다. 실행 로그는 서버 작업 디렉터리의 `Logs/Server_YYYYMMDD_HHMMSS.log`에 기록된다.

## 대량 가상 클라이언트 부하 테스트

`GameTechLabLoadTester.exe`는 렌더링·물리·게임 윈도우 없이 한 프로세스에서 수백 개의 실제 TCP 연결과 클라이언트별 UDP 소켓을 생성한다. 기본 4개의 `WSAPoll` I/O 워커가 클라이언트를 분할 처리하므로 `Mundi.exe`를 500개 실행하는 것보다 부하 생성기 자체의 CPU·메모리 간섭이 작다.

PowerShell을 처음 연 상태라면 다음처럼 실행한다. IP 주소는 `-server 127.0.0.1` 공백 형식을 사용하면 PowerShell의 점(`.`) 포함 인자 해석 문제를 피할 수 있다.

```powershell
cd C:\Users\USER\Desktop\MakeGame\GameTechLabEngine\Binaries\Release_StandAlone

# 서버: 대량 접속 로그는 끄고 성능 통계는 유지
Start-Process .\GameTechLabServer.exe -ArgumentList '-max-clients=600','-io=iocp','-iocp-workers=4','-quiet-connections'

# 500개 접속 및 handshake 후 연결 유지
.\GameTechLabLoadTester.exe -server 127.0.0.1 -port 7777 -clients 500 -duration 60 -scenario connect -connect-rate 100

# 500개 TCP Ping/Pong: 클라이언트마다 초당 10회
.\GameTechLabLoadTester.exe -server 127.0.0.1 -port 7777 -clients 500 -duration 60 -scenario ping -rate 10 -connect-rate 100

# UDP 이동: 먼저 작은 수로 검증한 뒤 단계적으로 증가
.\GameTechLabLoadTester.exe -server 127.0.0.1 -port 7777 -clients 50 -duration 60 -scenario movement -rate 10 -connect-rate 50
```

시나리오별 측정 대상은 다음과 같다.

- `connect`: TCP 연결부터 `S2C_Connected` 수신까지의 handshake p50/p95/p99/max와 성공·실패·비정상 종료 수
- `ping`: 실제 `C2S_Ping`/`S2C_Pong` 왕복 RTT p50/p95/p99/max와 TCP 패킷·바이트 처리율
- `movement`: 실제 UDP 이동 입력, 자신의 `LastProcessedInput`까지 걸린 ACK p50/p95/p99/max, 수신 state packet rate, server tick 기반 추정 누락·역순 수신

1초 구간 통계는 콘솔과 `Logs/LoadTest_YYYYMMDD_HHMMSS_<scenario>.csv`에 기록된다. 생성기 CPU 사용률과 `local_send_drops`도 함께 기록하므로 서버 병목과 부하 생성기 병목을 구분할 수 있다. `-workers 0..64`, `-no-csv`, `-csv 경로`로 실행을 조정할 수 있으며 `-duration`에는 접속 ramp-up 시간이 포함된다.

`movement`는 각 플레이어 상태를 모든 UDP-ready 클라이언트에 보내므로 현재 샘플 월드에서는 출력량이 대략 `클라이언트 수² × TickRate`로 증가한다. 이는 IOCP 자체가 아니라 게임 replication 정책의 fan-out 비용이다. 500명 이동 테스트는 의도적인 극한 부하가 되므로 50→100→200 순으로 올리며 서버의 tick p99, UDP queue depth와 overflow를 함께 확인한다.

IOCP와 WSAPoll을 비교할 때는 같은 실행 시간·클라이언트 수·접속률·패킷률을 유지하고 서버만 `-io=iocp -iocp-workers=4` 또는 `-io=wsapoll`로 다시 실행한다. 서버의 `ServerPerformance_*.csv`와 부하 클라이언트의 `LoadTest_*.csv`를 한 쌍으로 보관한다.

## 검증 결과

- 헤드리스 가상 클라이언트 500개를 초당 100개씩 IOCP 서버에 연결해 `500/500` handshake 성공, 실패·비정상 종료·invalid packet `0` 확인
- 500명 초기 spawn 목록을 연결별 단일 TCP byte-stream command로 batch해 protocol packet 순서는 유지하면서 서버 command queue overflow `0` 확인
- 30개 클라이언트 TCP Ping을 초당 20회씩 실행해 RTT 2,789개와 양방향 약 600 packet/s 집계, CSV 출력 확인
- 8개 클라이언트 UDP 이동을 초당 10회씩 실행해 state 약 1,900 packet/s, ACK 380개, 추정 누락·역순·로컬 송신 누락 `0` 확인
- 동일한 500개 결합 Ping 입력에서 IOCP와 WSAPoll 모두 TCP 패킷·바이트 처리율, CPU, 스레드 수, 틱 percentile, 큐 깊이를 콘솔과 CSV에 기록하는 것 확인
- IOCP의 completion rate·pending I/O와 WSAPoll의 poll call·ready event rate가 모드별 CSV 열에 기록되는 것 확인
- `Debug_StandAlone|x64`와 `Release_StandAlone|x64` IOCP 서버 빌드 통과
- IOCP 워커 4개에서 분할 TCP Hello, 두 클라이언트 핸드셰이크, Ping/Pong, Despawn 통과
- IOCP UDP endpoint 등록, 이동 입력 ack와 상대 클라이언트 상태 수신 통과
- IOCP 양방향 `60ms` 지연 시뮬레이션에서 이동 상태 경로 약 `169ms` 확인
- IOCP 워커 4개와 동시 TCP 클라이언트 32개에서 전체 핸드셰이크와 Ping/Pong 통과
- pending `AcceptEx`/`WSARecvFrom`이 있는 상태의 Ctrl+Break 정상 종료와 I/O drain 통과
- 실행 인자 `-io=wsapoll`을 통한 기존 서버 폴백 핸드셰이크 통과
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
