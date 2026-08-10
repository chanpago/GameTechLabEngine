#pragma once

#include "ActorComponent.h"
#include "Delegates.h"
#include "UStatsComponent.generated.h"

// ============================================================================
// UStatsComponent - HP/스태미나 관리 컴포넌트
// ============================================================================
// 사용법:
//   StatsComponent = CreateDefaultSubobject<UStatsComponent>("Stats");
//   StatsComponent->OnHealthChanged.AddDynamic(this, &AMyActor::HandleHealthChanged);
// ============================================================================

UCLASS(DisplayName = "스탯 컴포넌트", Description = "HP/스태미나 관리 컴포넌트")
class UStatsComponent : public UActorComponent
{
public:
    GENERATED_REFLECTION_BODY()

    // 델리게이트 선언 (ParticleSystemComponent 방식)
    // DECLARE_DELEGATE(OnHealthChanged, float /*Current*/, float /*Max*/);
    // DECLARE_DELEGATE(OnStaminaChanged, float /*Current*/, float /*Max*/);
    // DECLARE_DELEGATE(OnDeath);

    UStatsComponent();
    virtual ~UStatsComponent() = default;

    // ========================================================================
    // Lifecycle
    // ========================================================================
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime) override;

    // ========================================================================
    // HP 관련
    // ========================================================================

    /** 데미지를 적용합니다. */
    void ApplyDamage(float Damage);

    /** HP를 회복합니다. */
    void Heal(float Amount);

    /** HP를 직접 설정합니다. */
    void SetHealth(float NewHealth);

    /** HP를 최대로 회복합니다. */
    void RestoreFullHealth();

    /** 생존 여부 */
    bool IsAlive() const { return CurrentHealth > 0.f; }

    /** HP 퍼센트 (0.0 ~ 1.0) */
    float GetHealthPercent() const { return MaxHealth > 0.f ? CurrentHealth / MaxHealth : 0.f; }

    /** Getter */
    float GetCurrentHealth() const { return CurrentHealth; }
    float GetMaxHealth() const { return MaxHealth; }

    /** Setter */
    void SetCurrentHealth(float NewHealth) { CurrentHealth = FMath::Clamp(NewHealth, 0.f, MaxHealth); }
    void SetMaxHealth(float NewMaxHealth) { MaxHealth = NewMaxHealth; }

    // ========================================================================
    // 스태미나 관련
    // ========================================================================

    /**
     * 스태미나를 소모합니다.
     * @param Amount 소모량
     * @return 소모 성공 여부 (스태미나 부족 시 false)
     */
    bool ConsumeStamina(float Amount);

    /** 스태미나가 충분한지 확인합니다. */
    bool HasEnoughStamina(float Amount) const { return CurrentStamina >= Amount; }

    /** 스태미나를 회복합니다. */
    void RecoverStamina(float Amount);

    /** 스태미나를 최대로 회복합니다. */
    void RestoreFullStamina();

    /** 스태미나 퍼센트 (0.0 ~ 1.0) */
    float GetStaminaPercent() const { return MaxStamina > 0.f ? CurrentStamina / MaxStamina : 0.f; }

    /** Getter */
    float GetCurrentStamina() const { return CurrentStamina; }
    float GetMaxStamina() const { return MaxStamina; }

    // ========================================================================
    // 스태미나 비용 (밸런싱용)
    // ========================================================================
    UPROPERTY(EditAnywhere, Category = "Stamina Cost")
    float DodgeCost = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Stamina Cost")
    float LightAttackCost = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Stamina Cost")
    float HeavyAttackCost = 30.f;

    UPROPERTY(EditAnywhere, Category = "Stamina Cost")
    float BlockCostPerHit = 20.f;

    UPROPERTY(EditAnywhere, Category = "Stamina Cost")
    float ParryCost = 10.f;

    UPROPERTY(EditAnywhere, Category = "Stamina Cost", Tooltip = "막기 시작에 필요한 최소 스태미나")
    float BlockMinRequired = 20.f;

    UPROPERTY(EditAnywhere, Category = "Stamina Cost", Tooltip = "막기 중 초당 스태미나 소모량")
    float BlockDrainRate = 25.f;

    /** 스태미나 회복을 일시 중지합니다. */
    void PauseStaminaRegen() { bCanRegenStamina = false; }

    /** 스태미나 회복을 재개합니다. */
    void ResumeStaminaRegen() { bCanRegenStamina = true; TimeSinceStaminaUse = 0.f; }

    /** 스태미나를 강제로 소모합니다 (0 이하로 내려가면 0으로 클램프) */
    void DrainStamina(float Amount)
    {
        CurrentStamina = FMath::Max(0.f, CurrentStamina - Amount);
        TimeSinceStaminaUse = 0.f;
    }
    
    // ========================================================================
    // HP 스탯
    // ========================================================================
    UPROPERTY(EditAnywhere, Category = "Health")
    float MaxHealth = 100.f;

    UPROPERTY(EditAnywhere, Category = "Health")
    float CurrentHealth = 100.f;

    // ========================================================================
    // 스태미나 스탯
    // ========================================================================
    UPROPERTY(EditAnywhere, Category = "Stamina")
    float MaxStamina = 100.f;

    UPROPERTY(EditAnywhere, Category = "Stamina")
    float CurrentStamina = 100.f;

    UPROPERTY(EditAnywhere, Category = "Stamina", Tooltip = "초당 회복량")
    float StaminaRegenRate = 10.f;

    UPROPERTY(EditAnywhere, Category = "Stamina", Tooltip = "사용 후 회복 시작까지 딜레이")
    float StaminaRegenDelay = 20.0f;

    // ========================================================================
    // 포커스 관련
    // ========================================================================

    /** 포커스를 충전합니다. (Max를 넘지 않음) */
    void ChargeFocus(float Amount);

    /** 포커스를 소모합니다. */
    bool ConsumeFocus(float Amount);

    /** 포커스 퍼센트 (0.0 ~ 1.0) */
    float GetFocusPercent() const { return MaxFocus > 0.f ? CurrentFocus / MaxFocus : 0.f; }

    /** Getter */
    float GetCurrentFocus() const { return CurrentFocus; }
    float GetMaxFocus() const { return MaxFocus; }

    // ========================================================================
    // 포커스 스탯
    // ========================================================================
    UPROPERTY(EditAnywhere, Category = "Focus")
    float MaxFocus = 100.f;

    UPROPERTY(EditAnywhere, Category = "Focus")
    float CurrentFocus = 0.f;

    UPROPERTY(EditAnywhere, Category = "Focus", Tooltip = "차징 시 초당 충전량")
    float FocusChargeRate = 60.f;

private:
    float TimeSinceStaminaUse = 0.f;
    bool bCanRegenStamina = true;
};
