-- BossSwordProjectile.lua
-- 보스 이기어검 스타일 칼 투사체
-- RotatingMovementComponent와 ProjectileMovementComponent 활용

local hitboxComp = nil       -- HitboxComponent

local hoverDuration = 2.0    -- 머리 위에서 대기하는 시간
local hoverTimer = 0         -- 대기 타이머
local isHovering = true      -- 현재 대기 중인지
local hasLaunched = false    -- 발사됐는지

local damage = 60            -- 데미지
local speed = 15             -- 발사 속도 (m/s)

local boss = nil             -- 보스 참조
local hoverHeight = 3.0      -- 보스 머리 위 높이
local hoverOffset = {X = 0, Y = 0}  -- 개별 칼의 오프셋 (원형 배치용)
local floatPhase = 0         -- 둥둥 떠다니는 위상
local launchDir = nil        -- 발사 방향

-- 글로벌 인덱스 카운터
SwordSpawnCounter = SwordSpawnCounter or 0

function BeginPlay()
    Obj.Tag = "BossSwordProjectile"
    hoverTimer = 0
    isHovering = true
    hasLaunched = false
    floatPhase = math.random() * math.pi * 2  -- 랜덤 위상으로 시작

    -- 컴포넌트 가져오기
    hitboxComp = GetComponent(Obj, "UHitboxComponent")

    -- 보스 찾기
    boss = FindObjectByName("Valthor, Luminous Blacksteel")

    -- 글로벌 테이블에서 오프셋 읽기 (BossAI에서 저장)
    local myIndex = SwordSpawnCounter
    SwordSpawnCounter = SwordSpawnCounter + 1

    if SwordOffsets and SwordOffsets[myIndex] then
        hoverOffset.X = SwordOffsets[myIndex].X
        hoverOffset.Y = SwordOffsets[myIndex].Y
        hoverHeight = SwordOffsets[myIndex].Height or hoverHeight
        print("[BossSword] Index " .. myIndex .. " offset (" .. hoverOffset.X .. ", " .. hoverOffset.Y .. ") height=" .. hoverHeight)
    else
        print("[BossSword] Index " .. myIndex .. " - no offset found, using default")
    end
end

function EndPlay()
    print("[BossSword] Destroyed")
end

function Tick(dt)
    -- 발사됐으면 플레이어 방향으로 이동
    if hasLaunched then
        if launchDir then
            local currentPos = Obj.Location
            local newX = currentPos.X + launchDir.X * speed * dt
            local newY = currentPos.Y + launchDir.Y * speed * dt
            local newZ = currentPos.Z + launchDir.Z * speed * dt
            Obj.Location = Vector(newX, newY, newZ)
        end
        return
    end

    if isHovering then
        hoverTimer = hoverTimer + dt
        floatPhase = floatPhase + dt * 3  -- 둥둥 떠다니는 속도

        -- Hover 시간 끝나면 발사
        if hoverTimer >= hoverDuration then
            LaunchTowardPlayer()
            return
        end

        -- 보스 못 찾았으면 다시 찾기
        if not boss then
            boss = FindObjectByName("Valthor, Luminous Blacksteel")
            if boss then
                print("[BossSword] Boss found!")
            else
                print("[BossSword] Boss NOT found!")
            end
        end

        -- 보스 위치 따라가기 + 둥둥 떠다니기
        if boss then
            local bossPos = boss.Location
            -- 위아래 둥둥 떠다니는 효과
            local floatZ = math.sin(floatPhase) * 0.3
            -- 좌우로도 살짝 흔들림
            local floatX = math.cos(floatPhase * 0.7) * 0.1

            Obj.Location = Vector(
                bossPos.X + hoverOffset.X + floatX,
                bossPos.Y + hoverOffset.Y,
                bossPos.Z + hoverHeight + floatZ
            )
        end

    end
end

function LaunchTowardPlayer()
    hasLaunched = true
    isHovering = false

    -- C++의 bIsHovering도 false로 설정
    SetSwordIsHovering(Obj, false)

    -- 플레이어 방향 계산
    local player = GetPlayer()
    if player then
        local myPos = Obj.Location
        local targetPos = player.Location

        -- 방향 벡터
        local dirX = targetPos.X - myPos.X
        local dirY = targetPos.Y - myPos.Y
        local dirZ = (targetPos.Z + 1.0) - myPos.Z  -- 플레이어 중심 높이

        -- 정규화
        local len = math.sqrt(dirX*dirX + dirY*dirY + dirZ*dirZ)
        if len > 0 then
            dirX = dirX / len
            dirY = dirY / len
            dirZ = dirZ / len
        end

        -- 발사 방향 저장 (Tick에서 이동 처리)
        launchDir = { X = dirX, Y = dirY, Z = dirZ }

        -- 칼 회전 (칼끝이 플레이어를 향하도록)
        local yaw = math.deg(math.atan(dirY, dirX))
        local horizontalDist = math.sqrt(dirX*dirX + dirY*dirY)
        local pitch = math.deg(math.atan(-dirZ, horizontalDist))  -- 위아래 각도
        Obj.Rotation = Vector(0, pitch, yaw)

        print("[BossSword] Launched! Dir=(" .. string.format("%.2f", dirX) .. ", " .. string.format("%.2f", dirY) .. ", " .. string.format("%.2f", dirZ) .. ") Rot=(0, " .. string.format("%.1f", pitch) .. ", " .. string.format("%.1f", yaw) .. ")")
    end

    -- 히트박스 활성화
    EnableHitbox(Obj, damage, "Heavy", 0.3, 0.3, 1.5)

end

function OnBeginOverlap(OtherActor)
    if OtherActor and OtherActor.Tag == "Player" then
        print("[BossSword] Hit player!")
        -- 히트 후 삭제
        DeleteObject(Obj)
    end
end

function OnEndOverlap(OtherActor)
end

-- 외부에서 호출 가능한 함수들
function SetHoverDuration(duration)
    hoverDuration = duration
end

function SetDamage(newDamage)
    damage = newDamage
end

function SetSpeed(newSpeed)
    speed = newSpeed
end

function SetHoverOffset(x, y)
    hoverOffset.X = x
    hoverOffset.Y = y
end

function SetHoverHeight(height)
    hoverHeight = height
end
