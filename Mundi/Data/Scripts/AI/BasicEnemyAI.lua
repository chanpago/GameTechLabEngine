-- ============================================================================
-- BasicEnemyAI.lua - BasicEnemy용 간단한 AI
-- ============================================================================

-- BT 프레임워크 로드
dofile("Data/Scripts/AI/BehaviorTree.lua")

-- ============================================================================
-- Context
-- ============================================================================

local ctx = {
    self_actor = nil,
    target = nil,
    stats = nil,
    time = 0,
    delta_time = 0,
    last_attack_time = 0,
    is_attacking = false,
}

-- ============================================================================
-- 설정값
-- ============================================================================

local Config = {
    DetectionRange = 15,        -- 15m
    AttackRange = 2,             -- 2m
    LoseTargetRange = 30,        -- 30m
    MoveSpeed = 200,             -- 이동 속도
    AttackCooldown = 2.0,        -- 공격 쿨다운
    TurnSpeed = 180,             -- 회전 속도
}

-- ============================================================================
-- Conditions
-- ============================================================================

local function HasTarget(c)
    return c.target ~= nil
end

local function IsTargetInDetectionRange(c)
    if not c.target or not c.self_actor then
        return false
    end
    local dist = VecDistance(Obj.Location, c.target.Location)
    return dist <= Config.DetectionRange
end

local function IsTargetInAttackRange(c)
    if not c.target or not c.self_actor then
        return false
    end
    local dist = VecDistance(Obj.Location, c.target.Location)
    return dist <= Config.AttackRange
end

local function IsTargetLost(c)
    if not c.target or not c.self_actor then
        return true
    end
    local dist = VecDistance(Obj.Location, c.target.Location)
    return dist > Config.LoseTargetRange
end

local function CanAttack(c)
    if c.is_attacking then
        return false
    end
    local timeSinceLastAttack = c.time - c.last_attack_time
    return timeSinceLastAttack >= Config.AttackCooldown
end

local function IsNotAttacking(c)
    return not c.is_attacking
end

-- ============================================================================
-- Actions
-- ============================================================================

local function DoFindTarget(c)
    c.target = GetPlayer()
    if c.target then
        return BT_SUCCESS
    end
    c.target = FindObjectByName("Player")
    if c.target then
        return BT_SUCCESS
    end
    return BT_FAILURE
end

local function DoLoseTarget(c)
    c.target = nil
    return BT_SUCCESS
end

local function DoChase(c)
    if not c.target then
        return BT_FAILURE
    end

    -- 공격 중이면 이동하지 않음
    if c.is_attacking then
        return BT_SUCCESS
    end

    local myPos = Obj.Location
    local targetPos = c.target.Location
    local dist = VecDistance(myPos, targetPos)

    -- 공격 범위에 도달하면 멈춤
    if dist <= Config.AttackRange then
        return BT_SUCCESS
    end

    -- 타겟 방향으로 회전
    local targetYaw = CalcYaw(myPos, targetPos)
    Obj.Rotation = Vector(0, 0, targetYaw)

    -- 이동
    local dir = VecDirection(myPos, targetPos)
    dir.Z = 0
    local moveDir = Vector(dir.X, dir.Y, dir.Z)
    AddMovementInput(Obj, moveDir, 1.0)

    return BT_SUCCESS
end

local function DoAttack(c)
    if not c.target then
        return BT_FAILURE
    end

    -- 타겟 바라보기
    local myPos = Obj.Location
    local targetPos = c.target.Location
    local targetYaw = CalcYaw(myPos, targetPos)
    Obj.Rotation = Vector(0, 0, targetYaw)

    -- 공격 실행
    c.is_attacking = true
    c.last_attack_time = c.time

    -- 글로벌 PlayMontage 함수 호출 (LuaManager에 등록됨)
    local success = PlayMontage(Obj, "Attack", 0.1, 0.1, 1.0)
    if success then
        return BT_SUCCESS
    end

    return BT_FAILURE
end

local function DoIdle(c)
    return BT_SUCCESS
end

-- ============================================================================
-- Behavior Tree
-- ============================================================================

local BasicEnemyTree = Selector({
    -- 1. 타겟 없으면 탐색
    Sequence({
        Condition(function(c)
            return not HasTarget(c) or IsTargetLost(c)
        end),
        Action(DoFindTarget)
    }),

    -- 2. 공격 범위 내 + 쿨다운 완료 → 공격
    Sequence({
        Condition(function(c)
            return HasTarget(c) and IsTargetInAttackRange(c) and CanAttack(c)
        end),
        Action(DoAttack)
    }),

    -- 3. 공격 중이면 대기 (이동 멈춤)
    Sequence({
        Condition(function(c)
            return c.is_attacking
        end),
        Action(DoIdle)
    }),

    -- 4. 타겟 있으면 추적
    Sequence({
        Condition(function(c)
            return HasTarget(c) and IsNotAttacking(c)
        end),
        Action(DoChase)
    }),

    -- 5. 기본 - 대기
    Action(DoIdle)
})

-- ============================================================================
-- Callbacks
-- ============================================================================

function BeginPlay()
    ctx.self_actor = Obj
    ctx.stats = GetComponent(Obj, "UStatsComponent")
    ctx.time = 0
    ctx.is_attacking = false

    -- 플레이어 찾기
    ctx.target = GetPlayer()
    if not ctx.target then
        ctx.target = FindObjectByName("Player")
    end
end

function Tick(Delta)
    ctx.time = ctx.time + Delta
    ctx.delta_time = Delta

    -- 죽었으면 AI 중지
    if ctx.stats and ctx.stats.CurrentHealth <= 0 then
        return
    end

    -- 공격 상태 업데이트 (몽타주 재생 중인지 체크)
    local bMontagePlayling = IsMontagePlayling(Obj)

    -- 공격 중이었는데 몽타주가 끝났으면 공격 종료
    if ctx.is_attacking and not bMontagePlayling then
        ctx.is_attacking = false
    end

    -- 몽타주 재생 중이면 공격 상태 유지
    if bMontagePlayling then
        ctx.is_attacking = true
    end

    -- BT 실행
    BasicEnemyTree(ctx)
end

function OnHit(OtherActor, HitInfo)
    if OtherActor and (OtherActor.Tag == "Player" or OtherActor.Tag == "PlayerWeapon") then
        local player = FindObjectByName("Player")
        if player then
            ctx.target = player
        end
    end
end

function EndPlay()
end
